import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"tools/release/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CompressHdTests(unittest.TestCase):
    def art(self, root: Path) -> Path:
        art = root / "art"
        (art / "portraits").mkdir(parents=True)
        (art / "backgrounds").mkdir()
        face = Image.new("RGBA", (32, 32), (200, 150, 100, 0))
        face.paste((90, 60, 30, 255), (8, 8, 24, 24))
        face.putpixel((7, 7), (90, 60, 30, 128))
        face.save(art / "portraits/portrait-9.png")
        Image.new("RGB", (32, 24), (10, 80, 160)).save(art / "backgrounds/background-5470-5478.png")
        (art / "srw64-portraits-hd.json").write_text(json.dumps({
            "schema": "srw64.portrait-images.v1", "silhouette": {"palette": 609, "rgb": [41, 41, 41]},
            "images": [{"image": 9, "palette": 309, "file": "portraits/portrait-9.png", "sha256": "x"}]}))
        (art / "srw64-backgrounds-hd.json").write_text(json.dumps({
            "schema": "srw64.background-images.v1",
            "images": [{"image": 5470, "palette": 5478, "file": "backgrounds/background-5470-5478.png", "sha256": "x"}]}))
        return art

    def test_portraits_split_into_jpeg_and_a_silhouette_alpha(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = self.art(Path(tmp))
            self.assertEqual(compress.compress(art), {"backgrounds": 1, "portraits": 1, "jpeg_quality": 95})
            row = json.loads((art / "srw64-portraits-hd.json").read_text())["images"][0]
            self.assertEqual((row["file"], row["alpha"]), ("portraits/portrait-9.jpg", "portraits/portrait-9.alpha.png"))
            self.assertEqual(row["sha256"], compress.sha(art / row["file"]))
            self.assertFalse((art / "portraits/portrait-9.png").exists())
            alpha = Image.open(art / row["alpha"])
            self.assertEqual(alpha.mode, "LA")
            # The alpha is exact, and read as RGBA the file is the silhouette portrait.
            # (JPEG rings near the painted edge; the block 8..15 lies inside the square.)
            self.assertEqual(alpha.convert("RGBA").getpixel((12, 12)), (41, 41, 41, 255))
            self.assertEqual(alpha.getpixel((7, 7))[1], 128)
            self.assertEqual(alpha.getpixel((0, 0))[1], 0)
            colour = Image.open(art / row["file"])
            self.assertEqual(colour.format, "JPEG")
            self.assertTrue(all(abs(a - b) <= 3 for a, b in zip(colour.getpixel((12, 12)), (90, 60, 30))))
            background = json.loads((art / "srw64-backgrounds-hd.json").read_text())["images"][0]
            self.assertEqual(background["file"], "backgrounds/background-5470-5478.jpg")
            self.assertEqual(Image.open(art / background["file"]).format, "JPEG")

    def test_page_portraits_use_the_alpha_file_as_silhouette(self):
        sys.path.insert(0, str(ROOT / "src"))
        bundle = load("prepare_hd_bundle")
        with tempfile.TemporaryDirectory() as tmp:
            art = self.art(Path(tmp))
            load("compress_hd").compress(art)
            self.assertEqual(bundle.page_portraits(art), {"9:309": "portraits/portrait-9.jpg", "9:609": "portraits/portrait-9.alpha.png"})

    def test_a_translucent_background_is_refused(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = self.art(Path(tmp))
            Image.new("RGBA", (32, 24), (10, 80, 160, 200)).save(art / "backgrounds/background-5470-5478.png")
            with self.assertRaisesRegex(ValueError, "not opaque"):
                compress.compress_backgrounds(art)


if __name__ == "__main__":
    unittest.main()
