"""web/scripts/export_mods.py: the MOD packages the website hosts (web/public/mods) and their
entries (web/src/data/mods) are what packing their sources gives now."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostedModsTests(unittest.TestCase):
    def test_hosted_packages_match_their_sources(self):
        result = subprocess.run([sys.executable, str(ROOT / "web/scripts/export_mods.py"), "--check"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
