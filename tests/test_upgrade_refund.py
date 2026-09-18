"""Upgrade refund (src/host/upgrade_refund.hpp): hooks, labels and the ROM facts it relies on."""
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest

from srw64_native import rule_settings

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
HEADER = (ROOT / "src/host/upgrade_refund.hpp").read_text()
RESIDENT_DELTA = 0x80075610   # resident VRAM - ROM (upgrade_rules.hpp)


def story_grants() -> dict[int, int]:
    table = re.search(r"story_grants\[\]=\{(.*?)\};", HEADER, re.S).group(1)
    return {int(unit): int(levels) for unit, levels in re.findall(r"\{(\d+),(\d+)\}", table)}


class RefundSourceTests(unittest.TestCase):
    def test_story_routines_are_renamed_and_wrapped(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, scope in (("800AAD28", "srw64_original_unit_register", "refund::Registration"),
                                         ("800AA464", "srw64_original_unit_remove", "refund::Source::removal"),
                                         ("800AB808", "srw64_original_unit_merge", "refund::Source::merge"),
                                         ("800AA3C4", "srw64_original_unit_delete", "refund::before_delete")):
            self.assertIn(f'"resident_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void resident_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(scope, body)

    def test_rule_is_a_difficulty_choice_with_labels_and_a_notice(self):
        self.assertIn("upgrade-refund", rule_settings.DIFFICULTY)
        self.assertNotIn("upgrade-refund", rule_settings.DEFAULT)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertTrue(ui["rule_upgrade_refund"], locale)
            self.assertIn("{unit}", ui["refund_notice"], locale)
            self.assertIn("{amount}", ui["refund_notice"], locale)

    def test_grant_list_is_sorted_and_unique(self):
        grants = story_grants()
        self.assertEqual(len(grants), 26)
        self.assertEqual(list(grants), sorted(grants))
        self.assertTrue(all(1 <= levels <= 8 for levels in grants.values()))


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class RefundRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def test_grants_match_the_original_3d6c_commands(self):
        from srw64_native.catalog import source_catalog, text_headers
        from srw64_native.original_data import check_layout, extract_gameplay
        from srw64_native.original_scripts import attach_script_catalog
        layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
        check_layout(self.rom, layout)
        sources, _, _ = source_catalog(ROOT, ROM)
        data = extract_gameplay(self.rom, layout, sources)
        attach_script_catalog(data, self.rom, layout, sources, text_headers(ROOT, ROM))
        found: dict[int, int] = {}
        commands = 0
        for event in data["stage_events"]:
            for instruction in event["script"].get("instructions", []):
                if instruction.get("opcode") == 0x3D6C:
                    unit, levels = instruction["operands"][:2]
                    found[unit] = max(found.get(unit, 0), levels)
                    commands += 1
        self.assertEqual(commands, 38)
        self.assertEqual(found, story_grants())
        # Machine names start at text id 527, as the refund notice reads them.
        self.assertEqual(layout["names"]["unit_base"], 527)
        self.assertIn("unit_name_base=527", HEADER)

    def test_predecessor_table(self):
        # 800AA814 scans 30 (successor, predecessor) s16 pairs at D_800CA3A0.
        start = 0x800CA3A0 - RESIDENT_DELTA
        pairs = [(int.from_bytes(self.rom[start + i * 4:start + i * 4 + 2], "big", signed=True),
                  int.from_bytes(self.rom[start + i * 4 + 2:start + i * 4 + 4], "big", signed=True)) for i in range(30)]
        self.assertIn((126, 124), pairs)   # サンドロック → 改
        self.assertIn((121, 119), pairs)   # ウイングゼロ → カスタム
        self.assertNotIn(52, [successor for successor, _ in pairs])   # アウドムラ inherits nothing
        # The registration's lookup: lh v0,-0x5C60(v0) against the new machine.
        self.assertEqual(self.rom[0x800AA82C - RESIDENT_DELTA:0x800AA830 - RESIDENT_DELTA], bytes.fromhex("8442A3A0"))


if __name__ == "__main__":
    unittest.main()
