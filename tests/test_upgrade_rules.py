import copy
import json
import re
import tempfile
import unittest
from pathlib import Path

from srw64_native import upgrade_rules

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"


def document(**body):
    return {"schema": upgrade_rules.SCHEMA, **body}


class UpgradeRulesValidationTests(unittest.TestCase):
    def assert_rejected(self, value):
        with self.assertRaises(ValueError):
            upgrade_rules.validate(value)

    def test_every_section_is_optional(self):
        upgrade_rules.validate(document())
        upgrade_rules.validate(document(description="x", stats={"hp": {"prices": [1000] * 15}}))
        upgrade_rules.validate(document(weapon_types={"4": {"increments": [2000] * 15}},
                                        unit_caps=[{"id": 124, "name": "ガンダムサンドロック", "cap": 7}],
                                        weapon_type_overrides=[{"id": 19, "type": 2}]))

    def test_ranges_and_names_match_the_host(self):
        self.assert_rejected({"stats": {}})
        self.assert_rejected(document(extra=1))
        self.assert_rejected(document(stats={"hp": {"prices": [1000] * 14}}))
        self.assert_rejected(document(stats={"hp": {"prices": [0] * 15}}))
        self.assert_rejected(document(stats={"hp": {"prices": [99999] * 15}}))
        self.assert_rejected(document(stats={"hp": {"prices": [1.5] * 15}}))
        self.assert_rejected(document(stats={"hp": {"prices": [True] * 15}}))
        self.assert_rejected(document(stats={"hp": {"increments": [2001] * 15}}))
        upgrade_rules.validate(document(stats={"hp": {"increments": [2000] * 15}}))
        self.assert_rejected(document(stats={"speed": {"prices": [1] * 15}}))
        self.assert_rejected(document(weapon_types={"0": {"prices": [1] * 15}}))
        self.assert_rejected(document(unit_caps=[{"id": 124, "cap": 4}]))
        self.assert_rejected(document(unit_caps=[{"id": 124, "cap": 16}]))
        self.assert_rejected(document(unit_caps=[{"id": 363, "cap": 7}]))
        self.assert_rejected(document(unit_caps=[{"id": 124, "cap": 7}, {"id": 124, "cap": 8}]))
        self.assert_rejected(document(weapon_type_overrides=[{"id": 19, "type": 5}]))
        self.assert_rejected(document(weapon_type_overrides=[{"id": 19, "type": 1, "note": "x"}]))

    def test_report_names_the_changed_sections(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rules.json"
            path.write_text(json.dumps(document(stats={"armor": {"prices": [9000] * 15}}, unit_caps=[{"id": 50, "cap": 9}])))
            summary = upgrade_rules.report(path)
            self.assertEqual((summary["stats"], summary["unit_caps"], summary["weapon_type_overrides"]), (["armor"], 1, 0))
            path.write_text("{")
            with self.assertRaises(ValueError):
                upgrade_rules.report(path)

    def test_constants_match_the_host(self):
        header = (ROOT / "tools/recomp/native-host/upgrade_rules.hpp").read_text()
        for name, value in (("max_increment", upgrade_rules.MAX_INCREMENT), ("max_increment_sum", upgrade_rules.MAX_INCREMENT_SUM),
                            ("min_price", upgrade_rules.MIN_PRICE), ("max_price", upgrade_rules.MAX_PRICE),
                            ("min_cap", upgrade_rules.MIN_CAP), ("max_cap", upgrade_rules.MAX_CAP)):
            self.assertRegex(header, rf"\b{name}={value}\b")
        self.assertIn(f'"{upgrade_rules.SCHEMA}"', header)
        self.assertIn('stat_names={"' + '","'.join(upgrade_rules.STATS) + '"}', header)
        for name, value in (("unit_records", upgrade_rules.UNIT_RECORDS), ("weapon_records", upgrade_rules.WEAPON_RECORDS),
                            ("stat_increments", upgrade_rules.STAT_INCREMENTS), ("weapon_increments", upgrade_rules.WEAPON_INCREMENTS),
                            ("stat_prices", upgrade_rules.STAT_PRICES)):
            self.assertRegex(header, rf"\b{name}=0x{value:X}\b")

    def test_hooked_routines_are_renamed_and_replaced(self):
        source = (ROOT / "tools/recomp/generate_cpu.py").read_text()
        hooks = (ROOT / "tools/recomp/native-host/game_hooks.cpp").read_text()
        for routine, original in (("resident_func_800A5254", "srw64_original_unit_stats"),
                                  ("resident_func_800A5F84", "srw64_original_weapon_twin_sync"),
                                  ("load_0008F4B0_func_801CF680", "srw64_original_upgrade_open"),
                                  ("load_0008F4B0_func_801C80E0", "srw64_original_upgrade_stats_view"),
                                  ("load_0008F4B0_func_801CF988", "srw64_original_upgrade_stats_step"),
                                  ("load_0008F4B0_func_801CF85C", "srw64_original_upgrade_ew_check"),
                                  ("load_0008F4B0_func_801D0C7C", "srw64_original_upgrade_weapon_view"),
                                  ("load_0008F4B0_func_801D1100", "srw64_original_upgrade_weapon_step"),
                                  ("load_00107BF0_func_801C2600", "srw64_original_sale_price")):
            self.assertIn(f'"{routine}": "{original}"', source)
            self.assertIn(f"void {routine}(", hooks)
            self.assertIn(f"{original}(rdram, ctx)", hooks)
        host = (ROOT / "tools/recomp/native-host/host.cpp").read_text()
        for call in ("upgrades::initialize(", "upgrades::configure(", "upgrades::patch_resident(",
                     "upgrades::read_text(", "upgrades::descriptor(", "upgrades::patch_copy("):
            self.assertIn(call, host)


class UpgradeSaveEditTests(unittest.TestCase):
    def test_levels_are_nibbles_and_the_checksum_follows(self):
        import sys
        sys.path.insert(0, str(ROOT / "tools/recomp"))
        import upgrade_save
        sram = bytearray(upgrade_save.SRAM_BYTES)
        at = upgrade_save.BLOCK + upgrade_save.UNITS + 2 * upgrade_save.UNIT_SIZE
        sram[at:at + 2] = (216 << 6 | 2).to_bytes(2, "big")
        sram[at + 4] = 0x80                                    # flags share the limit byte
        sram[at + 14:at + 16] = (4).to_bytes(2, "big")
        weapon = upgrade_save.BLOCK + upgrade_save.WEAPONS + 5 * upgrade_save.WEAPON_SIZE
        sram[weapon:weapon + 2] = (843).to_bytes(2, "big")
        sram[weapon + 4] = 0x01
        sram[upgrade_save.BLOCK:upgrade_save.BLOCK + 2] = upgrade_save.checksum(sram).to_bytes(2, "big")
        edited = upgrade_save.edit(bytes(sram), funds=900000, unit_levels={2: [9, 3, 0, 15, 7]}, weapon_levels={5: 9})
        row = upgrade_save.units(edited)[0]
        self.assertEqual((row["slot"], row["unit"], row["levels"]), (2, 216, [9, 3, 0, 15, 7]))
        self.assertEqual(row["weapons"][1], {"index": 5, "weapon": 843, "level": 9})
        self.assertEqual((edited[at + 4], edited[weapon + 4]), (0x87, 0x91))
        self.assertEqual(int.from_bytes(edited[upgrade_save.BLOCK + upgrade_save.FUNDS:][:4], "big"), 900000)
        self.assertEqual(int.from_bytes(edited[upgrade_save.BLOCK:upgrade_save.BLOCK + 2], "big"), upgrade_save.checksum(edited))
        with self.assertRaises(ValueError):
            upgrade_save.edit(bytes(sram), unit_levels={2: [16, 0, 0, 0, 0]})
        with self.assertRaises(ValueError):
            upgrade_save.edit(bytes(sram), unit_levels={3: [1, 0, 0, 0, 0]})


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class UpgradeRulesRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def test_original_values(self):
        values = upgrade_rules.original(self.rom)
        stats, weapons = values["stats"], values["weapon_types"]
        self.assertEqual(stats["hp"]["increments"], [200] * 15)
        self.assertEqual(stats["hp"]["prices"], [2000 * n for n in range(1, 16)])
        self.assertEqual(stats["en"]["increments"], [10] * 5 + [20] * 10)
        self.assertEqual(stats["mobility"]["increments"], [5] * 5 + [10] * 10)
        self.assertEqual(stats["armor"]["increments"], [100] * 5 + [150] * 10)
        self.assertEqual(stats["limit"], stats["en"])
        self.assertEqual(stats["mobility"]["prices"], [5000, 8000, 10000, 12000, 15000] + [20000 + 5000 * n for n in range(10)])
        self.assertEqual(stats["armor"]["prices"], [3000, 5000, 8000, 10000, 15000] + [20000 + 5000 * n for n in range(10)])
        for index, kind in enumerate(upgrade_rules.WEAPON_TYPES):
            self.assertEqual(weapons[kind]["prices"], [(5 - index) * 1000 * n for n in range(1, 16)])
        self.assertEqual(weapons["2"]["increments"], weapons["3"]["increments"])
        self.assertEqual(sum(weapons["1"]["increments"]), 3050)
        self.assertEqual(sum(weapons["4"]["increments"]), 2600)

    def test_caps_and_types(self):
        caps = upgrade_rules.unit_caps(self.rom)
        self.assertEqual({cap: caps.count(cap) for cap in set(caps)},
                         {6: 1, 7: 40, 8: 5, 9: 51, 10: 18, 11: 34, 12: 1, 13: 28, 15: 185})
        self.assertEqual((caps[124], caps[50], caps[262], caps[263], caps[56], caps[61]), (6, 7, 12, 13, 13, 11))
        types = upgrade_rules.weapon_types(self.rom)
        self.assertEqual({kind: types.count(kind) for kind in set(types)}, {0: 102, 1: 541, 2: 616, 3: 3, 4: 67})

    def test_template_is_valid_and_complete(self):
        full = upgrade_rules.template(self.rom, units=True, weapons=True)
        upgrade_rules.validate(full)
        self.assertEqual(len(full["unit_caps"]), 363)
        self.assertEqual(len(full["weapon_type_overrides"]), 1329)
        edited = copy.deepcopy(full)
        edited["unit_caps"][124]["cap"] = 7
        upgrade_rules.validate(edited)

    def test_screen_routines_read_the_cap_where_documented(self):
        overlay = lambda vram, size: self.rom[vram - 0x801C4500 + 0x8F4B0:][:size]
        # lbu v1,0x51(s0) at 801CFA78 (five stats) and lbu a1,0x51(s0) at 801CF8B0 (EW).
        self.assertEqual(overlay(0x801CFA78, 4), bytes.fromhex("92030051"))
        self.assertEqual(overlay(0x801CF8B0, 4), bytes.fromhex("92050051"))
        # 801D1384: lbu v0,%lo(D_8016A261)(at), then bne v1,v0 (the full-upgrade test).
        self.assertEqual(overlay(0x801D1384, 8), bytes.fromhex("9022A26114620027"))
        # 800A5F84 is the call right before it.
        self.assertEqual(overlay(0x801D1354, 4), bytes.fromhex("0C0297E1"))


if __name__ == "__main__":
    unittest.main()
