import json
from pathlib import Path
import struct
import unittest

from PIL import Image

from srw64_native.catalog import sha
from srw64_native.original_images import decode_indexed, decode_map
from srw64_rom.resources import ResourceTable

ROOT = Path(__file__).resolve().parents[1]


class OriginalImageDecoderTests(unittest.TestCase):
    def test_ci4_nibble_order_palette_alpha_and_ci8(self):
        palette = struct.pack(">6H", 3, 4, 0, 0, 0xF801, 0x07C0)
        ci4 = struct.pack(">4H", 14, 2, 1, 0) + b"\x01"
        image = decode_indexed(ci4, palette)
        self.assertEqual(image.getpixel((0, 0)), (255, 0, 0, 255))
        self.assertEqual(image.getpixel((1, 0)), (0, 255, 0, 0))
        for kind in (6, 8, 15):
            ci8 = struct.pack(">4H", kind, 2, 1, 0) + b"\x00\x01"
            self.assertEqual(decode_indexed(ci8, palette).tobytes(), image.tobytes())
        for bad in (ci4[:-1], ci4 + b"\0", ci4[:8] + b"\x02"):
            with self.assertRaises(ValueError):
                decode_indexed(bad, palette)
        with self.assertRaises(ValueError):
            decode_indexed(ci4, palette[:-1])

    def test_grouped_map_coordinates_and_all_mirror_combinations(self):
        atlas = Image.new("RGBA", (64, 64))
        for y in range(16):
            for x in range(16):
                atlas.putpixel((16 + x, y), (x * 10, y * 10, 100, 255))
        data = (struct.pack(">4H", 7, 1, 4, 4) + bytes(16) + struct.pack(">3H", 2, 4, 30)
                + b"".join(struct.pack(">3H", f, x, y) for f, x, y in
                           [(0, 0, 0), (0x6000, 16, 0), (0x8000, 0, 16), (0xC000, 16, 16)]))
        image, meta = decode_map(data, atlas)
        self.assertEqual(image.size, (32, 32))
        self.assertEqual([image.getpixel(p) for p in ((0, 0), (16, 0), (0, 16), (16, 16))],
                         [(0, 0, 100, 255), (150, 0, 100, 255), (0, 150, 100, 255), (150, 150, 100, 255)])
        self.assertEqual(meta["placement_flags"], [0, 0x6000, 0x8000, 0xC000])
        duplicate = data[:38] + b"\0\0" + data[40:]
        for bad in (data[:-1], data + b"\0", duplicate, data[:28] + b"\xFF\xFF" + data[30:]):
            with self.assertRaises(ValueError):
                decode_map(bad, atlas)


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class OriginalImageROMTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (ROOT / "rom.z64").read_bytes()
        cls.layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
        cls.resources = ResourceTable(cls.rom)
        cls.cache = {}

    def resource(self, rid):
        if rid not in self.cache:
            self.cache[rid] = self.resources.extract(rid)[0]
        return self.cache[rid]

    def test_all_actor_and_unit_bindings_decode_with_their_own_palette(self):
        known = {"actors": {28: (33, 333), 162: (166, 466), 165: (169, 469), 360: (283, 583)},
                 "units": {0: (688, 1010), 36: (718, 1010), 216: (898, 1010), 362: (1009, 1010)}}
        for category in ("actors", "units"):
            spec = self.layout["images"][category]
            table = self.rom[spec["rom_offset"]:spec["rom_offset"] + spec["stride"] * spec["count"]]
            self.assertEqual(sha(table), spec["sha256"])
            for index in range(spec["count"]):
                rid = struct.unpack_from(">H", table, index * spec["stride"])[0]
                pid = (struct.unpack_from(">H", table, index * spec["stride"] + 2)[0]
                       if category == "actors" else spec["palette_resource"])
                if index in known[category]:
                    self.assertEqual((rid, pid), known[category][index])
                image = decode_indexed(self.resource(rid), self.resource(pid))
                self.assertIn(image.size, [(96, 96), (97, 97)] if category == "actors" else [(16, 16)])

    def test_all_158_maps_have_complete_bounded_tile_coverage(self):
        spec = next(t for t in self.layout["tables"] if t["id"] == "map_assets")
        placements = 0
        for index in range(spec["count"]):
            rid, tid, pid = struct.unpack_from(">3H", self.rom, spec["rom_offset"] + index * spec["stride"])
            atlas = decode_indexed(self.resource(tid), self.resource(pid))
            image, meta = decode_map(self.resource(rid), atlas)
            self.assertEqual(meta["placements"], meta["grid_columns"] * meta["grid_rows"])
            self.assertEqual(meta["placements"] * 256, image.width * image.height)
            placements += meta["placements"]
            if index == 20:
                self.assertEqual((rid, tid, pid), (6284, 6228, 6243))
                self.assertEqual(image.size, (448, 512))
        self.assertEqual(spec["count"], 158)
        self.assertEqual(placements, 290706)


if __name__ == "__main__":
    unittest.main()
