import json
import unittest
from pathlib import Path

from srw64_native.dialogue_paging import (COMMA, HALVABLE, OPENING, SENTENCE_END, Line, body_size, body_style,
                                          joined_record, paginate, sentence_ends, short_line, utf16)

ROOT = Path(__file__).resolve().parents[1]
CASES = json.loads((ROOT / "tests/data/dialogue-paging-cases.json").read_text(encoding="utf-8"))
INPUTS = json.loads((ROOT / "tests/data/dialogue-paging-inputs.json").read_text(encoding="utf-8"))


def table(case):
    lines = {row["start"]: Line(row["start"], row["end"], row["width"], row["halved"], row["emergency"])
             for row in case["line_table"]}
    return lines.__getitem__


class SharedCaseTests(unittest.TestCase):
    """The game's paging cases (tests/dialogue_cpu/paging_cases.cpp): same pages from the same lines."""

    def test_marks_match_the_game(self):
        self.assertEqual(CASES["marks"], {"sentence_end": SENTENCE_END, "comma": COMMA, "halvable": HALVABLE,
                                          "opening": OPENING})

    def test_style(self):
        for case in CASES["cases"]:
            with self.subTest(case["name"]):
                style = body_style(case["locale"])
                self.assertAlmostEqual(body_size(case["setting"], case["locale"]), case["size"])
                self.assertEqual((style.height, style.min_spacing, style.max_spacing, style.halve_line_end),
                                 (case["height"], case["min_spacing"], case["max_spacing"], case["halve_line_end"]))
                self.assertEqual(style.lines_per_page(case["size"]), case["expected"]["lines_per_page"])
                self.assertAlmostEqual(style.pitch(case["size"]), case["expected"]["pitch"], places=9)

    def test_joined_record(self):
        inputs = {case["name"]: case for case in INPUTS["cases"]}
        for case in CASES["cases"]:
            with self.subTest(case["name"]):
                text, stops = joined_record(inputs[case["name"]]["pages"], case["locale"])
                self.assertEqual((text, stops), (case["text"], case["stops"]))

    def test_pages_and_lines(self):
        for case in CASES["cases"]:
            with self.subTest(case["name"]):
                expected = case["expected"]
                paged = paginate(case["text"], case["clusters"], case["legal"], table(case),
                                 expected["lines_per_page"], case["forced"], sentence_ends(case["locale"], case["stops"]))
                self.assertEqual([list(p) for p in paged.pages], expected["pages"])
                self.assertEqual([(l.start, l.end) for l in paged.lines],
                                 [(l["start"], l["end"]) for l in expected["lines"]])
                self.assertEqual([l.end for l in paged.lines if l.halved], expected["halved"])

    def test_cases_cover_ranking_and_forcing(self):
        names = [case["name"] for case in CASES["cases"]]
        self.assertTrue(any("forced" in name for name in names))
        self.assertEqual({case["locale"] for case in CASES["cases"]}, {"zh-Hans", "en"})
        self.assertTrue(any(len(case["expected"]["pages"]) > 1 for case in CASES["cases"]))


class RuleTests(unittest.TestCase):
    def test_english_join_adds_one_space(self):
        self.assertEqual(joined_record(["Wait.", "Now!"], "en"), ("Wait. Now!", [6]))
        self.assertEqual(joined_record(["Wait. ", "Now!"], "en"), ("Wait. Now!", [6]))
        self.assertEqual(joined_record(["等等。", "现在！"], "zh-Hans"), ("等等。现在！", [3]))

    def test_offsets_are_utf16(self):
        self.assertEqual(len(utf16("🔧▶")), 3)
        self.assertEqual(joined_record(["🔧", "好"], "zh-Hans"), ("🔧好", [2]))

    def test_short_last_line(self):
        units = utf16("这一行很长很长。好。")
        clusters = list(range(1, len(units) + 1))
        self.assertTrue(short_line(units, clusters, 8, 10))
        self.assertFalse(short_line(units, clusters, 0, 8))
        # Marks and quotes do not count: “上！” is one character.
        shout = utf16("“上！”")
        self.assertTrue(short_line(shout, [1, 2, 3, 4], 0, 4))
        words = utf16("one two")
        self.assertFalse(short_line(words, list(range(1, 8)), 0, 7))
        self.assertTrue(short_line(words, list(range(1, 8)), 4, 7))

    def test_fewest_pages_then_sentence_ends(self):
        # 36 units in 10-unit lines, two lines a page: two pages either way; the
        # first page ends at the sentence end at 18 rather than the line end at 20.
        text = "一二三四五六七八九十一二三四五六七。" + "九十一二三四五六七八九十一二三四五六"
        units = utf16(text)
        clusters = list(range(1, len(units) + 1))
        legal = list(range(1, len(units) + 1))

        def line_at(start):
            return Line(start, min(start + 10, len(units)))

        paged = paginate(text, clusters, legal, line_at, 2)
        self.assertEqual(paged.pages, [(0, 18), (18, 36)])
        greedy = paginate(text, clusters, legal, line_at, 2, rank_breaks=False)
        self.assertEqual(greedy.pages, [(0, 20), (20, 36)])

    def test_original_page_breaks_end_sentences_only_in_japanese(self):
        self.assertEqual(sentence_ends("ja", [9, 3]), [3, 9])
        self.assertEqual(sentence_ends("zh-Hans", [3]), [])
        self.assertEqual(sentence_ends("en", [3]), [])

    def test_ties_keep_earlier_pages_full(self):
        # 25 units in 10-unit lines, two lines a page, no sentence ends: the first page
        # could end at 5..20 at equal rank; it takes the latest start for the second page.
        text = "一二三四五六七八九十" * 2 + "一二三四五"
        n = len(text)
        paged = paginate(text, list(range(1, n + 1)), list(range(1, n + 1)), lambda s: Line(s, min(s + 10, n)), 2)
        self.assertEqual(paged.pages, [(0, 20), (20, 25)])

    def test_forced_start_is_kept(self):
        text = "一二三四五六七八九十" * 3
        n = len(text)
        paged = paginate(text, list(range(1, n + 1)), list(range(1, n + 1)),
                         lambda s: Line(s, min(s + 10, n)), 3, forced=[5])
        self.assertEqual(paged.pages[0], (0, 5))
        self.assertEqual(paged.pages[1][0], 5)


if __name__ == "__main__":
    unittest.main()
