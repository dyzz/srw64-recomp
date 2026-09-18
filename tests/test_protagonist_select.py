"""Native protagonist selection page (docs/native/native-name-entry.md): hooks, labels and ROM facts."""
from __future__ import annotations

import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
TEXT_TABLE = 0x01A34980
EXPECTED = [("ブラッド・スカイウィンド", "カーツ・フォルネウス"), ("マナミ・ハミル", "アイシャ・リッジモンド"),
            ("アークライト・ブルー", "エルリッヒ・シュターゼン"), ("セレイン・メネス", "リッシュ・グリスウェル")]


def overlay(vram: int) -> int:
    """ROM offset of the name/selection overlay (ROM 0x1090A0 loaded at 801C2600)."""
    return vram - 0x801C2600 + 0x1090A0


class SelectionSourceTests(unittest.TestCase):
    def test_selection_step_is_renamed_and_wrapped(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        self.assertIn('"load_001090A0_func_801C50B8": "srw64_original_name_selection_step"', generator)
        body = hooks[hooks.index("void load_001090A0_func_801C50B8("):]
        body = body[:body.index("\n}\n")]
        self.assertIn("name_step(rdram,ctx,3)", body)
        self.assertIn("srw64_original_name_selection_step(rdram,ctx)", body)

    def test_page_labels_exist_in_every_locale(self):
        keys = {"name_step_select", "select_title", "select_hint", "select_super", "select_real", "select_male",
                "select_female", "select_confirm", "select_keyboard_hint"}
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertEqual(sorted(key for key in keys if not ui.get(key)), [], locale)


@unittest.skipUnless(ROM.exists(), "local original ROM is not present")
class SelectionRomTests(unittest.TestCase):
    rom = ROM.read_bytes() if ROM.exists() else b""

    def word(self, at: int, size: int = 4) -> int:
        return int.from_bytes(self.rom[at:at + size], "big")

    def test_default_names_decode_through_the_name_grid(self):
        from srw64_rom.glyphs import load_glyph_map
        glyphs = load_glyph_map(ROOT)

        def name(vram: int) -> str:
            codes = [self.word(overlay(vram) + 2 * i, 2) for i in range(7)]
            while codes and codes[-1] == 0xCA:   # the blank 0x1549 the original also writes
                codes.pop()
            text = ""
            for code in codes:
                entry = TEXT_TABLE + self.word(TEXT_TABLE + 4 + (0x147F + code) * 8)
                self.assertEqual(self.word(entry + 10, 2), 0xFFFF)   # one glyph per grid character
                text += glyphs[self.word(entry + 8, 2)]
            return text

        for route, (protagonist, partner) in enumerate(EXPECTED):
            base = route * 14
            self.assertEqual(name(0x801C6C00 + base) + "・" + name(0x801C6C70 + base), protagonist)
            self.assertEqual(name(0x801C6C38 + base) + "・" + name(0x801C6CA8 + base), partner)

    def test_confirm_path_the_page_replays(self):
        # 801C52FC addiu a2,zero,0x1549: the fill for a changed route; 801C532C jal 801C3744 loads defaults.
        self.assertEqual(self.word(overlay(0x801C52FC)), 0x24061549)
        self.assertEqual(self.word(overlay(0x801C532C)), 0x0C000000 | (0x801C3744 & 0x0FFFFFFF) >> 2)
        # State 1 into 801C6FB0, the route into 801C70B0, then the 5,1,2 fade.
        self.assertEqual(self.word(overlay(0x801C5344)), 0x24020001)
        self.assertEqual(self.word(overlay(0x801C5358)), 0x0C000000 | (0x80099814 & 0x0FFFFFFF) >> 2)
        # Browsing steps the route through 0..3 and wraps (801C34E4).
        self.assertEqual(self.word(overlay(0x801C3520)), 0x24020003)


if __name__ == "__main__":
    unittest.main()
