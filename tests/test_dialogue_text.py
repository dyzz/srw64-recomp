"""Plain-text dialogue files: syntax, checks against the original, overrides."""
import json
import tempfile
from pathlib import Path
import unittest

from srw64_native.dialogue_text import (DialogueTextError, compile_entry, format_entry, load, pages_of, parse)

ROOT = Path(__file__).resolve().parents[1]
KEY = "base:t00_17412"
SOURCE = "「お嬢様、<G:0124><G:0124><G:0124>です」<BR> 二行目<STOP>次のページ<END>"


def compiled(text, source=SOURCE):
    entries, problems = parse(text)
    assert not problems, problems
    return compile_entry(entries[0], source)


class FormatTests(unittest.TestCase):
    def test_pages_collapse_one_name_run(self):
        self.assertEqual(pages_of(SOURCE), [["「お嬢様、{HeroNick}です」", " 二行目"], ["次のページ"]])
        self.assertEqual(pages_of("<G:0104>A<G:0124><G:0126><END>"), [["{G:0104}A{HeroNick}{HeroName}"]])

    def test_round_trip(self):
        target = "「小姐，<G:0124>」<BR>第二行<STOP>下一页<END>"
        text = format_entry(KEY, SOURCE, target, "ローレンス")
        self.assertTrue(text.startswith("@17412 ローレンス\n> 「お嬢様、{HeroNick}です」\n>  二行目\n"))
        self.assertEqual(compiled(text), target)

    def test_free_line_breaks_and_moved_placeholders(self):
        text = "@17412\n{HeroNick}小姐，\n是这样的\n第三行\n---\n下一页\n"
        self.assertEqual(compiled(text), "<G:0124>小姐，<BR>是这样的<BR>第三行<STOP>下一页<END>")

    def test_rejections(self):
        cases = {
            "@17412\n只有一页\n": "pages",
            "@17412\n没有名字\n---\n下一页\n": "missing {HeroNick}",
            "@17412\n{HeroNick}{HeroName}\n---\n下一页\n": "extra {HeroName}",
            "@17412\n{HeroNik}\n---\n下一页\n": "unknown placeholder",
            "@17412\n{HeroNick} <b>\n---\n下一页\n": "half-width",
            "@17412\n> 違う原文\n{HeroNick}\n---\n下一页\n": "original text",
            "@17412\n{HeroNick}\n---\n": "no translation",
            "@100\n名字\n": "term tables",
        }
        for text, message in cases.items():
            with self.subTest(text=text):
                with self.assertRaises(DialogueTextError) as caught:
                    compiled(text, "名前<END>" if text.startswith("@100") else SOURCE)
                self.assertIn(message, str(caught.exception))

    def test_template_without_translation_is_ignored(self):
        self.assertIsNone(compiled(format_entry(KEY, SOURCE)))

    def test_choices(self):
        source = "協力する<BR>断る<END>"
        text = format_entry("base:t00_18020", source, "合作<BR>拒绝<END>", options=True)
        self.assertIn("> * 協力する\n> * 断る\n* 合作\n* 拒绝\n", text)
        self.assertEqual(compiled(text, source), "合作<BR>拒绝<END>")
        with self.assertRaises(DialogueTextError):
            compiled("@18020\n> * 協力する\n> * 断る\n* 合作\n", source)
        with self.assertRaises(DialogueTextError):
            compiled("@18020\n* 合作\n拒绝\n", source)

    def test_escapes_comments_bom_and_crlf(self):
        text = "﻿# 注释\r\n@17412 \r\n\\> 箭头 {HeroNick}\r\n\\* 星号\r\n---\r\n\\---\r\n"
        self.assertEqual(compiled(text), "> 箭头 <G:0124><BR>* 星号<STOP>---<END>")
        self.assertEqual(format_entry(KEY, "*印<END>", "---<END>").splitlines()[1:], ["> *印", "\\---"])
        entries, problems = parse("孤立的一行\n@abc\n")
        self.assertEqual([p.message for p in problems], ["text outside an entry", "malformed entry header"])
        self.assertEqual(entries, [])

    def test_intro_entries_are_kept_apart(self):
        entries, _ = parse("@intro:5506\n序章第一行\n")
        self.assertEqual((entries[0].key, compile_entry(entries[0], None)), ("intro:5506", "序章第一行<END>"))

    def test_load_overrides_and_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundled, user = Path(tmp) / "bundled", Path(tmp) / "user"
            (bundled / "story").mkdir(parents=True)
            user.mkdir()
            (bundled / "story/a.txt").write_text("@17412\n{HeroNick}\n---\n附带\n", encoding="utf-8")
            (bundled / "story/b.txt").write_text("@17412\n{HeroNick}\n---\n重复\n@17413\n另一条\n", encoding="utf-8")
            (user / "mine.txt").write_text("@17412\n{HeroNick}\n---\n玩家改的\n@17413\n只有一页\n---\n多了一页\n", encoding="utf-8")
            sources = {KEY: SOURCE, "base:t00_17413": "別の台詞<END>"}
            targets, intro, problems = load([bundled, user], sources)
            self.assertEqual(targets[KEY], "<G:0124><STOP>玩家改的<END>")
            self.assertEqual(targets["base:t00_17413"], "另一条<END>")  # the broken override falls back
            self.assertEqual([(p.path, p.key) for p in problems],
                             [("story/b.txt", KEY), ("mine.txt", "base:t00_17413")])


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class BundledDialogueTests(unittest.TestCase):
    def test_bundled_files_load_cleanly_and_match_in_every_locale(self):
        from srw64_native.catalog import source_catalog
        sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
        loaded = {}
        for locale in ("zh-Hans", "en"):
            targets, intro, problems = load([ROOT / f"content/dialogue/{locale}"], sources)
            self.assertEqual([str(p) for p in problems], [])
            loaded[locale] = targets
            with self.subTest(locale=locale):
                self.assertTrue(targets)
                terms = {row["key"] for row in json.loads((ROOT / f"content/locales/{locale}.json").read_text())["entries"]}
                self.assertFalse(terms & targets.keys())
        self.assertEqual(loaded["zh-Hans"].keys(), loaded["en"].keys())


if __name__ == "__main__":
    unittest.main()
