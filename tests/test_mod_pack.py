"""tools/mods/pack.py: the sample campaign becomes a package folder and a .srw64mod that
validate.py accepts, with the compiled campaign image inside (docs/design/custom-campaign.md §3)."""
import importlib.util
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
pack = load("pack")


class PackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_sample_campaign_packs_and_passes(self):
        folder, path, manifest = pack.build_package(ROOT / "config/recomp/campaigns/sample", self.out)
        self.assertEqual(folder, self.out / "srw64.sample")
        self.assertEqual(path.name, "srw64.sample-1.0.0.srw64mod")
        self.assertEqual(sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()),
                         ["campaign/campaign.json", "mod.json"])
        image = json.loads((folder / "campaign" / "campaign.json").read_text(encoding="utf-8"))
        self.assertEqual((image["schema"], image["id"], len(image["stages"])), ("srw64.campaign-image.v1", "srw64.sample", 4))
        with zipfile.ZipFile(path) as z:
            self.assertEqual(z.namelist(), ["campaign/campaign.json", "mod.json"])
            self.assertTrue(all(i.date_time == pack.FIXED_DATE for i in z.infolist()))
        problems, summary = validate.check(path)
        self.assertEqual([p.code for p in problems], [])
        self.assertEqual((summary["id"], summary["version"], summary["license"], summary["contents"]), ("srw64.sample", "1.0.0", "CC-BY-NC-4.0", ["campaign"]))

    def test_the_file_is_reproducible(self):
        _, first, _ = pack.build_package(ROOT / "config/recomp/campaigns/retry", self.out)
        first_bytes = first.read_bytes()
        _, second, _ = pack.build_package(ROOT / "config/recomp/campaigns/retry", self.out)
        self.assertEqual(first_bytes, second.read_bytes())

    def test_a_mismatched_campaign_id_is_refused(self):
        source = self.out / "src"
        source.mkdir()
        for name in ("campaign.json", "stages"):
            target = source / name
            origin = ROOT / "config/recomp/campaigns/retry" / name
            if origin.is_dir():
                import shutil
                shutil.copytree(origin, target)
            else:
                target.write_text(origin.read_text(encoding="utf-8"), encoding="utf-8")
        (source / "mod.json").write_text(json.dumps({"schema": "srw64.mod.v1", "id": "x.other", "version": "1.0.0", "name": "x", "license": "MIT"}), encoding="utf-8")
        with self.assertRaises(ValueError):
            pack.build_package(source, self.out / "out")

    def test_sources_stay_out_and_content_folders_come_along(self):
        source = self.out / "src"
        (source / "audio" / "music").mkdir(parents=True)
        (source / "notes").mkdir()
        (source / "mod.json").write_text(json.dumps({"schema": "srw64.mod.v1", "id": "x.music", "version": "2.1.0", "name": "x", "license": "CC0-1.0"}), encoding="utf-8")
        (source / "audio" / "music" / "7.ogg").write_bytes(b"OggS")
        (source / "audio" / ".DS_Store").write_bytes(b"junk")
        (source / "notes" / "todo.txt").write_text("not shipped", encoding="utf-8")
        (source / "README.md").write_text("not shipped", encoding="utf-8")
        folder, path, _ = pack.build_package(source, self.out / "out")
        self.assertEqual(sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()), ["audio/music/7.ogg", "mod.json"])
        self.assertEqual(path.name, "x.music-2.1.0.srw64mod")
        self.assertEqual([p.code for p in validate.check(path)[0]], [])

    def test_the_command(self):
        self.assertEqual(pack.main([str(ROOT / "config/recomp/campaigns/sample"), "-o", str(self.out)]), 0)
        self.assertTrue((self.out / "srw64.sample-1.0.0.srw64mod").is_file())


if __name__ == "__main__":
    unittest.main()
