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


class UnitPoseIndexTests(unittest.TestCase):
    """compile_art copies the whole HD unit poses and unit_lookup finds them by triplet."""

    def test_units_section_is_copied_and_indexed(self):
        sys.path.insert(0, str(ROOT / "src"))
        from srw64_native.assets import compile_art, unit_lookup
        from srw64_native.catalog import sha
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "pack"
            source.mkdir()
            (source / "rt64.json").write_text(json.dumps({"configuration": {}, "textures": []}))
            poses = root / "poses"
            poses.mkdir()
            Image.new("RGBA", (16, 16), (1, 2, 3, 255)).save(poses / "unit-2216-1615-1911.png")
            index = {"schema": "srw64.unit-images.v1", "scale": 8, "recipe": "test",
                     "images": [{"scene": 2216, "atlas": 1615, "palette": 1911, "units": [3], "file": "unit-2216-1615-1911.png",
                                 "sha256": sha((poses / "unit-2216-1615-1911.png").read_bytes()), "width": 16, "height": 16}]}
            (poses / "units.json").write_text(json.dumps(index))
            manifest = {"schema": "srw64.art-pack.v1", "locale": "neutral", "textures": [],
                        "source": {"path": "pack", "manifest_sha256": sha((source / "rt64.json").read_bytes())},
                        "units": {"path": "poses", "manifest_sha256": sha((poses / "units.json").read_bytes())}}
            result = compile_art(root, manifest, root / "art")
            self.assertEqual(result["units"], 1)
            lookup = unit_lookup(root / "art")
            self.assertEqual(lookup(2216, 1615, 1911), str(root / "art" / "units/unit-2216-1615-1911.png"))
            self.assertIsNone(lookup(1, 2, 3))
            self.assertTrue((root / "art" / "units/unit-2216-1615-1911.png").exists())


