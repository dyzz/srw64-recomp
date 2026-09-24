from collections import Counter
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("asset_inventory", ROOT / "tools/content/asset_inventory.py")
INVENTORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INVENTORY)


class ClassifyTests(unittest.TestCase):
    def test_header_kinds(self):
        classify = INVENTORY.classify
        self.assertEqual(classify(struct.pack(">4H", 3, 4, 0, 0) + bytes(4)), {"kind": "palette", "colors": 2})
        self.assertEqual(classify(struct.pack(">4H", 14, 3, 2, 0) + bytes(3))["kind"], "image4")
        self.assertEqual(classify(struct.pack(">4H", 15, 3, 2, 0) + bytes(6))["kind"], "image8")
        self.assertEqual(classify(bytes.fromhex("36340038") + bytes(8)), {"kind": "geometry"})
        # A CI8 header whose pixel count does not match falls through to data.
        self.assertEqual(classify(struct.pack(">4H", 15, 3, 2, 0) + bytes(5))["kind"], "data")

    def test_ranges_are_contiguous(self):
        ranges = INVENTORY.RANGES
        self.assertEqual(ranges[0][0], 0)
        self.assertTrue(all(a[1] + 1 == b[0] for a, b in zip(ranges, ranges[1:])))


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class RomInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = INVENTORY.inventory((ROOT / "rom.z64").read_bytes())
        cls.groups = {g["category"]: g for g in INVENTORY.summary(cls.rows) if g["category"] != "data"}

    def test_totals(self):
        kinds = Counter(row["kind"] for row in self.rows)
        self.assertEqual(len(self.rows), 6436)
        self.assertEqual((kinds["image4"], kinds["image8"], kinds["palette"], kinds["geometry"], kinds["map-layout"]),
                         (919, 722, 1368, 483, 155))

    def test_documented_categories(self):
        expected = {"portrait": 300, "map.unit-icon": 323, "battle.unit-sprite": 296, "battle.effect": 238,
                    "chapter-title": 117, "intermission.background": 8, "intro.text-page": 30,
                    "battle.3d-background-texture": 49, "map.tileset": 9}
        self.assertEqual({name: self.groups[name]["images"] for name in expected}, expected)
        self.assertEqual(self.groups["geometry"]["geometry"], 483)
        self.assertEqual(self.groups["map.layout"]["layouts"], 155)


if __name__ == "__main__":
    unittest.main()
