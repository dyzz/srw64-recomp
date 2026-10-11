"""Approved SRWZ decisions stay consistent across SRW64's translation pipelines."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/translation"))
from run_mt import renames


def document(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class SrwzTermMigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.migration = document("content/translation/srwz-terms.json")
        cls.tables = document("content/locales/terms/zh-Hans.json")["sections"]
        cls.story = document("content/translation/story-terms.json")["terms"]

    def target(self, row):
        if row["section"] == "story":
            return self.story[row["ja"]]["zh"]
        return self.tables[row["section"]][row["ja"]]

    def test_only_approved_donor_identities_are_bound(self):
        identities = {t["id"]: t for t in self.migration["terms"]}
        self.assertEqual(len(identities), len(self.migration["terms"]))
        for term in identities.values():
            with self.subTest(term=term["id"]):
                self.assertEqual(term["status"], "approved")
                self.assertNotEqual(term["registry_match"], "explicit_only")
                self.assertTrue(term["source_files"])
        for row in self.migration["matches"]:
            self.assertIn(row["srwz_id"], identities)
        for row in self.migration["dialogue_renames"]:
            self.assertIn(row["srwz_id"], identities)

    def test_terms_and_same_identity_compounds_follow_the_binding_plan(self):
        for row in self.migration["matches"]:
            with self.subTest(section=row["section"], source=row["ja"]):
                self.assertEqual(self.target(row), row["translation"])

    def test_different_identities_and_field_meanings_keep_their_translation(self):
        for row in self.migration["excluded"]:
            with self.subTest(section=row["section"], source=row["ja"]):
                self.assertEqual(self.target(row), row["translation"], row["reason"])
        self.assertEqual(self.tables["pilots"]["エイジ"], "英二")
        self.assertEqual(self.tables["pilots"]["シロー"], "士郎")
        self.assertEqual(self.tables["pilots"]["エマリー"], "艾玛丽")
        self.assertEqual(self.tables["stages"]["ムーンアタック"], "进攻月球")
        self.assertEqual(self.story["ミネルバ"]["zh"], "米涅瓦")

    def test_future_dialogue_generation_resolves_directly_to_the_new_names(self):
        pipeline = renames("zh-Hans")
        for row in self.migration["bindings"] + self.migration["dialogue_renames"]:
            if row["section"] == "spirit_abbreviations":
                continue  # UI-only one-character marker, not dialogue prose.
            with self.subTest(section=row["section"], source=row["ja"]):
                self.assertEqual(pipeline.apply(row["old"], row["ja"]), row["new"])
                self.assertEqual(pipeline.apply(row["new"], row["ja"]), row["new"])

    def test_short_roots_preserve_unrelated_names_and_ordinary_words(self):
        pipeline = renames("zh-Hans")
        cases = [
            ("艾玛丽和艾玛。", "エマリーとエマ", "艾玛丽和爱玛。"),
            ("雷伊，英二，士郎。", "レイ、エイジ、シロー", "雷伊，英二，士郎。"),
            ("靠直觉行事。", "直感で行動する", "靠直觉行事。"),
            ("米涅瓦X与精神感应高达。", "ミネルバXとサイコガンダム", "米涅瓦X与精神感应高达。"),
            ("{HeroName}使用赞波马格南。", "ザンボマグナム", "{HeroName}使用赞波马格南。"),
            ("浮游炮！！", "フィンファンルっ!!", "翼状浮游炮！！"),
        ]
        for text, source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(pipeline.apply(text, source), expected)


if __name__ == "__main__":
    unittest.main()
