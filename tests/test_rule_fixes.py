"""Optional rule fixes: settings, host catalog agreement, and the ROM facts they rely on."""
from __future__ import annotations

import json
from pathlib import Path
import re
import tempfile
import unittest

from srw64_native import rule_settings

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
TACTICAL_ROM, TACTICAL_VRAM = 0xAB160, 0x801C2600


def tactical(rom: bytes, vram: int, size: int) -> bytes:
    offset = vram - TACTICAL_VRAM + TACTICAL_ROM
    return rom[offset:offset + size]


class RuleSettingsTests(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.path = Path(temp.name) / "rules.json"

    def test_parse_orders_ids_and_rejects_unknown_ones(self):
        self.assertEqual(rule_settings.parse(""), ())
        self.assertEqual(rule_settings.parse("limit-cap,esp-level"), ("esp-level", "limit-cap"))
        for bad in ("esp", "esp-level,", ",limit-cap", "LIMIT-CAP"):
            with self.assertRaises(ValueError):
                rule_settings.parse(bad)

    def test_explicit_choice_is_remembered_and_defaults_to_the_corrections(self):
        # Nothing chosen yet: the bug-fix rules only, not the difficulty ones.
        self.assertEqual(rule_settings.select(self.path), rule_settings.DEFAULT)
        self.assertFalse(self.path.exists())
        self.assertEqual(rule_settings.select(self.path, preset="fixed"), rule_settings.CORRECTIONS)
        self.assertEqual(rule_settings.select(self.path), rule_settings.CORRECTIONS)
        self.assertEqual(rule_settings.select(self.path, preset="all"), tuple(rule_settings.RULE_FIXES))
        self.assertEqual(rule_settings.select(self.path, fixes="limit-cap"), ("limit-cap",))
        self.assertEqual(json.loads(self.path.read_text()),
                         {"schema": rule_settings.SCHEMA, "rules_version": 1, "fixes": ["limit-cap"]})
        self.assertEqual(rule_settings.select(self.path, fixes=""), ())
        self.assertEqual(rule_settings.select(self.path), (), "an empty saved list stays original")
        self.assertEqual(rule_settings.select(self.path, preset="original"), ())
        with self.assertRaises(ValueError):
            rule_settings.select(self.path, preset="fixed", fixes="")
        with self.assertRaises(ValueError):
            rule_settings.select(self.path, fixes="nope")
        self.assertEqual(rule_settings.select(self.path), ())

    def test_invalid_saved_file_is_reported(self):
        for content in ("[]", '{"schema": "other", "fixes": []}', '{"schema": "srw64.rule-settings.v1", "fixes": ["x"]}'):
            self.path.write_text(content)
            with self.assertRaises(ValueError):
                rule_settings.select(self.path)

    def test_recorded_rules_of_a_session(self):
        report = self.path.with_name("report.json")
        self.assertIsNone(rule_settings.recorded(report))
        report.write_text(json.dumps({"status": "native-graphics-run-completed"}))
        self.assertEqual(rule_settings.recorded(report), ())
        report.write_text(json.dumps({"rule_fixes": {"rules_version": 1, "enabled": ["seisenshi-level"]}}))
        self.assertEqual(rule_settings.recorded(report), ("seisenshi-level",))
        report.write_text(json.dumps({"rule_fixes": {"enabled": ["gone"]}}))
        self.assertIsNone(rule_settings.recorded(report))
        self.assertIn("原版", rule_settings.describe(()))

    def test_difficulty_rules_start_off(self):
        # Bug fixes are on for a first launch; the boss dummy and upgrade cap choices
        # change the original balance on purpose, so they are not in the default set.
        self.assertEqual(rule_settings.DEFAULT + rule_settings.DIFFICULTY, tuple(rule_settings.RULE_FIXES))
        self.assertEqual(rule_settings.DIFFICULTY, ("boss-dummy-half", "boss-dummy-none", "upgrade-cap-break",
                                                    "upgrade-refund", "parts-carry-over"))
        self.assertEqual(rule_settings.select(self.path), rule_settings.DEFAULT)

    def test_host_catalog_matches(self):
        header = (ROOT / "src/host/rule_fixes.hpp").read_text()
        catalog = re.search(r"catalog\[\]=\{(.*?)\};", header, re.S).group(1)
        entries = re.findall(r'"([a-z-]+)",Kind::(correction|difficulty)', catalog)
        self.assertEqual(tuple(rule for rule, _ in entries), tuple(rule_settings.RULE_FIXES))
        # The host groups the menu and the window by these kinds and derives its
        # default set from them, so they must match the Python registry exactly.
        self.assertEqual(tuple(rule for rule, kind in entries if kind == "correction"), rule_settings.CORRECTIONS)
        self.assertEqual(tuple(rule for rule, kind in entries if kind == "difficulty"), rule_settings.DIFFICULTY)
        self.assertIn(f"version={rule_settings.RULES_VERSION};", header)

    def test_every_rule_has_a_menu_label_in_each_locale(self):
        from srw64_native.profile import UI_KEYS
        keys = {"rules_menu", "rules_original", "rules_all", "rules_note"}
        keys |= {"rule_" + fix.replace("-", "_") for fix in rule_settings.RULE_FIXES}
        self.assertLessEqual(keys, UI_KEYS)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertEqual(sorted(key for key in keys if not ui.get(key)), [], locale)
            self.assertEqual(set(ui), UI_KEYS, locale)
        # The shared RmlUi settings page lists every catalog entry, grouped by kind,
        # and is rebuilt whenever the locale changes (the locale is in its stamp).
        page = (ROOT / "src/native/ui/frontend.cpp").read_text()
        self.assertIn("for(const auto& entry:rules::catalog)if(entry.kind==group)", page)
        self.assertIn("for(auto group:{rules::Kind::correction,rules::Kind::difficulty})", page)
        self.assertIn("const auto stamp=localization::catalog().locale+", page)
        self.assertIn("srw64.settings-control.v1", page)

    def test_hooked_routines_are_renamed_and_replaced(self):
        source = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original in (("801E1F08", "srw64_original_seisenshi_bonus"), ("801E1F10", "srw64_original_esp_bonus"),
                                  ("801E1D64", "srw64_original_potential_bonus"),
                                  ("801F4384", "srw64_original_battle_hit_rate"), ("80204254", "srw64_original_hit_estimate"),
                                  ("8020ABB4", "srw64_original_deploy_record")):
            self.assertIn(f'"load_000AB160_func_{address}": "{original}"', source)
            self.assertIn(f"void load_000AB160_func_{address}(", hooks)
            self.assertIn(f"{original}(rdram, ctx)", hooks)


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class RuleFixRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def test_bonus_routines_are_empty_and_hooked_entries_match(self):
        self.assertEqual(tactical(self.rom, 0x801E1F08, 8), bytes.fromhex("03E0000800000000"))
        self.assertEqual(tactical(self.rom, 0x801E1F10, 8), bytes.fromhex("03E0000800000000"))
        self.assertEqual(tactical(self.rom, 0x801F4384, 4), bytes.fromhex("27BDFFA8"))
        self.assertEqual(tactical(self.rom, 0x80204254, 4), bytes.fromhex("27BDFFB0"))

    def test_skill_bonus_rows_share_one_curve(self):
        curve = bytes([0, 10, 14, 18, 21, 24, 26, 28, 29, 30])
        nt = tactical(self.rom, 0x80218080, 20)
        self.assertEqual((nt[:10], nt[10:]), (curve, curve))
        unreferenced = tactical(self.rom, 0x802180B4, 30)
        self.assertEqual((unreferenced[:10], unreferenced[10:20]), (curve, curve))
        aura = tactical(self.rom, 0x80218930, 20)
        self.assertEqual([int.from_bytes(aura[i:i + 2], "big") for i in range(0, 20, 2)],
                         [value * 10 for value in unreferenced[20:]])

    def test_potential_table_and_original_bands(self):
        table = tactical(self.rom, 0x80217F90, 100)
        for level in range(10):
            self.assertEqual(list(table[level * 10:level * 10 + 10]),
                             [max(0, band + level - 9) * 10 for band in range(10)])
        code = tactical(self.rom, 0x801E1D64, 0x178)
        # First comparison: 90.0 <= ratio branches with band 1 in the delay slot,
        # the last one is 20.0 with band 9 as the fall-back, so column 0 is never read.
        self.assertEqual(code[0x44:0x48], bytes.fromhex("3C0142B4"))
        self.assertEqual(code[0x60:0x64], bytes.fromhex("24060001"))
        self.assertEqual(code[0x128:0x12C], bytes.fromhex("3C0141A0"))
        self.assertEqual(code[0x144:0x14C], bytes.fromhex("2406000924060008"))
        self.assertEqual(code.count(bytes.fromhex("24060000")), 0)
        # The kind argument (a0) is overwritten with the table address before any use.
        self.assertEqual(code[0x150:0x158], bytes.fromhex("3C04802124847F90"))

    def test_aura_attack_row_and_hyper_aura_weapons(self):
        # The row after the two hit/evade rows of the unreferenced table: x10 it is
        # +200..+1500, the +1500 at L9 the guidebook-era sources give.
        self.assertEqual(list(tactical(self.rom, 0x802180C8, 10)), [0, 20, 40, 60, 80, 100, 120, 130, 140, 150])
        # Condition 15 (聖戦士 L3) marks exactly the Hyper Aura family.
        gated = {weapon for weapon in range(1329) if self.rom[0x74E90 + weapon * 16 + 8] == 15}
        self.assertEqual(len(gated), 20)
        self.assertIn(954, gated)   # ダンバイン ハイパーオーラ斬り
        self.assertNotIn(953, gated)  # ダンバイン オーラ斬り (condition 14)
        # Neither damage routine reads the pilot's skill flags (+0x36) at all.
        for vram, size in ((0x801F5628, 0x550), (0x80203418, 0x330)):
            code = tactical(self.rom, vram, size)
            words = [code[i:i + 4] for i in range(0, len(code), 4)]
            self.assertFalse([w for w in words if w[0] & 0xFC == 0x90 and w[2:] == b"\x00\x36"])

    def test_status_page_compares_stat_plus_mobility_with_limit(self):
        # 801E7170..801E7188: lhu a3,0x10(s3); lhu v1,0x14(s3); ...; addu v0,a2,a3; slt v1,v1,v0
        page = tactical(self.rom, 0x801E7170, 0x1C)
        self.assertEqual(page[:8], bytes.fromhex("9667001096630014"))
        self.assertEqual(page[-8:], bytes.fromhex("00C710210062182A"))


if __name__ == "__main__":
    unittest.main()
