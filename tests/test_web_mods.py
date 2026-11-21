"""web/scripts/export_mods.py: the MOD packages the website hosts (web/public/mods) and their
entries (web/src/data/mods) are what packing their sources gives now."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostedModsTests(unittest.TestCase):
    # tools/mods/pack.py now stores pictures and sound (validate.STORED), so packing the Final
    # Dancouga source gives other bytes than the hosted 0.2.0. The site's packages and pages stay
    # as they are until the next release (user, 2026-11-20): run export_mods.py then, and drop this.
    @unittest.skip("hosted packages are repacked at the next release")
    def test_hosted_packages_match_their_sources(self):
        result = subprocess.run([sys.executable, str(ROOT / "web/scripts/export_mods.py"), "--check"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
