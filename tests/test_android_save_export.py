"""Exercise the APK's ZIP writer without Android or a ROM (requires a JDK)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK unavailable")
class AndroidSaveExportTests(unittest.TestCase):
    def test_archive_preserves_snapshot_and_reports_failures(self):
        with tempfile.TemporaryDirectory(prefix="srw64-android-save-") as directory:
            root = Path(directory)
            classes, saves = root / "classes", root / "saves"
            classes.mkdir()
            saves.mkdir()
            subprocess.run(["javac", "-d", str(classes),
                            str(ROOT / "tools/release/android/app/java/org/srw64/game/SaveExport.java"),
                            str(ROOT / "tests/android/SaveExportTest.java")], check=True, capture_output=True)
            subprocess.run(["java", "-cp", str(classes), "org.srw64.game.SaveExportTest", str(saves)],
                           check=True, capture_output=True)
