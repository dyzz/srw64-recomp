"""Packaged fonts: the manifest, and preparation from the official archive."""
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
        self.assertEqual(set(manifest["files"]), {"HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS_Sans_Condensed_Regular.ttf",
                                                  "LICENSE-HarmonyOS-Sans.txt"})
        for row in manifest["files"].values():
            self.assertRegex(row["sha256"], r"^[0-9a-f]{64}$")
        # The licence forbids distributing the fonts on their own: none in the repository.
        self.assertFalse(list((ROOT / "content/fonts").glob("HarmonyOS*")))

    def test_missing_archive_says_where_to_get_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as caught:
                FONTS.prepare(Path(tmp) / "missing.zip", Path(tmp) / "out")
            self.assertIn("developer.huawei.com", str(caught.exception))
            self.assertFalse(FONTS.prepared(Path(tmp) / "out"))

    @unittest.skipUnless(FONTS.ARCHIVE.is_file() and (ROOT / "content/fonts/SRW64Symbols.ttf").is_file(),
                         "Official HarmonyOS Sans archive required")
    def test_prepare_extracts_checked_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = FONTS.prepare(FONTS.ARCHIVE, Path(tmp) / "fonts")
            self.assertTrue(FONTS.prepared(out))
            (out / "HarmonyOS_Sans_SC_Regular.ttf").write_bytes(b"tampered")
            self.assertFalse(FONTS.prepared(out))


if __name__ == "__main__":
    unittest.main()
