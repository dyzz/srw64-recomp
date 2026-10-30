"""The weapon marker icons cut from the ROM font for the native weapon lists."""
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class WeaponMarkerTests(unittest.TestCase):
    def test_icons_come_from_the_font_through_the_text_palette(self):
        from srw64_rom.resources import ResourceTable
        from srw64_native.battle_assets import FONT_RESOURCE, MARKER_SCALE, TEXT_PALETTE, text_palette, weapon_markers
        table = ResourceTable((ROOT / "rom.z64").read_bytes())
        palette = text_palette(table.extract(TEXT_PALETTE)[0])
        self.assertEqual(palette[0][3], 0)                   # transparent
        self.assertEqual(palette[1], (255, 255, 255, 255))   # the text colour
        with tempfile.TemporaryDirectory() as directory:
            markers = weapon_markers(table.extract(FONT_RESOURCE)[0], table.extract(TEXT_PALETTE)[0], Path(directory))
            self.assertEqual(set(markers), {"格", "射", "P", "B", "MAP"})
            images = {token: Image.open(row["path"]).convert("RGBA") for token, row in markers.items()}
            for token, row in markers.items():
                self.assertEqual(images[token].size, (row["width"] * MARKER_SCALE, row["height"] * MARKER_SCALE))
                self.assertEqual(row["height"], markers["格"]["height"])   # one shared row band
            self.assertEqual((markers["P"]["width"], markers["MAP"]["width"]), (8, 13))
            # Each icon is its own glyph, not the letter or kanji it stands for.
            self.assertNotEqual(images["P"].tobytes(), images["B"].tobytes())
            self.assertNotEqual(images["格"].tobytes(), images["射"].tobytes())
            # The MAP badge is filled with the palette's red.
            colours = {pixel[:3] for pixel in images["MAP"].getdata() if pixel[3]}
            self.assertIn(palette[11][:3], colours)

    def test_the_crown_comes_from_the_wide_font_through_its_gold_palette(self):
        from srw64_rom.resources import ResourceTable
        from srw64_native.battle_assets import (CROWN_GLYPH, CROWN_PALETTE, MARKER_SCALE, WIDE_FONT_RESOURCE,
                                                double_move_marker, text_palette)
        table = ResourceTable((ROOT / "rom.z64").read_bytes())
        palette = text_palette(table.extract(CROWN_PALETTE)[0])
        self.assertEqual(palette[0][3], 0)
        with tempfile.TemporaryDirectory() as directory:
            crown = double_move_marker(table.extract(WIDE_FONT_RESOURCE)[0], table.extract(CROWN_PALETTE)[0], Path(directory))
            self.assertEqual((crown["glyph"], crown["width"], crown["height"]), (CROWN_GLYPH, 14, 12))
            image = Image.open(crown["path"]).convert("RGBA")
            self.assertEqual(image.size, (14 * MARKER_SCALE, 12 * MARKER_SCALE))
            colours = {pixel[:3] for pixel in image.getdata() if pixel[3]}
            # Gold with a near-black outline and red jewels, as the unit window shows it.
            self.assertIn(palette[1][:3], colours)
            self.assertIn(palette[3][:3], colours)
            self.assertIn(palette[12][:3], colours)


if __name__ == "__main__":
    unittest.main()
