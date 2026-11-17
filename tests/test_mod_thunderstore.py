"""tools/mods/thunderstore.py: a .srw64mod wrapped as Thunderstore's upload zip, the package
inside left as made (the game installs it from such a zip: tests/native_mod_library.cpp)."""
import importlib.util
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"tools/mods/{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


validate = load("validate")
thunderstore = load("thunderstore")
PACKAGE = ROOT / "web/public/mods/srw64.funnels-p-1.0.0.srw64mod"


def package_with(manifest: dict, path: Path) -> Path:
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("mod.json", json.dumps(manifest))
        z.writestr("audio/music/3.ogg", b"OggS" + bytes(60))
    return path


class ThunderstoreTests(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.icon = self.dir / "icon.png"
        Image.new("RGBA", (512, 512), (200, 40, 40, 255)).save(self.icon)

    def tearDown(self):
        self.tmp.cleanup()

    def test_wraps_the_package_as_made(self):
        path, ts = thunderstore.build(PACKAGE, self.icon, self.dir / "out", team="Marchwind64")
        self.assertEqual(path.name, "Marchwind64-funnels_p-1.0.0.zip")
        with zipfile.ZipFile(path) as z:
            self.assertEqual(z.namelist(), ["manifest.json", "icon.png", "README.md", PACKAGE.name])
            self.assertEqual(hashlib.sha256(z.read(PACKAGE.name)).hexdigest(), hashlib.sha256(PACKAGE.read_bytes()).hexdigest())
            manifest = json.loads(z.read("manifest.json"))
            from PIL import Image
            self.assertEqual(Image.open(io.BytesIO(z.read("icon.png"))).size, (256, 256))
            readme = z.read("README.md").decode("utf-8")
        self.assertEqual(manifest, ts)
        self.assertEqual(set(manifest), {"name", "version_number", "website_url", "description", "dependencies"})
        self.assertEqual((manifest["name"], manifest["version_number"], manifest["website_url"], manifest["dependencies"]),
                         ("funnels_p", "1.0.0", "", []))
        self.assertLessEqual(len(manifest["description"]), 250)
        self.assertTrue(manifest["description"].startswith("Makes the funnel-type weapons"))
        self.assertIn("Manual Download", readme)
        self.assertIn("简体中文", readme)
        self.assertIn("CC0-1.0", readme)

    def test_the_same_package_makes_the_same_zip(self):
        a, _ = thunderstore.build(PACKAGE, self.icon, self.dir / "a")
        b, _ = thunderstore.build(PACKAGE, self.icon, self.dir / "b")
        self.assertEqual(a.name, "funnels_p-1.0.0.zip")
        self.assertEqual(a.read_bytes(), b.read_bytes())

    def test_requirements_need_their_thunderstore_names(self):
        manifest = {"schema": "srw64.mod.v1", "id": "test.top", "version": "1.2", "name": {"en": "Top"}, "license": "MIT",
                    "requires": {"test.base": ">=1.0", "srw64.hd": ">=1.0"}}
        package = package_with(manifest, self.dir / "test.top-1.2.0.srw64mod")
        with self.assertRaisesRegex(ValueError, "no --dep for test.base"):
            thunderstore.build(package, self.icon, self.dir / "out")
        with self.assertRaisesRegex(ValueError, "not a Thunderstore package string"):
            thunderstore.build(package, self.icon, self.dir / "out", deps={"test.base": "test.base>=1.0"})
        _, ts = thunderstore.build(package, self.icon, self.dir / "out", deps={"test.base": "Team-Base-1.0.0"})
        self.assertEqual(ts["dependencies"], ["Team-Base-1.0.0"])   # the HD pack left to the game
        self.assertEqual(ts["version_number"], "1.2.0")

    def test_refusals(self):
        from PIL import Image
        wide = self.dir / "wide.png"
        Image.new("RGBA", (300, 200)).save(wide)
        with self.assertRaisesRegex(ValueError, "must be square"):
            thunderstore.build(PACKAGE, wide, self.dir / "out")
        with self.assertRaisesRegex(ValueError, "letters, digits and _"):
            thunderstore.build(PACKAGE, self.icon, self.dir / "out", name="funnels-p")
        unlicensed = package_with({"schema": "srw64.mod.v1", "id": "test.x", "version": "1.0.0", "name": "X"}, self.dir / "test.x-1.0.0.srw64mod")
        with self.assertRaises(ValueError):
            thunderstore.build(unlicensed, self.icon, self.dir / "out")

    def test_a_long_description_is_cut(self):
        manifest = {"description": {"en": "word " * 100}, "name": "N"}
        text = thunderstore.short_description(manifest)
        self.assertEqual(len(text), 250)
        self.assertTrue(text.endswith("…"))


if __name__ == "__main__":
    unittest.main()
