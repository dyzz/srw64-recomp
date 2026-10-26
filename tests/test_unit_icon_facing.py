"""The map icon facing table: the host's header follows content/art/unit-icon-facing.json
(docs/native/unit-icon-facing.md)."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class UnitIconFacingTest(unittest.TestCase):
    def test_every_icon_has_a_facing(self):
        icons = json.loads((ROOT / "content/art/unit-icon-facing.json").read_text(encoding="utf-8"))["icons"]
        self.assertEqual(len(icons), 320)
        for rid, entry in icons.items():
            self.assertTrue(688 <= int(rid) <= 1009, rid)
            self.assertIn(entry["facing"], ("left", "right", "front"), rid)

    def test_header_is_up_to_date(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/content/unit_icon_facing.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