class CompressHdTests(unittest.TestCase):
    def art(self, root: Path) -> Path:
        art = root / "art"
        (art / "portraits").mkdir(parents=True)
        (art / "backgrounds").mkdir()
        face = Image.new("RGBA", (64, 64), (200, 150, 100, 0))
        face.paste((90, 60, 30, 255), (16, 16, 48, 48))
        face.putpixel((15, 15), (90, 60, 30, 128))
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
            self.assertEqual(compress.compress(art), {"backgrounds": 1, "portraits": 1, "units": 0, "unit_extras": 0, "battle_sprites": 0, "tactical_maps": 0,
                                                      "rt64_resaved": 0, "jpeg_quality": 92, "jpeg_subsampling": "4:2:0"})
            row = json.loads((art / "srw64-portraits-hd.json").read_text())["images"][0]
            self.assertEqual((row["file"], row["alpha"]), ("portraits/portrait-9.jpg", "portraits/portrait-9.alpha.png"))
            self.assertEqual(row["sha256"], compress.sha(art / row["file"]))
            self.assertFalse((art / "portraits/portrait-9.png").exists())
            alpha = Image.open(art / row["alpha"])
            self.assertEqual(alpha.mode, "LA")
            # The alpha is exact, and read as RGBA the file is the silhouette portrait.
            # (JPEG rings near the painted edge and 4:2:0 shares colour over 16x16
            # blocks; the block 16..31 lies inside the square.)
            self.assertEqual(alpha.convert("RGBA").getpixel((24, 24)), (41, 41, 41, 255))
            self.assertEqual(alpha.getpixel((15, 15))[1], 128)
            self.assertEqual(alpha.getpixel((0, 0))[1], 0)
            colour = Image.open(art / row["file"])
            self.assertEqual(colour.format, "JPEG")
            self.assertTrue(all(abs(a - b) <= 3 for a, b in zip(colour.getpixel((24, 24)), (90, 60, 30))))
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

    def test_unit_poses_scale_to_six_times_and_split(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = Path(tmp)
            (art / "units").mkdir()
            pose = Image.new("RGBA", (32, 40), (120, 80, 40, 0))
            pose.paste((30, 60, 200, 255), (8, 8, 24, 32))
            pose.save(art / "units/unit-1-2-3.png")
            tall = Image.new("RGBA", (400, 1600), (120, 80, 40, 255))
            tall.save(art / "units/unit-4-5-6.png")
            (art / "srw64-units-hd.json").write_text(json.dumps({"schema": "srw64.unit-images.v1", "scale": 8, "images": [
                {"scene": 1, "atlas": 2, "palette": 3, "units": [0], "file": "units/unit-1-2-3.png", "sha256": "x", "width": 32, "height": 40},
                {"scene": 4, "atlas": 5, "palette": 6, "units": [1], "file": "units/unit-4-5-6.png", "sha256": "x", "width": 400, "height": 1600}]}))
            self.assertEqual(compress.compress_units(art), 2)
            index = json.loads((art / "srw64-units-hd.json").read_text())
            self.assertEqual((index["scale"], index["limit"]), (6, 1024))
            small, big = index["images"]
            self.assertEqual((small["file"], small["alpha"]), ("units/unit-1-2-3.jpg", "units/unit-1-2-3.alpha.png"))
            self.assertEqual((small["width"], small["height"]), (24, 30))  # 6/8 of the master
            self.assertEqual((big["width"], big["height"]), (256, 1024))  # the longer side capped first
            self.assertEqual(small["sha256"], compress.sha(art / small["file"]))
            self.assertFalse((art / "units/unit-1-2-3.png").exists())
            self.assertEqual(Image.open(art / small["file"]).format, "JPEG")
            alpha = Image.open(art / small["alpha"])
            self.assertEqual((alpha.mode, alpha.size), ("LA", (24, 30)))
            self.assertEqual(alpha.getpixel((0, 0))[1], 0)
            self.assertEqual(alpha.getpixel((12, 15))[1], 255)

    def test_unit_extra_frames_split_like_the_poses(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = Path(tmp)
            (art / "unit-extras").mkdir()
            Image.new("RGBA", (64, 48), (200, 30, 30, 255)).save(art / "unit-extras/2237-1636-2997-f0.png")
            (art / "srw64-unit-extras-hd.json").write_text(json.dumps({"schema": "srw64.unit-extra-images.v1", "scale": 8, "images": [
                {"scene": 2237, "atlas": 1636, "palette": 2997, "frame": 0, "file": "unit-extras/2237-1636-2997-f0.png",
                 "sha256": "x", "width": 64, "height": 48}]}))
            self.assertEqual(compress.compress_units(art, "srw64-unit-extras-hd.json"), 1)
            row = json.loads((art / "srw64-unit-extras-hd.json").read_text())["images"][0]
            self.assertEqual((row["file"], row["alpha"], row["frame"]), ("unit-extras/2237-1636-2997-f0.jpg", "unit-extras/2237-1636-2997-f0.alpha.png", 0))
            self.assertEqual((row["width"], row["height"]), (48, 36))

    def test_rt64_textures_lose_an_opaque_alpha_only(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = Path(tmp)
            opaque = Image.new("RGBA", (64, 64), (10, 20, 30, 255))
            opaque.putpixel((3, 3), (200, 100, 50, 255))
            opaque.save(art / "a.png", compress_level=0)
            clear = Image.new("RGBA", (64, 64), (10, 20, 30, 128))
            clear.save(art / "b.png", compress_level=0)
            (art / "rt64.json").write_text(json.dumps({"configuration": {}, "textures": [
                {"hashes": {"rt64": "a"}, "path": "a.png", "kind": "worldmap"},
                {"hashes": {"rt64": "b"}, "path": "b.png", "kind": "worldmap"}]}))
            self.assertEqual(compress.compress_rt64(art), 2)
            self.assertEqual(Image.open(art / "a.png").mode, "RGB")
            self.assertEqual(Image.open(art / "a.png").getpixel((3, 3)), (200, 100, 50))
            self.assertEqual(Image.open(art / "b.png").mode, "RGBA")
            self.assertEqual(Image.open(art / "b.png").getpixel((0, 0)), (10, 20, 30, 128))
            self.assertEqual(sorted(p.name for p in art.iterdir()), ["a.png", "b.png", "rt64.json"])

    def test_a_translucent_background_is_refused(self):
        compress = load("compress_hd")
        with tempfile.TemporaryDirectory() as tmp:
            art = self.art(Path(tmp))
            Image.new("RGBA", (32, 24), (10, 80, 160, 200)).save(art / "backgrounds/background-5470-5478.png")
            with self.assertRaisesRegex(ValueError, "not opaque"):
                compress.compress_backgrounds(art)


if __name__ == "__main__":
    unittest.main()
