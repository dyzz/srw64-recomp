import importlib.util
from pathlib import Path
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pixel_scale", ROOT / "tools/hd_ai/pixel_scale.py")
SCALE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCALE)

PALETTE = [(0, 0, 0, 0), (255, 255, 255, 255), (80, 80, 160, 255), (20, 20, 60, 255)]


class PixelScaleTests(unittest.TestCase):
    def test_scale2x_rounds_a_diagonal(self):
        # A 2x2 checker corner: the top-left output of the blank pixel takes the diagonal colour.
        w, h, out = SCALE.scale2x(2, 2, [1, 0, 0, 1])
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(set(out), {0, 1})

    def test_only_source_indices_survive(self):
        image = Image.new("L", (6, 5))
        image.putdata([(x * 7 + y * 3) % 4 for y in range(5) for x in range(6)])
        for method in ("mmpx", "scale2x"):
            result = SCALE.magnify(image, PALETTE, 4, method)
            self.assertEqual(result.size, (24, 20))
            self.assertLessEqual(set(result.getdata()), set(image.getdata()))

    def test_flat_area_is_unchanged(self):
        image = Image.new("L", (4, 4), 2)
        self.assertEqual(set(SCALE.magnify(image, PALETTE, 2).getdata()), {2})

    def test_rejects_bad_input(self):
        with self.assertRaises(ValueError):
            SCALE.magnify(Image.new("RGB", (2, 2)), PALETTE, 2)
        with self.assertRaises(ValueError):
            SCALE.magnify(Image.new("L", (2, 2)), PALETTE, 3)


if __name__ == "__main__":
    unittest.main()
