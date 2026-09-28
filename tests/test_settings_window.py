"""The settings window's pages (frontend.cpp settings_sync, docs/native/settings-window.md)."""
import json
from pathlib import Path
import re
import unicodedata
import unittest

from srw64_native.profile import SETTINGS_KEY_ROWS, SETTINGS_KEY_SECTIONS, SETTINGS_PAGES, UI_KEYS

ROOT = Path(__file__).resolve().parents[1]
LOCALES = ("ja", "zh-Hans", "en")


def ui(locale: str) -> dict[str, str]:
    return json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]


def em(text: str) -> float:
    """Rough width in em: full-width characters one em, the rest about 0.6 (bold UI text)."""
    return sum(1.0 if unicodedata.east_asian_width(c) in "WF" else 0.6 for c in text)


class SettingsWindowTests(unittest.TestCase):
    def setUp(self):
        self.page = (ROOT / "src/native/ui/frontend.cpp").read_text()

    def test_pages_and_key_rows_match_the_python_lists(self):
        pages = re.search(r"settings_pages\[\]=\{(.*?)\};", self.page).group(1)
        self.assertEqual(tuple(re.findall(r'"(\w+)"', pages)), SETTINGS_PAGES)
        rows = re.findall(r'\{"(\w+)","(\w+)"\}', re.search(r"key_rows\[\]=\{(.*?)\};", self.page, re.S).group(1))
        self.assertEqual(tuple(row for _, row in rows), SETTINGS_KEY_ROWS)
        self.assertEqual(tuple(dict.fromkeys(section for section, _ in rows)), SETTINGS_KEY_SECTIONS)
        for locale in LOCALES:
            labels = ui(locale)
            self.assertEqual(set(labels), UI_KEYS, locale)
            for row in SETTINGS_KEY_ROWS:
                for key in (f"settings_key_{row}", f"settings_bind_{row}", f"settings_bind_{row}_pad"):
                    self.assertTrue(labels[key].strip(), (locale, key))

    def test_labels_fit_at_the_smallest_window(self):
        # The UI is laid out at no less than 960 x 720 dp; the panel is 88 % of that less
        # its padding and borders, a row loses the scroll bar and its own padding. RmlUi
        # breaks lines only at spaces, so every run between spaces must fit its column:
        # a Chinese or Japanese sentence is one run.
        panel = 960 * 0.88 - 52 - 2
        row = panel - 8 - 12 - 24
        tab = (panel - 4 * 6) / 5 - 20
        runs = lambda text: text.split(" ")
        choices = {"settings_images": ("original", "hd"), "settings_battle_ui": ("native", "original"),
                   "settings_intermission_ui": ("native", "original"), "settings_name_entry_ui": ("native", "original"),
                   "settings_title_ui": ("native", "original"), "settings_language": ()}
        for locale in LOCALES:
            labels = ui(locale)
            for page in SETTINGS_PAGES:
                self.assertLessEqual(em(labels[f"settings_page_{page}"]) * 15, tab, (locale, page))
            for key, modes in choices.items():
                buttons = [labels[f"{key}_{mode}"] for mode in modes] or ["日本語", "简体中文", "English"]
                # The name (17 dp) and the choices (14 dp, 32 dp padding each) share a line.
                width = em(labels[key]) * 17 + 18 + sum(em(text) * 14 + 32 for text in buttons)
                self.assertLessEqual(width, row, (locale, key))
                for run in runs(labels[f"{key}_note"]):
                    self.assertLessEqual(em(run) * 12, row, (locale, key, run))
            for run in runs(labels["rules_note"]):
                self.assertLessEqual(em(run) * 12, row, (locale, "rules_note"))
            for key in [k for k in labels if k.startswith("rule_")]:
                for run in runs(labels[key]):   # beside the 38 dp switch
                    self.assertLessEqual(em(run) * 15, row - 38 - 14, (locale, key, run))
            for name in SETTINGS_KEY_ROWS:   # the keys take 46 % of the row
                for run in runs(labels[f"settings_key_{name}"]):
                    self.assertLessEqual(em(run) * 14, row * 0.54 - 16, (locale, name, run))
                for key in (f"settings_bind_{name}", f"settings_bind_{name}_pad"):
                    for run in runs(labels[key]):
                        self.assertLessEqual(em(run) * 14, row * 0.46, (locale, key, run))

    def test_page_is_remembered_across_launches(self):
        settings = (ROOT / "src/native/ui/presentation_settings.cpp").read_text()
        self.assertIn('{"settings_page",page}', settings)
        self.assertIn('saved["settings_page"].is_string()', settings)
        self.assertIn("settings::set_settings_page(settings_pages[page]);", self.page)
        self.assertIn("const auto saved=settings::settings_page();", self.page)
        # --language rewrites the file and keeps the page.
        launch = (ROOT / "src/native/app/launch.cpp").read_text()
        self.assertIn('settings_page=saved.value("settings_page","");', launch)
        self.assertIn('{"settings_page",settings_page}', launch)

    def test_keys_controller_and_mouse_turn_the_page(self):
        self.assertIn("if(pressed&(0x0020|0x0010)){settings_turn(pressed&0x0020?-1:1);return;}", self.page)
        for key in ("SDLK_q", "SDLK_e", "SDLK_PAGEUP", "SDLK_PAGEDOWN"):
            self.assertIn(key, self.page[self.page.index("} else if(settings_open){"):])
        self.assertIn('if(id.starts_with("settings-page:"))', self.page)
        # The debug interface clicks a control on another page by turning to it first.
        self.assertIn("for(unsigned page=0;page<std::size(settings_pages) && !found;++page)", self.page)

    def test_every_switch_is_rebuilt_when_it_changes(self):
        stamp = self.page[self.page.index("const auto stamp=localization::catalog().locale+"):]
        stamp = stamp[:stamp.index(";")]
        for call in ("battle_ui", "native_intermission_ui", "native_name_entry_ui", "native_title_ui"):
            self.assertIn(f"settings::{call}()", stamp)
        self.assertIn("settings_page", stamp)


if __name__ == "__main__":
    unittest.main()
