import copy
import json
from pathlib import Path
import tempfile
import unittest

from srw64_native.assets import compile_art
from srw64_native.catalog import compile_locale, sha, signature, text_key
from srw64_native.profile import load_profile

ROOT = Path(__file__).resolve().parents[1]


class NativeCatalogTests(unittest.TestCase):
    def setUp(self):
        self.key = text_key(0, 42)
        self.source = {self.key: "名<G:0124><G:0124><STOP>次<END>"}
        self.hashes = {self.key: "a" * 64}
        self.locale = {"schema": "srw64.locale.v1", "locale": "en", "source_locale": "ja", "font": "Helvetica",
                       "entries": [{"key": self.key, "source_sha256": "a" * 64,
                                    "target": "Name <G:0124><G:0124><STOP>" + "long text " * 500 + "<BR>End<END>"}]}

    def test_text_identity_includes_table_and_rejects_truncation(self):
        self.assertNotEqual(text_key(0, 42), text_key(1, 42))
        for table, index in ((-1, 1), (100, 1), (0, 65536), (False, 1)):
            with self.assertRaises(ValueError):
                text_key(table, index)

    def test_native_length_and_linebreaks_are_free(self):
        self.assertEqual(len(compile_locale(self.locale, self.source, self.hashes)), 1)

    def test_barriers_and_parameters_cannot_change_segments(self):
        for target in ("<G:0124><STOP>x<END>", "<STOP><G:0124><G:0124><END>",
                       "<G:0124><G:0124><END>", "<G:0124><G:0124><STOP><STOP><END>"):
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, "barriers or parameters"):
                doc = copy.deepcopy(self.locale)
                doc["entries"][0]["target"] = target
                compile_locale(doc, self.source, self.hashes)

    def test_stale_unknown_and_duplicate_translation_fail(self):
        for field, value in (("source_sha256", "b" * 64), ("key", "base:t01_00042")):
            doc = copy.deepcopy(self.locale)
            doc["entries"][0][field] = value
            with self.assertRaises(ValueError):
                compile_locale(doc, self.source, self.hashes)
        self.locale["entries"] *= 2
        with self.assertRaisesRegex(ValueError, "duplicate"):
            compile_locale(self.locale, self.source, self.hashes)

    def test_malformed_controls_are_rejected(self):
        for text in ("<STOP><END>x", "<BAD><END>", "<END><END>", "\f<END>", "\x00<END>"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                signature(text)

    def test_profile_axes_are_independent(self):
        path = ROOT / "config/recomp/play-profile.json"
        for locale in ("ja", "zh-Hans"):
            for images in ("original", "hd"):
                profile = load_profile(path, locale=locale, images=images)
                self.assertEqual(profile["baseline"], "srw64-jp-rev0")
                self.assertEqual(profile["presentation"]["model_5600"], "waterdrop")
                self.assertEqual(profile["presentation"]["images"], images)
                self.assertEqual(profile["presentation"]["locale"], locale)
        with self.assertRaisesRegex(ValueError, "not registered"):
            load_profile(path, locale="fr")


class NativeArtTests(unittest.TestCase):
    def test_pack_allowlist_excludes_fonts_and_checks_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pack = root / "input"
            pack.mkdir()
            (pack / "art.png").write_bytes(b"reviewed art")
            (pack / "font.png").write_bytes(b"Chinese font")
            db = {"configuration": {}, "textures": [
                {"hashes": {"rt64": "a" * 16}, "path": "art.png"},
                {"hashes": {"rt64": "b" * 16}, "path": "font.png"}]}
            (pack / "rt64.json").write_text(json.dumps(db))
            manifest = {"schema": "srw64.art-pack.v1", "locale": "neutral",
                        "source": {"path": "input", "manifest_sha256": sha((pack / "rt64.json").read_bytes())},
                        "textures": [{"hash": "a" * 16, "kind": "portrait", "sha256": sha(b"reviewed art")}]}
            result = compile_art(root, manifest, root / "out")
            self.assertEqual(result["count"], 1)
            self.assertFalse((root / "out/font.png").exists())
            (pack / "art.png").write_bytes(b"changed art")
            with self.assertRaisesRegex(ValueError, "pixels changed"):
                compile_art(root, manifest, root / "failed")
            self.assertFalse((root / "failed").exists())
            manifest["locale"] = "zh-Hans"
            with self.assertRaisesRegex(ValueError, "language-neutral"):
                compile_art(root, manifest, root / "failed")
