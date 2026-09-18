"""BUG05: an enemy 五飛's dummy count is his player kill count (docs/base-fixes.md)."""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/recomp"))
import wing_kill_save  # noqa: E402
from srw64_native import original_saves  # noqa: E402

ROM = ROOT / "rom.z64"
RESIDENT_VRAM_TO_ROM = 0x80075610
TACTICAL_ROM, TACTICAL_VRAM = 0xAB160, 0x801C2600


def resident(rom: bytes, vram: int, size: int) -> bytes:
    return rom[vram - RESIDENT_VRAM_TO_ROM:vram - RESIDENT_VRAM_TO_ROM + size]


def tactical(rom: bytes, vram: int, size: int) -> bytes:
    offset = vram - TACTICAL_VRAM + TACTICAL_ROM
    return rom[offset:offset + size]


class BaseFixWiringTests(unittest.TestCase):
    def test_restore_hook_is_renamed_and_applied_unconditionally(self):
        source = (ROOT / "tools/recomp/generate_cpu.py").read_text()
        hooks = (ROOT / "tools/recomp/native-host/game_hooks.cpp").read_text()
        header = (ROOT / "tools/recomp/native-host/base_fixes.hpp").read_text()
        # 800A5054 is resident code, called directly by 800A84F8 and 80210758.
        self.assertIn('"resident_func_800A5054": "srw64_original_wing_kill_restore"', source)
        wrapper = re.search(r"void resident_func_800A5054\(.*?\n\}", hooks, re.S).group(0)
        self.assertIn("srw64::base_fixes::player_pilot(uint32_t(ctx->r4))", wrapper)
        self.assertIn("srw64_original_wing_kill_restore(rdram, ctx)", wrapper)
        # A base fix has no switch: the wrapper must not consult the rule catalog.
        self.assertNotIn("rules::", wrapper)
        self.assertIn("inline constexpr uint32_t pilot_table=0x80172F40,pilot_side_stride=0x1DB0;", header)

    def test_it_is_not_an_optional_rule(self):
        from srw64_native import rule_settings
        self.assertNotIn("wing-kill-dummy", rule_settings.RULE_FIXES)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertNotIn("rule_wing_kill_dummy", ui, locale)


class WingKillSaveTests(unittest.TestCase):
    def blank(self) -> bytearray:
        sram = bytearray(wing_kill_save.SRAM_BYTES)
        sram[0x40] = 0x12
        sram[wing_kill_save.BLOCK:wing_kill_save.BLOCK + 2] = wing_kill_save.checksum(sram).to_bytes(2, "big")
        return sram

    def test_sets_one_backup_and_keeps_the_block_valid(self):
        edited = wing_kill_save.set_kills(bytes(self.blank()), "wufei", 25)
        self.assertEqual(wing_kill_save.read_backup(edited), {"heero": 0, "duo": 0, "trowa": 0, "quatre": 0, "wufei": 25})
        self.assertEqual(int.from_bytes(edited[0x10:0x12], "big"), wing_kill_save.checksum(edited))
        self.assertEqual([i for i, (a, b) in enumerate(zip(self.blank(), edited)) if a != b], [0x11, 0x1F01])

    def test_checksum_ignores_the_second_slot(self):
        # The game's sum runs two bytes past the block into RAM that is always
        # 0000, not into the second slot's checksum word that follows it in SRAM.
        sram = self.blank()
        before = wing_kill_save.checksum(sram)
        sram[0x1F10:0x1F12] = (0x80AB).to_bytes(2, "big")
        self.assertEqual(wing_kill_save.checksum(sram), before)
        edited = wing_kill_save.set_kills(bytes(sram), "wufei", 3)
        self.assertTrue(original_saves.inspect_sram(b"SRW64V3" + edited[7:])["slots"]["intermission-1"]["checksum_matches"])

    def test_rejects_other_files_and_values(self):
        with self.assertRaises(ValueError):
            wing_kill_save.set_kills(bytes(0x4000), "wufei", 1)
        broken = self.blank()
        broken[0x11] ^= 1
        with self.assertRaises(ValueError):
            wing_kill_save.set_kills(bytes(broken), "wufei", 1)
        with self.assertRaises(ValueError):
            wing_kill_save.set_kills(bytes(self.blank()), "wufei", 1000)


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class WingKillRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def test_kill_credit_also_raises_the_wing_backup(self):
        # 801FB3E4: lhu v0,0x14(v1); addiu v0,v0,1; sh v0,0x14(v1) ... jal 800A4FBC
        self.assertEqual(tactical(self.rom, 0x801FB3E4, 12), bytes.fromhex("9462001424420001A4620014"))
        self.assertEqual(tactical(self.rom, 0x801FB44C, 4), bytes.fromhex("0C0293EF"))

    def test_restore_ignores_the_side(self):
        # 800A84F8 calls 800A5054 with the new pilot record in a0 (addu a0,s0,zero).
        self.assertEqual(resident(self.rom, 0x800A8690, 8), bytes.fromhex("0C02941502002021"))
        # 800A5054 reads only the actor (+2) and raises +0x14 to 801614E0[i].
        self.assertEqual(resident(self.rom, 0x800A5054, 4), bytes.fromhex("94820002"))
        self.assertEqual(resident(self.rom, 0x800A50B8, 8), bytes.fromhex("3C03801600621821"))
        self.assertEqual(resident(self.rom, 0x800A50C4, 16), bytes.fromhex("948200140043102B54400001A4830014"))
        # Jump table for actor - 0x59 (カトル, サリィ, 五飛, デュオ, トロワ, ノベンタ, ヒイロ).
        cases = {0x800A5088: 0, 0x800A5090: 1, 0x800A5098: 2, 0x800A50A0: 3, 0x800A50A8: 4, 0x800A50AC: None}
        table = resident(self.rom, 0x800D09B8, 28)
        indices = [cases[int.from_bytes(table[i:i + 4], "big")] for i in range(0, 28, 4)]
        self.assertEqual(indices, [3, None, 4, 1, 2, None, 0])

    def test_enemy_pilot_record_counts_dummies(self):
        # 801F6EA8: lhu v0,0x14(v0); sltiu v0,v0,1; negu v0,v0; ori a2,v0,0x15
        self.assertEqual(tactical(self.rom, 0x801F6EA8, 16), bytes.fromhex("944200142C4200010002102334460015"))
        # 801E05F4: lbu v0,-0x1EFF(v0); ori v0,v0,0x80 (map roster +1 for sides 1 and 2)
        self.assertEqual(tactical(self.rom, 0x801E05F4, 8), bytes.fromhex("9042E10134420080"))

    def test_enemy_wufei_records_carry_no_dummies(self):
        for offset, side in ((0x202A0C, 1), (0x208568, 2)):
            record = self.rom[offset:offset + 28]
            self.assertEqual(int.from_bytes(record[6:8], "big"), 91)
            self.assertEqual(int.from_bytes(record[20:22], "big"), side)
            self.assertEqual(record[22:26], bytes(4))


if __name__ == "__main__":
    unittest.main()
