"""The offline guide pages are generated from guide/data/<lang> and must stay in step with it."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_guide", ROOT / "tools/content/build_guide.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class GuideTests(unittest.TestCase):
    def test_pages_match_data(self):
        for path, page in MODULE.pages().items():
            with self.subTest(page=path.name):
                self.assertEqual(path.read_text(encoding="utf-8"), page,
                                 f"{path.name} is stale; run tools/content/build_guide.py")

    def test_links_and_translations_line_up(self):
        for lang in MODULE.languages():
            with self.subTest(lang=lang):
                self.assertEqual(MODULE.check_data(lang), [])

    def test_ids_are_unique(self):
        for lang in MODULE.languages():
            ids = [c["id"] for c in MODULE.load(lang, "progression.json")["cards"]]
            ids += [e["id"] for e in MODULE.load(lang, "hidden-elements.json")["entries"]]
            self.assertEqual(len(ids), len(set(ids)), lang)


if __name__ == "__main__":
    unittest.main()
