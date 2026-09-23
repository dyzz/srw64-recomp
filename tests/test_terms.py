"""Term tables: same coverage in every locale, and the locale files stay expanded from them."""
import json
from pathlib import Path
import unittest

from srw64_native.catalog import signature
from srw64_native.terms import ORIGIN, load_sections, merge
from srw64_native.weapon_traits import split_menu, weapon_traits

ROOT = Path(__file__).resolve().parents[1]
LOCALES = ("zh-Hans", "en")


def terms(locale):
    return json.loads((ROOT / f"content/locales/terms/{locale}.json").read_text())


class TermTableTests(unittest.TestCase):
    def test_sections_cover_disjoint_ranges(self):
        sections = load_sections(ROOT)
        names = [s["name"] for s in sections]
        self.assertIn("weapons", names)
        derived = [s for s in sections if "derive" in s]
        self.assertEqual([(s["name"], s["derive"]) for s in derived],
                         [("weapon_menus", {"from": "weapons", "offset": -1329})])

    def test_locales_translate_the_same_strings(self):
        tables = {locale: terms(locale) for locale in LOCALES}
        for locale, table in tables.items():
            self.assertEqual(table["schema"], "srw64.terms.v1")
            self.assertEqual(table["locale"], locale)
        reference = tables["zh-Hans"]["sections"]
        for locale in LOCALES[1:]:
            other = tables[locale]["sections"]
            self.assertEqual(reference.keys(), other.keys())
            for section in reference:
                with self.subTest(locale=locale, section=section):
                    self.assertEqual(reference[section].keys(), other[section].keys())

    def test_targets_keep_glyph_parameters(self):
        for locale in LOCALES:
            for section, table in terms(locale)["sections"].items():
                for source, target in table.items():
                    with self.subTest(locale=locale, section=section, source=source):
                        self.assertTrue(target.strip())
                        self.assertEqual(signature(target + "<END>"), signature(source + "<END>"))
                        for run in {source[i:j] for i in range(len(source)) for j in range(i + 2, len(source) + 1)
                                    if set(source[i:j]) == {" "}}:
                            self.assertIn(run, target)
                        if section == "weapons" and source.endswith("MAP"):
                            self.assertTrue(target.endswith("MAP"))
                        if locale == "zh-Hans":
                            self.assertNotIn("・", target)

    def test_locale_term_entries_are_drafts(self):
        for locale in LOCALES:
            document = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
            rows = [row for row in document["entries"] if row.get("origin") == ORIGIN]
            self.assertTrue(rows)
            self.assertTrue(all(row["review_status"] == "draft" for row in rows))

    def test_merge_refuses_hand_entries_over_terms(self):
        document = {"entries": [{"key": "base:t00_00503", "source_sha256": "0" * 64, "target": "移动<END>"}]}
        with self.assertRaises(ValueError):
            merge(document, {"base:t00_00503": "移动<END>"}, {"base:t00_00503": "0" * 64})
        merged = merge(document, {"base:t00_00504": "精神<END>"}, {"base:t00_00504": "1" * 64})
        self.assertEqual([row["key"] for row in merged["entries"]], ["base:t00_00503", "base:t00_00504"])
        self.assertEqual(merged["entries"][1]["origin"], ORIGIN)

    def test_menu_split_matches_traits(self):
        self.assertEqual(split_menu("ファンネルMAP", "射ファンネルBMAP"), ("射", "ファンネル", "BMAP", True))
        self.assertEqual(split_menu("ビームサーベル", "格ビームサーベルP"), ("格", "ビームサーベル", "P", False))
        self.assertEqual([m["token"] for m in weapon_traits("ファンネルMAP", "射ファンネルBMAP")["markers"]],
                         ["射", "B", "MAP"])
        self.assertIsNone(split_menu("ビーム", "ミサイル"))


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class TermExpansionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from srw64_native.catalog import source_catalog
        cls.sources, cls.hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
        cls.sections = load_sections(ROOT)

    def test_locale_files_are_expanded_from_terms(self):
        from srw64_native.terms import expand, unused
        for locale in LOCALES:
            with self.subTest(locale=locale):
                table = terms(locale)
                self.assertEqual(unused(self.sections, table, self.sources), [])
                document = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
                expected = merge(document, expand(self.sections, table, self.sources), self.hashes)
                self.assertEqual(expected["entries"], document["entries"])

    def test_weapon_menus_follow_translated_names(self):
        from srw64_native.terms import expand
        for locale in LOCALES:
            targets = expand(self.sections, terms(locale), self.sources)
            for number in range(1329):
                name, menu = f"base:t00_{1370 + number:05d}", f"base:t00_{2699 + number:05d}"
                if name not in targets:
                    self.assertNotIn(menu, targets)
                    continue
                with self.subTest(locale=locale, number=number):
                    split = split_menu(targets[name][:-5], targets[menu][:-5])
                    original = split_menu(self.sources[name][:-5], self.sources[menu][:-5])
                    self.assertIsNotNone(split)
                    self.assertEqual((split[0], split[2]), (original[0], original[2]))


if __name__ == "__main__":
    unittest.main()
