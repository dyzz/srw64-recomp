"""tools/mods/map_tool.py: a painted picture and a terrain grid become a layout, a tile atlas
and a palette the original draws (docs/design/level-editor.md §4), and the HD mode's files."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "mods"))
sys.path.insert(0, str(ROOT / "src"))
import map_tool  # noqa: E402
from srw64_native.original_images import decode_indexed, decode_map  # noqa: E402

ROM = ROOT / "rom.z64"
COLUMNS, ROWS = 24, 16


def picture(scale: int) -> Image.Image:
    """Grass with a road, a mirrored pair of houses and a pond, drawn at the game's size."""
    image = Image.new("RGB", (COLUMNS * 16, ROWS * 16), (60, 120, 50))
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            if 120 <= y < 136:
                pixels[x, y] = (150, 130, 90)
            elif (x * 7 + y * 13) % 11 == 0:
                pixels[x, y] = (50, 105, 45)
    house = Image.new("RGB", (16, 16), (120, 60, 40))
    for i in range(16):
        house.putpixel((i, i), (230, 220, 200))
    image.paste(house, (64, 48))
    image.paste(house.transpose(Image.Transpose.FLIP_LEFT_RIGHT), (96, 48))
    for y in range(160, 208):
        for x in range(200, 280):
            pixels[x, y] = (40, 80, 160 + (x + y) % 3 * 8)
    return image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)


