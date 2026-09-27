"""The 部隊名 stays マーチウィンド (docs/native/fixed-unit-name.md): hooks, labels, ROM facts."""
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
TEXT_TABLE = 0x01A34980
# Opening events that ask about the name, by ROM offset: scene 33, 62, 46/58.
EVENTS = {0x1AB86C: 24045, 0x1AC6F0: 24417, 0x1BC304: 32003}


def overlay(vram: int, rom: int = 0x1090A0) -> int:
    """ROM offset in an overlay loaded at 801C2600 (the name overlay by default)."""
    return vram - 0x801C2600 + rom


def resident(vram: int) -> int:
    return vram - 0x80075610


def jal(target: int) -> int:
    return 0x0C000000 | (target & 0x0FFFFFFF) >> 2


class UnitNameSourceTests(unittest.TestCase):
    def test_hooks_are_installed_by_the_game_host_only(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        self.assertIn('"load_000A7EC0_func_801C517C": "srw64_original_unit_name_command"', generator)
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        body = hooks[hooks.index("void load_000A7EC0_func_801C517C("):]
        body = body[:body.index("\n}\n")]
        self.assertIn("unit_name_page(rdram))return;", body)
        self.assertIn("srw64_original_unit_name_command(rdram,ctx)", body)
        body = hooks[hooks.index("void resident_func_8009FA94("):]
        body = body[:body.index("\n}\n")]
        self.assertLess(body.index("choice_step(rdram, uint32_t(ctx->r4))) return;"), body.index("srw64_original_dialogue_choice"))
        # Only the RT64 game host installs them; CPU-only and fixed-frame hosts keep the original flow.
        self.assertIn("target_sources(srw64-gfx-host PRIVATE unit_name.cpp)", (ROOT / "src/host/CMakeLists.txt").read_text())
        host = (ROOT / "src/host/host.cpp").read_text()
        self.assertGreater(host.index("srw64::unit_name::configure(output_dir);"), host.index("#if defined(SRW64_WITH_RT64)\n    srw64_set_capture_directory"))
        # The default is read from the dialogue catalogs, so they must be loaded first.
        self.assertGreater(host.index("srw64::unit_name::configure(output_dir);"), host.index("srw64::dialogue::configure(output_dir);"))

    def test_choices_match_the_hook(self):
        header = (ROOT / "src/host/unit_name.hpp").read_text()
        listed = re.search(r"naming_choices\[\]=\{([0-9,]+)\}", header)[1]
        self.assertEqual(sorted(int(text) for text in listed.split(",")), sorted(EVENTS.values()))

    def test_default_name_in_every_locale(self):
        names = {locale: json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]["unit_default_name"]
                 for locale in ("ja", "zh-Hans", "en")}
        terms = json.loads((ROOT / "content/translation/story-terms.json").read_text())["terms"]["マーチウィンド"]
        self.assertEqual(names, {"ja": "マーチウィンド", "zh-Hans": terms["zh"], "en": terms["en"]})


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class UnitNameRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def word(self, at: int, size: int = 4) -> int:
        return int.from_bytes(self.rom[at:at + size], "big")

    def glyph_map(self) -> dict[int, str]:
        from srw64_rom.glyphs import load_glyph_map
        return {0: " ", **load_glyph_map(ROOT)}

    def text(self, record: int) -> str:
        glyphs, at = self.glyph_map(), TEXT_TABLE + self.word(TEXT_TABLE + 4 + record * 8) + 8
        out = ""
        while (code := self.word(at, 2)) != 0xFFFF:
            out += glyphs.get(code, "|")
            at += 2
        return out

    def test_each_naming_choice_comes_before_its_3d5e(self):
        for event, text in EVENTS.items():
            block = self.rom[event:event + 0x800]
            choice = block.index(bytes.fromhex("3d4400000002") + text.to_bytes(2, "big"))
            # 3E10 (first answer) follows the choice; 3D5E comes only after 3E11 (second answer).
            self.assertEqual(block[choice + 8:choice + 10], b"\x3e\x10", hex(event))
            self.assertLess(block.index(b"\x3e\x11", choice), block.index(b"\x3d\x5e", choice), hex(event))
            lines = self.text(text)
            self.assertIn("それでかまわない", lines)
            self.assertIn("気に入らない", lines)
            self.assertLess(lines.index("それでかまわない"), lines.index("気に入らない"))

    def test_choice_handler_the_hook_replays(self):
        choice = resident(0x8009FA94)
        self.assertEqual(self.word(choice + 0x10), 0x86020026)             # lh v0,0x26(s0): first frame when 0
        self.assertEqual(self.word(resident(0x8009FBE0)), 0xA6000024)      # sh zero,0x24(s0): done
        self.assertEqual(self.word(resident(0x8009FBE4)), 0x24420006)      # PC += 6
        self.assertEqual(self.word(resident(0x8009FBE8)), 0x24633DD9)      # answer = 0x3DD9 + cursor
        self.assertEqual(self.word(resident(0x8009FBF0)), 0xA4830000)      # into engine+0x994 (ctx+0xC)

    def test_3d5e_only_switches_to_the_name_overlay(self):
        # 800A1050 calls the world-map overlay's 801C517C, then completes the command.
        self.assertEqual(self.word(resident(0x800A105C)), jal(0x801C517C))
        self.assertEqual(self.word(resident(0x800A1064)), 0xA6000024)
        handler = overlay(0x801C517C, 0xA7EC0)
        self.assertEqual(self.word(handler + 0x14), jal(0x80080188))
        self.assertEqual(self.word(handler + 0x18), 0x24040006)          # mode 6: the 部隊名 page

    def test_default_name(self):
        glyphs = self.glyph_map()
        self.assertEqual("".join(glyphs[self.word(overlay(0x801C6F54) + 2 * i, 2)] for i in range(7)), "マーチウィンド")
        # 801C5FCC copies it into 8010F698 with a 0xFFFF end on a new game.
        self.assertEqual(self.word(overlay(0x801C5FF8)), 0x3C04801C)
        self.assertEqual(self.word(overlay(0x801C5FFC)), 0x24846F54)


if __name__ == "__main__":
    unittest.main()
