"""Hong Kong's Traditional Chinese made from the Simplified (src/srw64_native/zh_hant_hk.py)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from srw64_native.zh_hant import Converter
from srw64_native.zh_hant_hk import HongKong

ROOT = Path(__file__).resolve().parents[1]


def hong_kong(names, terms, rules, characters=None):
    directory = tempfile.TemporaryDirectory()
    root = Path(directory.name)
    (root / "content/translation/zh-Hant-HK").mkdir(parents=True)
    (root / "content/locales/terms").mkdir(parents=True)
    (root / "content/translation/zh-Hant-HK/names.json").write_text(json.dumps(
        {"schema": "srw64.zh-hant-hk-names.v1", "entries": [{"ja": ja, "zh_hant_hk": hk} for ja, hk in names.items()]}))
    (root / "content/translation/zh-Hant-HK/regional.json").write_text(json.dumps(
        {"schema": "srw64.zh-hant-hk-regional.v1", "terms": rules, "characters": characters or {"說": "説", "裡": "裏", "臺": "台"}}))
    (root / "content/locales/terms/zh-Hans.json").write_text(json.dumps(
        {"schema": "srw64.terms.v1", "locale": "zh-Hans", "sections": terms}))
    result = HongKong(root)
    directory.cleanup()
    return result


class HongKongTests(unittest.TestCase):
    def setUp(self):
        self.hk = hong_kong({"アムロ": "阿寶", "ガンダム": "高達"},
                            {"pilots": {"アムロ": "阿姆罗", "カミーユ": "卡缪"}, "units": {"ガンダム": "高达", "ゲッターロボ": "盖塔机器人"}},
                            [{"group": "franchise", "sc": "机器人", "hk": "機械人"},
                             {"group": "franchise", "sc": "盖塔机器人", "hk": "三一萬能俠"},
                             {"group": "term", "sc": "导弹", "hk": "導彈"},
                             {"group": "term", "sc": "信息", "hk": "資訊", "jp": ["情報"]},
                             {"group": "term", "sc": "信息", "hk": "信息"}])

    def test_characters_words_and_quotes(self):
        # Mainland words stay (no s2twp), Hong Kong characters and quotation marks.
        self.assertEqual(self.hk.convert("“他说软件里有导弹”"), "「他説軟件裏有導彈」")

    def test_names_only_where_the_japanese_has_them(self):
        self.assertEqual(self.hk.convert("阿姆罗驾驶高达。", "アムロがガンダムに乗る"), "阿寶駕駛高達。")
        self.assertEqual(self.hk.convert("阿姆罗驾驶高达。"), "阿姆羅駕駛高達。")

    def test_words_bound_to_the_japanese(self):
        self.assertEqual(self.hk.convert("情报信息", "敵の情報"), "情報資訊")
        self.assertEqual(self.hk.convert("一条信息", "メッセージ"), "一條信息")

    def test_the_longest_word_first(self):
        self.assertEqual(self.hk.term("units", "ゲッターロボ", "盖塔机器人"), "三一萬能俠")
        self.assertEqual(self.hk.convert("这台机器人"), "這台機械人")

    def test_names_without_a_hong_kong_name(self):
        # Characters only, and the works' names in them; never the words.
        self.assertEqual(self.hk.term("pilots", "カミーユ", "卡缪"), "卡繆")

    def test_taiwan_phrases_in_hong_kong_characters(self):
        hk = HongKong(ROOT)
        self.assertEqual(hk.convert("总是只能等，组织里的人干得很好"), "總是只能等，組織裏的人幹得很好")
        # The Simplified 著 is zhù and stays; 着 is Hong Kong's 着.
        self.assertEqual(hk.convert("效果显著，看着他"), "效果顯著，看着他")

    def test_add_to_only_where_it_differs(self):
        value = {"t": {"zh-Hans": "阿姆罗", "zh-Hant": "阿姆羅", "ja": "アムロ"}, "u": {"zh-Hans": "出击", "zh-Hant": "出擊", "ja": "出撃"}}
        out = self.hk.add_to(value)
        self.assertEqual(list(out["t"]), ["zh-Hans", "zh-Hant", "zh-Hant-HK", "ja"])
        self.assertEqual(out["t"]["zh-Hant-HK"], "阿寶")
        self.assertNotIn("zh-Hant-HK", out["u"])
        self.assertEqual(self.hk.add_to(out), out)

    def test_library_names_have_no_japanese(self):
        self.assertEqual(self.hk.name("高达Mk-Ⅱ(阿姆罗)"), "高達Mk-Ⅱ(阿寶)")


class GeneratedFileTests(unittest.TestCase):
    def test_tables_are_well_formed(self):
        names = json.loads((ROOT / "content/translation/zh-Hant-HK/names.json").read_text())
        regional = json.loads((ROOT / "content/translation/zh-Hant-HK/regional.json").read_text())
        taiwan = json.loads((ROOT / "content/translation/zh-Hant/names.json").read_text())
        self.assertEqual([row["ja"] for row in names["entries"]], [row["ja"] for row in taiwan["entries"]],
                         "one Hong Kong row per Taiwan row: run tools/translation/import_srwz_hk.py")
        for row in names["entries"]:
            self.assertTrue(row["zh_hant_hk"], row["ja"])
            self.assertIn(row["basis"], {"srwz", "derived", "tw", "hk_dub", "hk_release", "hk_common", "official_tc", "keep_tw", "decided"})
        for rule in regional["terms"]:
            self.assertTrue(rule["sc"] and rule["hk"], rule)
        for taiwan_form, hong_kong_form in regional["characters"].items():
            self.assertEqual((len(taiwan_form), len(hong_kong_form)), (1, 1))

    def test_locale_stands_on_taiwan(self):
        hk = json.loads((ROOT / "content/locales/zh-Hant-HK.json").read_text())
        tw = json.loads((ROOT / "content/locales/zh-Hant.json").read_text())
        self.assertEqual((hk["fallback"], hk["region"], tw["region"]), ("zh-Hant", "HK", "TW"))
        own = {row["key"]: row["target"] for row in tw["entries"]}
        self.assertTrue(all(own.get(row["key"]) != row["target"] for row in hk["entries"]), "only what Hong Kong writes differently")
        self.assertTrue(all(tw["ui"].get(key) != text for key, text in hk["ui"].items()))

    @unittest.skipUnless((ROOT / "rom.z64").is_file() or (ROOT / "assets/rom.z64").is_file(), "ROM unavailable")
    def test_generated_files_are_current(self):
        spec = importlib.util.spec_from_file_location("make_zh_hant", ROOT / "tools/translation/make_zh_hant.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        files = module.outputs(Converter(ROOT), HongKong(ROOT))
        stale = [str(p.relative_to(ROOT)) for p, text in files.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
        self.assertEqual(stale, [], "run tools/translation/make_zh_hant.py")


if __name__ == "__main__":
    unittest.main()
