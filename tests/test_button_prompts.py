"""Button prompt tokens: the strings, the C++ table and the SRW64Prompts font agree.

UI strings write "{A}", "{Esc}" and so on; src/native/text/button_prompts.hpp turns a
token into a Private Use Area character per controller family, and
content/fonts/SRW64Prompts.ttf (tools/content/build_prompt_font.py) draws it."""
import collections
import json
import re
import unittest
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "src/native/text/button_prompts.hpp").read_text()
TABLE = {m[0]: [int(c, 16) for c in m[1:]] for m in re.findall(
    r'\{"(\w+)", 0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+)\}', HEADER)}
KEYBOARD = {"Esc", "Enter", "Tab", "Space", "Ctrl", "KeyLeft", "KeyUp", "KeyRight", "KeyDown", "Arrows", "WASD",
            "IJKL", "F5", "F6", "F7"}
TOKEN = re.compile(r"\{([A-Z][A-Za-z0-9]*)\}")
LOCALES = ("zh-Hans", "en", "ja")


def catalogue(locale: str) -> dict:
    data = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
    return data.get("ui", data)


class ButtonPromptTests(unittest.TestCase):
    def test_table_parses(self):
        self.assertGreater(len(TABLE), 30)
        self.assertTrue(KEYBOARD <= set(TABLE))
        # Keyboard keys look the same for every controller family.
        for token in KEYBOARD:
            self.assertEqual(len(set(TABLE[token])), 1, token)

    def test_font_draws_every_character_the_table_uses(self):
        font = ImageFont.truetype(str(ROOT / "content/fonts/SRW64Prompts.ttf"), 1000)
        builder = (ROOT / "tools/content/build_prompt_font.py").read_text()
        built = {int(c, 16) for c in re.findall(r"\(0x(E8[0-9A-F]{2}), 0x", builder)}
        used = {c for codes in TABLE.values() for c in codes}
        self.assertTrue(used <= built, sorted(hex(c) for c in used - built))
        for code in sorted(built):
            with self.subTest(code=hex(code)):
                self.assertGreater(font.getlength(chr(code)), 1000)
        # The original PromptFont code points are gone: HarmonyOS Sans keeps its arrows.
        self.assertEqual(font.getlength("⇓"), 500)
        self.assertEqual(font.getlength("A"), 500)

    def test_font_ships_with_its_licence(self):
        manifest = json.loads((ROOT / "content/fonts/harmonyos-sans.json").read_text())
        self.assertIn("SRW64Prompts.ttf", manifest["bundled"])
        self.assertIn("LICENSE-SRW64Prompts.txt", manifest["bundled"])
        licence = (ROOT / "content/fonts/LICENSE-SRW64Prompts.txt").read_text()
        self.assertIn("SIL OPEN FONT LICENSE", licence.upper())
        self.assertIn("shinmera.com/promptfont", licence)

    def test_strings_use_known_tokens_the_same_in_every_language(self):
        catalogues = {locale: catalogue(locale) for locale in LOCALES}
        for key, text in catalogues["zh-Hans"].items():
            if not isinstance(text, str):
                continue
            tokens = {locale: collections.Counter(TOKEN.findall(catalogues[locale].get(key, ""))) for locale in LOCALES}
            with self.subTest(key=key):
                for locale in LOCALES:
                    self.assertNotIn("{{", catalogues[locale].get(key, ""))
                    self.assertTrue(set(tokens[locale]) <= set(TABLE), (locale, set(tokens[locale]) - set(TABLE)))
                self.assertEqual(tokens["zh-Hans"], tokens["en"])
                self.assertEqual(tokens["zh-Hans"], tokens["ja"])

    def test_controller_hints_name_controller_buttons_and_keyboard_hints_keys(self):
        ui = catalogue("zh-Hans")
        for key, text in ui.items():
            if not isinstance(text, str):
                continue
            tokens = set(TOKEN.findall(text))
            with self.subTest(key=key):
                if key.endswith("_pad") or key == "pad_rstick_down":
                    self.assertFalse(tokens & KEYBOARD)
                elif tokens:
                    self.assertTrue(tokens <= KEYBOARD, tokens - KEYBOARD)

    def test_the_hints_that_name_buttons_use_tokens(self):
        # The controller strings no longer spell out button names.
        for locale in LOCALES:
            for key, text in catalogue(locale).items():
                if isinstance(text, str) and key.endswith("_pad"):
                    with self.subTest(locale=locale, key=key):
                        self.assertIsNone(re.search(r"(?<![A-Za-z0-9{])(A|B|L1|R1|L2|R2)(?![A-Za-z0-9}])", text))
                        for word in ("视图键", "菜单键", "十字键", "摇杆", "ビュー", "メニュー", "十字キー", "スティック", "D-pad", "stick"):
                            self.assertNotIn(word, text)


if __name__ == "__main__":
    unittest.main()
