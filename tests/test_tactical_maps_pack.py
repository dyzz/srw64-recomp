"""The HD tactical map pack the art manifest points at (src/srw64_native/assets.py)."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.assets import tactical_maps  # noqa: E402
import importlib.util  # noqa: E402
from PIL import Image  # noqa: E402

_spec = importlib.util.spec_from_file_location("compress_hd", ROOT / "tools/release/compress_hd.py")
compress_hd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(compress_hd)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class TacticalMapPackTests(unittest.TestCase):
    def make(self, root: Path) -> dict:
        pack = root / "pack"
        (pack / "map-020").mkdir(parents=True)
        (pack / "colony/frame-00").mkdir(parents=True)
        files = {}
        for folder in ("map-020", "colony/frame-00"):
            files[folder] = {}
            for name in ("base.png", "index.png", "meta.json"):
                data = f"{folder}/{name}".encode()
                (pack / folder / name).write_bytes(data)
                files[folder][name] = sha(data)
        index = {"schema": "srw64.tactical-maps.v1",
                 "maps": [{"map": 20, "layout": 6284, "folder": "map-020", "files": files["map-020"]}],
                 "colony_frames": [{"folder": "colony/frame-00", "files": files["colony/frame-00"]}]}
        data = json.dumps(index).encode()
        (pack / "tactical-maps.json").write_bytes(data)
        return {"path": "pack", "manifest_sha256": sha(data)}

    def test_checked_pack_lists_every_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder, index, files = tactical_maps(root, self.make(root))
            self.assertEqual(folder, (root / "pack").resolve())
            self.assertEqual(len(index["maps"]), 1)
            self.assertEqual(sorted(name for _, name in files)[:1], ["colony/frame-00/base.png"])
            self.assertEqual(len(files), 6)

    def test_changed_pixels_or_index_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = self.make(root)
            (root / "pack/map-020/base.png").write_bytes(b"repainted")
            with self.assertRaisesRegex(ValueError, "pixels changed"):
                tactical_maps(root, spec)
            with self.assertRaisesRegex(ValueError, "manifest changed"):
                tactical_maps(root, {**spec, "manifest_sha256": "0" * 64})


class TacticalMapCompressTests(unittest.TestCase):
    def test_bases_become_jpeg_and_the_index_follows(self):
        with tempfile.TemporaryDirectory() as temp:
            art = Path(temp)
            folder = art / "maps/map-020"
            folder.mkdir(parents=True)
            Image.new("RGB", (64, 64), (40, 60, 30)).save(folder / "base.png")
            Image.new("L", (64, 64), 7).save(folder / "index.png")
            (folder / "meta.json").write_text(json.dumps({"map": 20, "layout": 6284}))
            files = {name: sha((folder / name).read_bytes()) for name in ("base.png", "index.png", "meta.json")}
            (art / "maps/tactical-maps.json").write_text(json.dumps({"schema": "srw64.tactical-maps.v1", "colony_frames": [],
                "maps": [{"map": 20, "layout": 6284, "folder": "map-020", "files": files}]}))
            (art / "srw64-tactical-maps.json").write_text(json.dumps({"schema": "srw64.tactical-maps-runtime.v1", "root": "maps"}))
            self.assertEqual(compress_hd.compress_tactical_maps(art), 1)
            self.assertFalse((folder / "base.png").exists())
            self.assertEqual(Image.open(folder / "base.jpg").format, "JPEG")
            self.assertEqual(json.loads((folder / "meta.json").read_text())["base"], "base.jpg")
            row = json.loads((art / "maps/tactical-maps.json").read_text())["maps"][0]
            self.assertEqual(sorted(row["files"]), ["base.jpg", "index.png", "meta.json"])
            self.assertEqual(row["files"]["index.png"], files["index.png"])
            self.assertEqual(row["files"]["base.jpg"], sha((folder / "base.jpg").read_bytes()))


if __name__ == "__main__":
    unittest.main()
