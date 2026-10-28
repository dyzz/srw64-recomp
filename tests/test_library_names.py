"""Library names for single records: the host's header follows content/locales/library-names.json."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class LibraryNamesTest(unittest.TestCase):
    def test_names_are_well_formed(self):
        data = json.loads((ROOT / "content/locales/library-names.json").read_text(encoding="utf-8"))
        for kind in ("units", "pilots"):
            for rid, names in data[kind].items():
                self.assertTrue(rid.isdigit(), rid)
                for locale, name in names.items():
                    self.assertIn(locale, ("ja", "zh-Hans", "zh-Hant", "en"))
                    self.assertTrue(name and '"' not in name and "\\" not in name, name)

    def test_header_is_up_to_date(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/content/library_names.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
