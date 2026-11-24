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

    def test_branch_layout_covers_each_step_once_in_every_language(self):
        layout = MODULE.STEP_LAYOUT
        for lang in layout["languages"]:
            entries = {e["id"]: e for e in MODULE.load(lang, "hidden-elements.json")["entries"]}
            for entry_id, groups in layout["entries"].items():
                with self.subTest(lang=lang, entry=entry_id):
                    steps = entries[entry_id]["steps"]
                    numbers = []
                    for heading, rows in groups.items():
                        self.assertTrue(layout["labels"][heading][layout["languages"].index(lang)])
                        for row in rows:
                            if isinstance(row, list):
                                self.assertGreaterEqual(len(row), 2)
                                numbers.extend(row)
                            else:
                                numbers.append(row)
                    self.assertEqual(sorted(numbers), list(range(1, len(steps) + 1)))
            for entry in entries.values():
                for step in entry.get("steps", []):
                    if "choices" in step:
                        self.assertNotIn("text", step)
                        self.assertGreaterEqual(len(step["choices"]), 2)
                        self.assertTrue(all(step["choices"]))

    def test_ids_are_unique(self):
        for lang in MODULE.languages():
            ids = [c["id"] for c in MODULE.load(lang, "progression.json")["cards"]]
            ids += [e["id"] for e in MODULE.load(lang, "hidden-elements.json")["entries"]]
            self.assertEqual(len(ids), len(set(ids)), lang)


if __name__ == "__main__":
    unittest.main()
