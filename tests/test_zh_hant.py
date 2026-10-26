"""Traditional Chinese made from the Simplified (src/srw64_native/zh_hant.py, tools/translation/make_zh_hant.py)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from srw64_native.zh_hant import Converter

ROOT = Path(__file__).resolve().parents[1]


def converter(names, terms, words=None):
    directory = tempfile.TemporaryDirectory()
    root = Path(directory.name)
    (root / "content/translation/zh-Hant").mkdir(parents=True)
    (root / "content/locales/terms").mkdir(parents=True)
    (root / "content/translation/zh-Hant/names.json").write_text(json.dumps(
        {"schema": "srw64.zh-hant-names.v1", "entries": [{"ja": ja, "zh_hant": hant} for ja, hant in names.items()],
         "words": words or {}}))
    (root / "content/locales/terms/zh-Hans.json").write_text(json.dumps(
        {"schema": "srw64.terms.v1", "locale": "zh-Hans", "sections": terms}))
    result = Converter(root)
    directory.cleanup()
    return result


class ConverterTests(unittest.TestCase):
    def setUp(self):
        self.hant = converter({"アムロ": "阿姆羅", "ガンダム": "鋼彈", "集中": "專注"},
                              {"pilots": {"アムロ": "阿姆罗", "カミーユ": "卡缪"}, "units": {"ガンダム": "高达"},
                               "spirits": {"集中": "集中", "熱血": "热血"}, "stages": {"出撃": "出击"}},
                              words={"扎古": "薩克"})

    def test_wording_and_quotes(self):
        self.assertEqual(self.hant.convert("“软件里的‘鼠标’”"), "「軟體裡的『滑鼠』」")

    def test_names_only_where_the_japanese_has_them(self):
        self.assertEqual(self.hant.convert("阿姆罗驾驶高达。", "アムロがガンダムに乗る"), "阿姆羅駕駛鋼彈。")
        # Without the Japanese the mainland name is only converted.
        self.assertEqual(self.hant.convert("阿姆罗驾驶高达。"), "阿姆羅駕駛高達。")
        self.assertEqual(self.hant.convert("高达", "アムロ"), "高達")

    def test_names_without_a_taiwan_name_keep_their_characters(self):
        # 卡缪 has no Taiwan name: characters only, never s2twp words.
        self.assertEqual(self.hant.term("pilots", "カミーユ", "卡缪"), "卡繆")
        self.assertEqual(self.hant.term("units", " ガンダム", " 高达"), " 鋼彈")

    def test_game_terms_take_the_official_term_else_wording(self):
        self.assertEqual(self.hant.term("spirits", "集中", "集中"), "專注")
        self.assertEqual(self.hant.term("spirits", "熱血", "热血"), "熱血")
        self.assertEqual(self.hant.term("stages", "出撃", "出击"), "出擊")

    def test_words_everywhere(self):
        self.assertEqual(self.hant.convert("扎古出击"), "薩克出擊")

    def test_add_to_puts_zh_hant_after_zh_hans(self):
        value = {"title": {"zh-Hans": "阿姆罗出击", "ja": "アムロ出撃", "en": "Amuro"}, "n": 1}
        out = self.hant.add_to(value)
        self.assertEqual(list(out["title"]), ["zh-Hans", "zh-Hant", "ja", "en"])
        self.assertEqual(out["title"]["zh-Hant"], "阿姆羅出擊")
        self.assertEqual(self.hant.add_to(out), out)


class GeneratedFileTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / "rom.z64").is_file() or (ROOT / "assets/rom.z64").is_file(), "ROM unavailable")
    def test_generated_files_are_current(self):
        spec = importlib.util.spec_from_file_location("make_zh_hant", ROOT / "tools/translation/make_zh_hant.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        files = module.outputs(Converter(ROOT))
        stale = [str(p.relative_to(ROOT)) for p, text in files.items()
                 if not p.exists() or p.read_text(encoding="utf-8") != text]
        self.assertEqual(stale, [], "run tools/translation/make_zh_hant.py")

    def test_dialogue_keeps_the_japanese(self):
        spec = importlib.util.spec_from_file_location("make_zh_hant", ROOT / "tools/translation/make_zh_hant.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        hant = converter({"アムロ": "阿姆羅"}, {"pilots": {"アムロ": "阿姆罗"}})
        text = "# 自动生成：x\n\n@1 阿姆罗 · アムロ（主角）\n> 「アムロ、行きます!」\n“阿姆罗，出击！”\n---\n> 次\n* 选项\n"
        self.assertEqual(module.dialogue_file(hant, text),
                         module.MARKER + "\n\n@1 阿姆羅 · アムロ（主角）\n> 「アムロ、行きます!」\n「阿姆羅，出擊！」\n---\n> 次\n* 選項\n")


if __name__ == "__main__":
    unittest.main()
