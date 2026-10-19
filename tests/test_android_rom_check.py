"""The Android launcher's ROM check (RomCheck.java) without Android (requires a JDK)."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/release/android/app/java/org/srw64/game/RomCheck.java"


class AndroidRomIdentityTests(unittest.TestCase):
    def test_constants_match_the_baseline(self):
        rom = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text(encoding="utf-8"))["rom"]
        text = SOURCE.read_text(encoding="utf-8")
        self.assertEqual(re.search(r'SHA256 = "([0-9a-f]{64})"', text).group(1), rom["sha256"])
        self.assertEqual(int(re.search(r"SIZE = (\d+)L", text).group(1)), rom["size"])


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK unavailable")
class AndroidRomCheckTests(unittest.TestCase):
    def test_orders_and_refusals(self):
        rom = next((p for p in (ROOT / "assets/rom.z64", ROOT / "rom.z64") if p.is_file()), None)
        with tempfile.TemporaryDirectory(prefix="srw64-android-rom-") as directory:
            root = Path(directory)
            classes = root / "classes"
            classes.mkdir()
            subprocess.run(["javac", "-d", str(classes), str(SOURCE), str(ROOT / "tests/android/RomCheckTest.java")],
                           check=True, capture_output=True)
            arguments = [str(root)] + ([str(rom)] if rom else [])
            result = subprocess.run(["java", "-Xmx512m", "-cp", str(classes), "org.srw64.game.RomCheckTest", *arguments],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
