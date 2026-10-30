"""The text update list (tools/release/build_text_update.py): the whole text, by short hash."""
import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_text_update", ROOT / "tools/release/build_text_update.py")
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)

PROFILE = {"locales": {"ja": "content/locales/ja.json", "zh-Hans": "content/locales/zh-Hans.json"}}


def locale(entries, ui):
    return {"entries": [{"key": k, "target": v} for k, v in entries.items()], "ui": ui}


TREES = {
    "HEAD": {
        update.PROFILE: {"locales": {**PROFILE["locales"], "ko": "content/locales/ko.json"}},
        "content/locales/ja.json": locale({}, {"title": "タイトル", "back": "戻る"}),
        "content/locales/zh-Hans.json": locale({"base:t00_00001": "新<END>"}, {"title": "标题"}),
        "content/locales/ko.json": locale({}, {}),
        "content/dialogue/zh-Hans/story/scene-0001.txt": "new",
        "content/dialogue/zh-Hans/story/scene-0002.txt": "same",
        "content/dialogue/zh-Hans/notes.md": "not a dialogue file",
        "content/dialogue/fr/story/scene-0001.txt": "a locale the profile lacks",
        update.LIBRARY: {"units": {}},
    },
}


def fake_show(revision, path):
    value = TREES[revision][path]
    return value.encode() if isinstance(value, str) else json.dumps(value).encode()


def fake_blobs(revision, prefix):
    return {path: str(hash(fake_show(revision, path))) for path in TREES[revision] if path.startswith(prefix + "/")}


def fake_git(*args, **kwargs):
    return "v0.5.3\n" if args[0] == "describe" else "abc\n"


class ListTests(unittest.TestCase):
    def setUp(self):
        patches = [mock.patch.object(update, "show", fake_show), mock.patch.object(update, "blobs", fake_blobs),
                   mock.patch.object(update, "git", fake_git)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        data, self.stored = update.build("HEAD", 3, "2026-10-30", {})
        self.about = json.loads(data)

    def test_every_text_file_whatever_changed(self):
        self.assertEqual(sorted(self.about["files"]), [
            "dialogue/zh-Hans/story/scene-0001.txt", "dialogue/zh-Hans/story/scene-0002.txt",
            "library-names.json", "locales/ja.json", "locales/ko.json", "locales/zh-Hans.json"])

    def test_the_newest_release_the_commit_holds(self):
        self.assertEqual(self.about["release"], "0.5.3")
        self.assertEqual(self.about["schema"], update.SCHEMA)

    def test_labels_as_the_game_sees_them(self):
        row = self.about["files"]["locales/zh-Hans.json"]
        catalog = json.loads(zlib.decompress(self.stored[row["hash"]]))
        self.assertEqual(catalog["entries"], {"base:t00_00001": "新<END>"})
        self.assertEqual(catalog["ui"], {"back": "戻る", "title": "标题"})

    def test_files_by_short_hash_compressed(self):
        row = self.about["files"]["dialogue/zh-Hans/story/scene-0002.txt"]
        self.assertEqual(len(row["hash"]), update.HASH_DIGITS)
        self.assertEqual(zlib.decompress(self.stored[row["hash"]]), b"same")
        self.assertEqual(row["bytes"], 4)


class VersionTests(unittest.TestCase):
    def test_versions_sort_as_numbers(self):
        self.assertEqual(sorted(["0.5.10", "0.5.9", "0.6.0"], key=update.version_key), ["0.5.9", "0.5.10", "0.6.0"])


if __name__ == "__main__":
    unittest.main()
