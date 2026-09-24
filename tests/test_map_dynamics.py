import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("map_dynamics", ROOT / "tools/content/map_dynamics.py")
DYNAMICS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DYNAMICS)


class CycleFormatTests(unittest.TestCase):
    def test_header(self):
        data = bytes([2, 0xC0, 3, 9, 7]) + bytes(3 * 2 * 2)
        self.assertEqual(DYNAMICS.cycle_info(data), {"first_index": 0xC0, "colors": 3, "frames": 2, "cycle_ticks": 16})
        with self.assertRaises(ValueError):
            DYNAMICS.cycle_info(data[:-1])


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class RomMapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.maps = DYNAMICS.survey((ROOT / "rom.z64").read_bytes(), None)["maps"]

    def test_counts(self):
        visible = lambda m, name=None: any(c["pixels"] and (name is None or c["name"] == name) for c in m["cycles"])
        self.assertEqual(len(self.maps), 158)
        self.assertEqual(len({m["layout"] for m in self.maps}), 154)
        self.assertEqual(sum(visible(m) for m in self.maps), 67)
        self.assertEqual(sum(visible(m, "水面") for m in self.maps), 36)
        self.assertEqual(sum(m["colony"]["active"] for m in self.maps), 77)
        self.assertEqual(sum(bool(m["colony"]["instances"]) for m in self.maps), 39)
        self.assertEqual(sum(visible(m) or bool(m["colony"]["instances"]) for m in self.maps), 104)

    def test_stage_one_map(self):
        stage_one = self.maps[20]
        self.assertEqual((stage_one["layout"], stage_one["atlas"], stage_one["palette"]), (6284, 6228, 6243))
        self.assertEqual([(c["resource"], c["pixels"]) for c in stage_one["cycles"]], [(6422, 13010), (6429, 69)])


if __name__ == "__main__":
    unittest.main()
