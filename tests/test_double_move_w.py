"""The embedded 二回行動 W picture (tools/content/double_move_w.py) matches its PNG."""
import subprocess
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class DoubleMoveWTests(unittest.TestCase):
    def test_picture_fills_a_14_by_12_cell(self):
        image = Image.open(ROOT / "content/art/double-move-w.png")
        self.assertEqual((image.mode, image.size), ("RGBA", (224, 192)))
        # Its solid body spans the cell's width and sits centred in its height.
        solid = image.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
        left, top, right, bottom = solid.getbbox()
        self.assertLessEqual(left, 4)
        self.assertGreaterEqual(right, 220)
        self.assertLessEqual(abs((top + bottom) / 2 - 96), 6)
        self.assertGreaterEqual(bottom - top, 140)

    def test_header_is_up_to_date(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/content/double_move_w.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
