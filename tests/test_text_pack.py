"""Text packs (tools/release/build_text_pack.py): what goes in, and the same bytes every time."""
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_text_pack", ROOT / "tools/release/build_text_pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

PROFILE = {"locales": {"ja": "content/locales/ja.json", "zh-Hans": "content/locales/zh-Hans.json"}}


def locale(entries, ui):
    return {"entries": [{"key": k, "target": v} for k, v in entries.items()], "ui": ui}


TREES = {
    "v0.5.3": {
        pack.PROFILE: PROFILE,
        "content/locales/ja.json": locale({}, {"title": "タイトル", "back": "戻る"}),
        "content/locales/zh-Hans.json": locale({"base:t00_00001": "旧<END>", "base:t00_00002": "同<END>"},
                                               {"title": "标题"}),
        "content/dialogue/zh-Hans/story/scene-0001.txt": "old",
        "content/dialogue/zh-Hans/story/scene-0002.txt": "same",
        pack.LIBRARY: {"units": {}},
    },
    "HEAD": {
        pack.PROFILE: {"locales": {**PROFILE["locales"], "ko": "content/locales/ko.json"}},
        "content/locales/ja.json": locale({}, {"title": "タイトル", "back": "戻る", "new_feature": "新機能"}),
        "content/locales/zh-Hans.json": locale({"base:t00_00001": "新<END>", "base:t00_00002": "同<END>", "base:t00_00003": "加<END>"},
                                               {"title": "标题", "back": "返回", "new_feature": "新功能"}),
        "content/dialogue/zh-Hans/story/scene-0001.txt": "new",
        "content/dialogue/zh-Hans/story/scene-0002.txt": "same",
        "content/dialogue/zh-Hans/story/scene-0003.txt": "added",
        "content/dialogue/ko/story/scene-0001.txt": "a locale the release lacks",
        pack.LIBRARY: {"units": {}},
    },
}


def fake_show(revision, path):
    value = TREES[revision][path]
    return value.encode() if isinstance(value, str) else json.dumps(value).encode()


def fake_blobs(revision, prefix):
    return {path: str(hash(fake_show(revision, path))) for path in TREES[revision] if path.startswith(prefix + "/")}


class ChangeTests(unittest.TestCase):
    def setUp(self):
        patches = [mock.patch.object(pack, "show", fake_show), mock.patch.object(pack, "blobs", fake_blobs)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        self.result = pack.changes("v0.5.3", "HEAD")

    def test_changed_and_new_dialogue_files_only(self):
        dialogue = sorted(name for name in self.result["files"] if name.startswith("dialogue/"))
        self.assertEqual(dialogue, ["dialogue/zh-Hans/story/scene-0001.txt", "dialogue/zh-Hans/story/scene-0003.txt"])

    def test_entries_and_only_the_release_labels(self):
        catalog = json.loads(self.result["files"]["locales/zh-Hans.json"])
        self.assertEqual(catalog["entries"], {"base:t00_00001": "新<END>", "base:t00_00003": "加<END>"})
        # "back" was the Japanese label in the release; "new_feature" is not in it.
        self.assertEqual(catalog["ui"], {"back": "返回"})
        self.assertNotIn("locales/ja.json", self.result["files"])

    def test_unchanged_library_names_stay_out(self):
        self.assertNotIn("library-names.json", self.result["files"])


class ZipTests(unittest.TestCase):
    def test_same_bytes_whatever_the_order(self):
        files = {"text.json": b"{}", "dialogue/en/a.txt": b"a"}
        self.assertEqual(pack.zipped(files), pack.zipped(dict(reversed(list(files.items())))))
        names = zipfile.ZipFile(io.BytesIO(pack.zipped(files))).namelist()
        self.assertEqual(names, ["dialogue/en/a.txt", "text.json"])

    def test_versions_sort_as_numbers(self):
        self.assertEqual(sorted(["0.5.10", "0.5.9", "0.6.0"], key=pack.version_key), ["0.5.9", "0.5.10", "0.6.0"])


if __name__ == "__main__":
    unittest.main()