@unittest.skipUnless(ROM.exists(), "no ROM")
class MapToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name) / "field"
        self.source.mkdir()
        self.rom = ROM.read_bytes()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, scale=2, **changes) -> Path:
        picture(scale).save(self.source / "base.png")
        terrain = [[1 if 7 <= y <= 8 else 5 for _ in range(COLUMNS)] for y in range(ROWS)]
        document = {"schema": map_tool.SCHEMA, "like": 20, "image": "base.png", "scale": scale, "terrain": terrain, **changes}
        (self.source / "map.json").write_text(json.dumps({k: v for k, v in document.items() if v is not None}), encoding="utf-8")
        return self.source

    def test_the_original_draws_the_picture(self):
        built = map_tool.build(self.write(), self.rom)
        image = built["image"]
        self.assertEqual((image["schema"], image["like"], image["columns"], image["rows"], image["hd"]), (map_tool.IMAGE_SCHEMA, 20, COLUMNS, ROWS, True))
        self.assertEqual(image["replaces"], {"layout": 6284, "atlas": 6228, "palette": 6243})   # map 20's
        layout, atlas, palette = (bytes.fromhex(image[f"{name}_hex"]) for name in ("layout", "atlas", "palette"))
        # The original's own reader takes them, and draws the picture (few colours: exactly).
        drawn, info = decode_map(layout, decode_indexed(atlas, palette))
        self.assertEqual((info["grid_columns"], info["grid_rows"], drawn.size), (COLUMNS, ROWS, (COLUMNS * 16, ROWS * 16)))
        self.assertIsNone(ImageChops.difference(drawn.convert("RGB"), built["preview"]).getbbox())
        wanted = picture(1)
        self.assertLess(max(abs(a - b) for a, b in zip(built["preview"].tobytes(), wanted.tobytes())), 9)   # five bits a channel
        # A house and its mirror image are one tile, the second drawn flipped.
        cell = lambda x, y: struct.unpack_from(">2H", layout, 8 + (y * COLUMNS + x) * 4)
        (left_flags, left_tile), (right_flags, right_tile) = cell(4, 3), cell(6, 3)
        self.assertEqual(left_tile, right_tile)
        self.assertEqual((left_flags ^ right_flags) & 0xE000, map_tool.FLIP_H)
        self.assertLess(image["tiles"], 40)
        self.assertNotIn(0, {struct.unpack_from(">H", layout, 8 + i * 4 + 2)[0] for i in range(COLUMNS * ROWS)})   # tile 0 is not drawn
        # Terrain: the outer two rings are the frame, the rest as written.
        terrain = [[cell(x, y)[0] & 0xFF for x in range(COLUMNS)] for y in range(ROWS)]
        self.assertEqual((terrain[0][5], terrain[1][5], terrain[5][1], terrain[5][COLUMNS - 2], terrain[ROWS - 2][5]), (99,) * 5)
        self.assertEqual((terrain[2][2], terrain[7][2], terrain[8][COLUMNS - 3], terrain[9][5]), (5, 1, 1, 5))
        # Colours: not index 0, not what map 20's cycles rewrite; those keep the original's.
        like = map_tool.template(self.rom, 20)
        self.assertTrue(like["cycles"])
        cycled = {i for first, count in like["cycles"] for i in range(first, first + count)}
        self.assertFalse(set(atlas[8:]) & (cycled | {0}))
        for index in cycled:
            self.assertEqual(palette[8 + index * 2:10 + index * 2], like["palette_bytes"][8 + index * 2:10 + index * 2])

    def test_the_hd_modes_files(self):
        out = Path(self.tmp.name) / "out"
        image = map_tool.write(map_tool.build(self.write(scale=4), self.rom), out, preview=True)
        self.assertEqual(sorted(p.name for p in out.iterdir()), ["base.png", "index.png", "map.json", "meta.json", "preview.png"])
        meta = json.loads((out / "meta.json").read_text())
        self.assertEqual((meta["schema"], meta["layout"], meta["width"], meta["height"], meta["scale"], len(meta["reference_palette"])),
                         ("srw64.hd-map-runtime.v0", 6284, COLUMNS * 16, ROWS * 16, 4, 256))
        with Image.open(out / "base.png") as base, Image.open(out / "index.png") as index:
            self.assertEqual((base.size, index.size, index.mode), ((COLUMNS * 64, ROWS * 64), (COLUMNS * 64, ROWS * 64), "L"))
            # A pixel's index is its colour in the reference palette.
            at = (300 * 4, 180 * 4)                                 # in the pond
            self.assertLess(max(abs(a - b) for a, b in zip(meta["reference_palette"][index.getpixel(at)][:3], base.getpixel(at))), 9)
        self.assertEqual(json.loads((out / "map.json").read_text()), image)
        # Scale 1: the original's files only.
        map_tool.write(map_tool.build(self.write(scale=1), self.rom), out)
        self.assertEqual(sorted(p.name for p in out.iterdir()), ["map.json"])

    def test_what_is_wrong_is_said(self):
        colony = next(m for m in range(map_tool.MAPS) if struct.unpack_from(">5H2B", self.rom, map_tool.MAP_RECORDS + m * 12)[5])
        for message, changes in {"like 要是原版地图号": {"like": 158}, "殖民地动画": {"like": colony},
                                 "scale 是": {"scale": 9}, "找不到底图": {"image": "other.png"}, "terrain 要是": {"terrain": [[0] * COLUMNS]},
                                 "不认识的字段": {"tiles": 3}}.items():
            with self.subTest(message=message), self.assertRaises(map_tool.MapError) as caught:
                map_tool.build(self.write(**changes), self.rom)
            self.assertIn(message, str(caught.exception))
        source = self.write(scale=2)                                # 768 x 512, said to be 5 times the game's
        document = json.loads((source / "map.json").read_text())
        (source / "map.json").write_text(json.dumps({**document, "scale": 5}))
        with self.assertRaises(map_tool.MapError) as caught:
            map_tool.build(source, self.rom)
        self.assertIn("不是 80 的整数倍", str(caught.exception))
        bad = [[5] * COLUMNS for _ in range(ROWS)]
        bad[4][4] = 99
        with self.assertRaises(map_tool.MapError):
            map_tool.build(self.write(terrain=bad), self.rom)


if __name__ == "__main__":
    unittest.main()
