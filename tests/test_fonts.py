"""Packaged fonts: the manifest, the repository copies, and preparation."""
import importlib.util
import json
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_fonts", ROOT / "tools/content/prepare_fonts.py")
FONTS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(FONTS)


class FontPackageTests(unittest.TestCase):
    def test_manifest_names_the_official_archive_and_every_file(self):
        manifest = json.loads(FONTS.MANIFEST.read_text())
        self.assertEqual(manifest["schema"], "srw64.font-package.v1")
        self.assertTrue(manifest["url"].startswith("https://developer.huawei.com/"))
        # Variable fonts: Regular for text, the Bold instance for the title menu and chapter cards.
        self.assertEqual(manifest["version"], "2.040")
        self.assertEqual(set(manifest["files"]), {"HarmonyOS_Sans_SC.ttf", "HarmonyOS_Sans_Condensed.ttf",
                                                  "LICENSE-HarmonyOS-Sans.txt"})
        for row in manifest["files"].values():
            self.assertRegex(row["sha256"], r"^[0-9a-f]{64}$")
        # The licence allows redistributing them unmodified with the software: the
        # repository carries the official files, byte for byte.
        for name, row in manifest["files"].items():
            self.assertEqual(FONTS.sha256((ROOT / "content/fonts" / name).read_bytes()), row["sha256"], name)

    def test_missing_archive_says_where_to_get_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as caught:
                FONTS.prepare(Path(tmp) / "missing.zip", Path(tmp) / "out", Path(tmp) / "no-copies")
            self.assertIn("developer.huawei.com", str(caught.exception))
            self.assertFalse(FONTS.prepared(Path(tmp) / "out"))

    def test_prepare_takes_the_repository_copies(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = FONTS.prepare(Path(tmp) / "missing.zip", Path(tmp) / "fonts")
            self.assertTrue(FONTS.prepared(out))

    @unittest.skipUnless(FONTS.ARCHIVE.is_file() and (ROOT / "content/fonts/SRW64Symbols.ttf").is_file(),
                         "Official HarmonyOS Sans archive required")
    def test_prepare_extracts_checked_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = FONTS.prepare(FONTS.ARCHIVE, Path(tmp) / "fonts", Path(tmp) / "no-copies")
            self.assertTrue(FONTS.prepared(out))
            (out / "HarmonyOS_Sans_SC.ttf").write_bytes(b"tampered")
            self.assertFalse(FONTS.prepared(out))
            # A file of an older package counts as unprepared and is removed on the next run.
            (out / "HarmonyOS_Sans_SC_Regular.ttf").write_bytes(b"1.0")
            FONTS.prepare(FONTS.ARCHIVE, out, Path(tmp) / "no-copies")
            self.assertFalse((out / "HarmonyOS_Sans_SC_Regular.ttf").exists())
            self.assertTrue(FONTS.prepared(out))


if __name__ == "__main__":
    unittest.main()
