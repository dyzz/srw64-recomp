"""tools/mods/validate.py: the host's manifest and archive rules (the cases shared with
tests/native_mod_library.cpp) and the submission rules of docs/design/mod-packages.md §13.4."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load():
    spec = importlib.util.spec_from_file_location("validate", ROOT / "tools/mods/validate.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["validate"] = module   # dataclasses resolve the module's annotations through sys.modules
    spec.loader.exec_module(module)
    return module


validate = load()


def manifest(id="a.b", version="1.0.0", **extra):
    m = {"schema": "srw64.mod.v1", "id": id, "version": version, "name": {"en": id}, "license": "CC-BY-NC-4.0"}
    m.update(extra)
    return m


class ManifestFixtureTests(unittest.TestCase):
    """The cases both the host and this tool must agree on."""

    def test_shared_cases(self):
        cases = json.loads((ROOT / "tests/mods/manifests.json").read_text(encoding="utf-8"))["cases"]
        self.assertGreaterEqual(len(cases), 10)
        for case in cases:
            m, problem = validate.parse_manifest(case["manifest"])
            self.assertEqual(problem.code if problem else "", case["problem"], case["case"])
            self.assertEqual(m is None, bool(case["problem"]), case["case"])

    def test_versions_and_requirements(self):
        self.assertEqual(validate.parse_version("1.2.3"), (1, 2, 3))
        self.assertEqual(validate.parse_version("2"), (2, 0, 0))
        for bad in ("1.x", "", "1.2.3.4", "1234567", "-1", "1."):
            self.assertIsNone(validate.parse_version(bad), bad)
        self.assertTrue(validate.satisfies((1, 2, 0), ">=1.0 <2"))
        self.assertFalse(validate.satisfies((2, 0, 0), ">=1.0 <2"))
        self.assertTrue(validate.satisfies((0, 1, 0), "*"))
        self.assertTrue(validate.valid_requirement("=1.2.3"))
        for bad in ("~1.0", "", ">= 1"):
            self.assertFalse(validate.valid_requirement(bad), bad)

    def test_safe_entries(self):
        self.assertEqual(validate.safe_entry("audio/music/7.ogg"), "audio/music/7.ogg")
        self.assertEqual(validate.safe_entry("./mod.json"), "mod.json")
        for bad in ("../evil", "/etc/x", "a/../../b", "C:/x", "a\\..\\b", ""):
            self.assertIsNone(validate.safe_entry(bad), bad)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def zip(self, name, files, prefix=""):
        path = self.dir / name
        with zipfile.ZipFile(path, "w") as z:
            for entry, data in files.items():
                z.writestr(prefix + entry, data if isinstance(data, bytes) else data.encode("utf-8"))
        return path

    def good_files(self, **extra):
        return {"mod.json": json.dumps(manifest(**extra)), "audio/music/7.ogg": b"OggS", "art/portraits/x.png": b"\x89PNG"}

    def codes(self, path, **kw):
        problems, summary = validate.check(path, **kw)
        return [p.code for p in problems], summary

    def test_a_good_package_passes_with_its_summary(self):
        path = self.zip("a.b-1.0.0.srw64mod", self.good_files())
        codes, summary = self.codes(path)
        self.assertEqual(codes, [])
        self.assertEqual((summary["id"], summary["version"], summary["license"]), ("a.b", "1.0.0", "CC-BY-NC-4.0"))
        self.assertEqual(summary["contents"], ["art", "audio"])
        self.assertEqual(summary["file"], "a.b-1.0.0.srw64mod")
        self.assertEqual(summary["sha256"], validate.sha256_of(path))

    def test_names_file_follows_the_host(self):
        files = self.good_files()
        files["dialogue/names.json"] = json.dumps({"schema": "srw64.mod-names.v1", "unit": {"zh-Hans": "疾风"},
            "heroes": {"super-male": {"hero": {"zh-Hans": {"name": "龙", "surname": "天野"}}}}}).encode()
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], [])
        for bad, why in [({"unit": {"fr": "Gale"}}, "未知语言"), ({"unit": {"en": "x" * 25}}, "最多 24"),
                         ({"heroes": {"super-male": {"hero": {"en": {"surname": "A"}}}}}, "缺少 name"),
                         ({"heroes": {"super-male": {"hero": {"en": {"name": "{HeroNick}"}}}}}, "< > { }")]:
            self.assertIn(why, validate.names_problem(bad))
        files["dialogue/names.json"] = b"{"
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], ["names"])

    def test_everything_inside_one_top_folder_and_junk(self):
        files = {"a.b/" + name: data for name, data in self.good_files().items()}
        files["__MACOSX/a.b/._mod.json"] = b"junk"
        files["a.b/audio/.DS_Store"] = b"junk"
        path = self.zip("a.b-1.0.0.srw64mod", files)
        self.assertEqual(self.codes(path)[0], [])

    def test_a_folder_package(self):
        folder = self.dir / "a.b"
        (folder / "audio" / "music").mkdir(parents=True)
        (folder / "mod.json").write_text(json.dumps(manifest()), encoding="utf-8")
        (folder / "audio" / "music" / "7.ogg").write_bytes(b"OggS")
        codes, summary = self.codes(folder)
        self.assertEqual(codes, [])
        self.assertNotIn("sha256", summary)

    def test_the_hosts_refusals_come_first(self):
        self.assertEqual(self.codes(self.zip("x.srw64mod", {"audio/music/7.ogg": b""}))[0], ["no_manifest"])
        self.assertEqual(self.codes(self.zip("x.srw64mod", {"mod.json": "{", "a.txt": ""}))[0], ["manifest"])
        self.assertEqual(self.codes(self.zip("x.srw64mod", {"mod.json": json.dumps(manifest(colour="red"))}))[0], ["field"])
        self.assertEqual(self.codes(self.zip("x.srw64mod", {"mod.json": json.dumps(manifest()), "../evil.txt": ""}))[0], ["archive_path"])
        (self.dir / "not.srw64mod").write_bytes(b"not a zip")
        self.assertEqual(self.codes(self.dir / "not.srw64mod")[0], ["archive"])
        self.assertEqual(self.codes(self.zip("x.srw64mod", {"mod.json": json.dumps(manifest(license="Mine"))}))[0], ["license"])

    def test_host_only_stops_at_the_hosts_rules(self):
        m = manifest(); del m["license"]
        path = self.zip("wrong-name.zip", {"mod.json": json.dumps(m), "readme.pdf": b""})
        self.assertEqual(self.codes(path, host_only=True)[0], [])
        self.assertEqual(sorted(self.codes(path)[0]), ["empty", "filename", "layout", "license_missing"])

    def test_submission_rules(self):
        m = manifest(); del m["license"]
        files = self.good_files()
        files["mod.json"] = json.dumps(m)
        files["readme.pdf"] = b""
        files["extras/x.txt"] = b""
        files["art/thing.exe"] = b""
        codes, _ = self.codes(self.zip("a.b-1.0.0.srw64mod", files))
        self.assertEqual(sorted(codes), ["file_type", "layout", "layout", "license_missing"])
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files), require_license=True)[0].count("license_missing"), 1)
        self.assertNotIn("license_missing", self.codes(self.zip("a.b-1.0.0.srw64mod", files), require_license=False)[0])
        self.assertEqual(self.codes(self.zip("a.b-1.0.srw64mod", self.good_files()))[0], ["filename"])
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", self.good_files()), max_bytes=10)[0], ["too_large"])

    def test_original_data_is_refused(self):
        path = self.zip("a.b-1.0.0.srw64mod", self.good_files())
        import hashlib
        hashes = {hashlib.sha256(b"\x89PNG").hexdigest()}
        self.assertEqual(self.codes(path, original_hashes=hashes)[0], ["original"])
        (self.dir / "hashes.txt").write_text("# exported originals\n" + hashlib.sha256(b"OggS").hexdigest() + "  art/portraits/x.png\n", encoding="utf-8")
        self.assertEqual(self.codes(path, original_hashes=validate.load_original_hashes(self.dir / "hashes.txt"))[0], ["original"])

    def test_against_the_index_and_owners(self):
        index = {"schema": "srw64.mods-index.v1", "packages": [{"id": "a.b", "version": "1.0.0"}, {"id": "c.d", "version": "2.0.0"}]}
        same = self.zip("a.b-1.0.0.srw64mod", self.good_files())
        self.assertEqual(self.codes(same, index=index)[0], ["not_newer"])
        newer = self.zip("a.b-1.1.0.srw64mod", self.good_files(version="1.1.0", requires={"c.d": ">=2", "e.f": "*"}))
        self.assertEqual(self.codes(newer, index=index)[0], ["missing"])
        self.assertEqual(self.codes(newer, index=index, also_listed=("e.f",))[0], [])
        owners = {"a": "github:someone"}
        self.assertEqual(self.codes(newer, owners=owners, owner="github:someone")[0], [])
        self.assertEqual(self.codes(newer, owners=owners, owner="github:other")[0], ["owner"])
        self.assertEqual(self.codes(newer, owners={}, owner="github:someone")[0], ["no_owner"])

    def test_a_campaign_image_must_be_the_packages(self):
        files = {"mod.json": json.dumps(manifest()), "campaign/campaign.json": json.dumps({"schema": "srw64.campaign-image.v1", "id": "a.b", "stages": []})}
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], [])
        files["campaign/campaign.json"] = json.dumps({"schema": "srw64.campaign-image.v1", "id": "c.d", "stages": []})
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], ["campaign"])
        files["campaign/campaign.json"] = json.dumps({"schema": "srw64.campaign.v1", "id": "a.b"})
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], ["campaign"])
        # A campaign folder without its image is not a campaign (and the package is empty).
        files = {"mod.json": json.dumps(manifest()), "campaign/readme.txt": "x"}
        self.assertEqual(self.codes(self.zip("a.b-1.0.0.srw64mod", files))[0], ["empty"])

    def test_wording_uses_the_games_text(self):
        labels = json.loads((ROOT / "content/locales/zh-Hans.json").read_text(encoding="utf-8"))
        self.assertEqual(validate.wording(validate.Problem("field", "colour"), labels), "mod.json 有误（colour）")
        self.assertIn("mod.json", validate.wording(validate.Problem("no_manifest"), labels))
        self.assertIn("newer", validate.wording(validate.Problem("not_newer", "1 <= 1"), labels))
        self.assertIn("colour", validate.wording(validate.Problem("field", "colour"), None))

    def test_the_command(self):
        path = self.zip("a.b-1.0.0.srw64mod", self.good_files())
        self.assertEqual(validate.main([str(path), "--json"]), 0)
        self.assertEqual(validate.main([str(self.zip("a.b-1.0.srw64mod", self.good_files()))]), 1)


if __name__ == "__main__":
    unittest.main()
