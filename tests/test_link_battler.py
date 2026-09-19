"""Link Battler without a pak (src/host/link_battler.hpp): hooks, labels and the ROM facts it relies on."""
from __future__ import annotations

import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
HEADER = (ROOT / "src/host/link_battler.hpp").read_text()
RESIDENT_DELTA = 0x80075610   # resident VRAM - ROM
OVERLAY_ROM, OVERLAY_RAM = 0x8F4B0, 0x801C4500   # load_0008F4B0, the intermission


def lb_pilots() -> list[int]:
    table = re.search(r"lb_pilots\[lb_pilot_count\]=\{(.*?)\};", HEADER, re.S).group(1)
    return [int(value) for value in re.findall(r"-?\d+", table)]


def series() -> list[tuple[int, list[int], list[int]]]:
    rows = re.findall(r"\{(\d+),\{(-?\d+),(-?\d+)\},\{(\d+),(\d+)\}\}", HEADER)
    return [(int(v), [int(a), int(b)], [int(c), int(d)]) for v, a, b, c, d in rows]


class LinkSourceTests(unittest.TestCase):
    def test_driver_and_screen_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address in ("80090F44", "80090FA0", "80090FC4", "800910C4", "80091120", "80091284"):
            self.assertIn(f'"resident_func_{address}": "srw64_original_gbpak_', generator)
            body = hooks[hooks.index(f"void resident_func_{address}("):]
            self.assertIn("ctx->r2 = 0;", body[:body.index("}\n")])
        for address, original, hook in (("801D6FF4", "srw64_original_link_open", "link_begin"),
                                        ("801D70FC", "srw64_original_link_step", "link_step")):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"srw64_game_hooks.{hook}", body)

    def test_page_labels_exist_in_every_language(self):
        from srw64_native.profile import UI_KEYS
        keys = {key for key in UI_KEYS if key.startswith("link_")}
        # Per series: name, lead pilot, machines, the pilots who come along.
        for field in ("series", "lead", "units", "crew"):
            self.assertLessEqual({f"link_{field}_{name}" for name in ("f91", "goshogun", "zambot")}, keys)
        page = (ROOT / "src/host/macos/link_page_macos.mm").read_text()
        for key in keys - {key for key in keys if key.count("_") == 2 and key.split("_")[1] in ("series", "lead", "units", "crew")}:
            self.assertIn(f'"{key}"', page)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertTrue(all(ui.get(key) for key in keys), locale)

    def test_series_table(self):
        self.assertEqual(len(lb_pilots()), 92)
        self.assertEqual(series(), [(48, [42, 289], [23, 80]), (49, [184, -1], [55, 55]), (50, [206, -1], [59, 59])])


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class LinkRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def overlay(self, vram: int, size: int) -> bytes:
        start = vram - OVERLAY_RAM + OVERLAY_ROM
        return self.rom[start:start + size]

    def test_pilot_table_matches_d_801dcb90(self):
        self.assertEqual(list(struct.unpack(">92h", self.overlay(0x801DCB90, 184))), lb_pilots())

    def test_series_bits_match_d_801dcd0c(self):
        # D_801DCD0C..D_801DCD13: (byte, mask) of each series' pilot bits in the block.
        raw = self.overlay(0x801DCD0C, 8)
        bits = [raw[i] * 8 + (raw[i + 1].bit_length() - 1) for i in range(0, 8, 2)]
        self.assertEqual(bits, [23, 80, 55, 59])
        pilots = lb_pilots()
        self.assertEqual([pilots[bit] for bit in bits], [41, 230, 133, 152])   # シーブック セシリー 真吾 勝平
        for _, _, pilot_bits in series():
            self.assertTrue(set(pilot_bits) <= set(bits))

    def test_scene_masks_match_d_801dcabc(self):
        pairs = self.overlay(0x801DCABC, 28)
        masks = dict(zip(pairs[0::2], pairs[1::2]))
        header = [int(value) for value in re.search(r"scene_masks\[7\]=\{(.*?)\}", HEADER).group(1).split(",")]
        for scene in range(109, 123):
            self.assertEqual(masks[scene], header[(scene - 109) // 2], scene)

    def test_magic_strings_match_the_block_check(self):
        self.assertEqual(self.overlay(0x801DCB38, 16), b"ROBOT_TAISENN_GB")
        self.assertEqual(self.overlay(0x801DCB48, 16), b"LINK_BATTLER_V00")

    def test_link_screen_is_intermission_screen_8(self):
        # 801D8D20 calls D_801DC9D0[screen * 8] to set up and D_801DC9D4[screen * 8] to step.
        init, step = struct.unpack(">2I", self.overlay(0x801DC9D0 + 8 * 8, 8))
        self.assertEqual((init, step), (0x801D6FF4, 0x801D70FC))

    def test_hooked_driver_routines_are_the_ones_the_screen_calls(self):
        # 801D93C4 (cartridge check) calls 80090F44 then 800910C4; 801D9544 (read)
        # calls 80091120 then 80091284 with the block at 0xA000, 0x1000 bytes.
        def jal(target: int) -> bytes:
            return struct.pack(">I", 0x0C000000 | ((target >> 2) & 0x3FFFFFF))
        check = self.overlay(0x801D93C4, 0xC0)
        self.assertIn(jal(0x80090F44), check)
        self.assertIn(jal(0x800910C4), check)
        read = self.overlay(0x801D9544, 0x128)
        for target in (0x80090FC4, 0x80091120, 0x80091284):
            self.assertIn(jal(target), read)
        self.assertIn(struct.pack(">I", 0x3405A000), read)   # ori a1, zero, 0xA000
        self.assertIn(struct.pack(">I", 0x24071000), read)   # addiu a3, zero, 0x1000


if __name__ == "__main__":
    unittest.main()
