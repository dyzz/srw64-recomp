import unittest
from pathlib import Path

from PIL import ImageFont

from srw64_rom.glyphs import load_glyph_map

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "content/fonts/SRW64Symbols.ttf"
# Advances in 1/1000 em: the triangles keep Arial Unicode MS's width, the wrench is full width.
SYMBOLS = {"▶": 600, "▷": 600, "◀": 600, "🔧": 1000}
# The weapon markers at U+E000 + their ROM glyph id: narrow 8:10 icons, the square traced
# fist and the MAP badge.
MARKERS = {241: 680, 242: 680, 243: 680, 244: 760, 575: 1060}


class SymbolFontTests(unittest.TestCase):
    def setUp(self):
        self.font = ImageFont.truetype(str(FONT), 1000)

    def test_holds_the_four_symbols(self):
        for char, advance in SYMBOLS.items():
            with self.subTest(char=char):
                self.assertEqual(self.font.getlength(char), advance)
                self.assertIsNotNone(self.font.getbbox(char))
        # Anything else is the empty .notdef (500), so the text engine keeps looking.
        self.assertEqual(self.font.getlength("A"), 500)

    def test_holds_the_weapon_markers(self):
        mapping = load_glyph_map(ROOT)
        for glyph_id, advance in MARKERS.items():
            with self.subTest(glyph=glyph_id):
                char = chr(0xE000 + glyph_id)
                self.assertEqual(self.font.getlength(char), advance)
                left, top, right, bottom = self.font.getbbox(char)
                self.assertLessEqual(bottom - top, 800)          # one band, never taller than text
                self.assertIn(mapping[glyph_id], {"P", "B", "射", "格", "MAP"})

    def test_never_raises_a_line(self):
        # HarmonyOS Sans SC hhea ascent/descent; the engine takes the tallest face's ascent.
        self.assertEqual(self.font.getmetrics(), (928, 244))

    def test_symbols_come_from_the_glyph_map(self):
        self.assertLessEqual(set(SYMBOLS), set(load_glyph_map(ROOT).values()))

    def test_licence_travels_with_the_font(self):
        text = (FONT.parent / "LICENSE-SRW64Symbols.txt").read_text(encoding="utf-8")
        self.assertIn("Bitstream Vera Fonts Copyright", text)
        self.assertIn("DejaVu changes are in public domain", text)


if __name__ == "__main__":
    unittest.main()
