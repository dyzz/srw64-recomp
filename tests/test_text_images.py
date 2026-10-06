"""Story text drawn as images: card table, ending pages and title labels (docs/native/native-title-and-story-images.md)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

from srw64_native.dialogue_text import load
from srw64_native.profile import UI_KEYS

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"
CARDS = ROOT / "assets/transcriptions/chapter-titles.ja.json"
ENDING = ROOT / "assets/transcriptions/ending-pages.ja.json"
TITLE_LABELS = ("title_press_start", "title_start", "title_load", "title_continue", "title_option",
                "title_demo_giant_robo_subtitle")


class TextImageTests(unittest.TestCase):
    @unittest.skipUnless(ROM.exists() and CARDS.exists(), "needs the ROM and the card transcription")
    def test_card_table_is_current(self):
        result = subprocess.run([sys.executable, "tools/content/text_images.py", "header"], cwd=ROOT,
                                capture_output=True, text=True, env={"PYTHONPATH": "src:tools", "PATH": "/usr/bin:/bin"})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_card_table_shape(self):
        text = (ROOT / "src/host/story_cards.hpp").read_text()
        rows = [line for line in text.splitlines() if line.strip().startswith("{") and line.strip().endswith(("},", "}"))]
        self.assertEqual(len(rows), 133)
        pairs = [tuple(int(v) for v in row.strip(" {},").split(",")) for row in rows]
        self.assertEqual(sum(1 for _, record in pairs if record == 0), 2)       # the 第１話 placeholders
        self.assertTrue(all(281 <= record <= 423 for _, record in pairs if record))

    def test_ending_pages_in_both_languages(self):
        pages = [f"intro:{resource}" for resource in range(5570, 5577)]
        transcription = json.loads(ENDING.read_text()) if ENDING.exists() else None
        for locale in ("zh-Hans", "en"):
            _, intro, problems = load([ROOT / "content/dialogue" / locale], {})
            self.assertFalse([p for p in problems if "ending" in p.path], problems)
            for key in pages:
                self.assertIn(key, intro, f"{locale} {key}")
            if transcription is None:
                continue
            # The '>' lines are the original page, paragraph by paragraph.
            text = (ROOT / "content/dialogue" / locale / "ending.txt").read_text()
            for page in transcription["pages"]:
                block = text.split(f"@intro:{page['resource']} ", 1)[1].split("\n@", 1)[0]
                source = [line[2:] for line in block.splitlines() if line.startswith("> ")]
                self.assertEqual(source, [line for line in page["lines"] if line], page["resource"])
                self.assertEqual(block.count("\n---"), page["lines"].count(""), page["resource"])

    def test_credits_pages_in_both_languages(self):
        # Staff roll 5544-5563 and the copyright page 620; the '>' lines (the Japanese display) agree.
        pages = [f"intro:{resource}" for resource in [*range(5544, 5564), 620]]
        sources = []
        for locale in ("zh-Hans", "en"):
            _, intro, problems = load([ROOT / "content/dialogue" / locale], {})
            self.assertFalse([p for p in problems if "credits" in p.path], problems)
            for key in pages:
                self.assertIn(key, intro, f"{locale} {key}")
            text = (ROOT / "content/dialogue" / locale / "credits.txt").read_text()
            sources.append([line for line in text.splitlines() if line.startswith(">")])
        self.assertEqual(sources[0], sources[1])

    def test_title_labels(self):
        self.assertTrue(set(TITLE_LABELS) <= UI_KEYS)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            for key in TITLE_LABELS:
                self.assertTrue(ui.get(key), f"{locale} {key}")

    def test_title_demo_names(self):
        # sprite_text.cpp kDemoWorks / kDemoUnits: the works' split titles (record 85 + work)
        # and the units' names (527 + unit) are translated; the units follow the demo table
        # 801CB124 (title overlay, ROM 0x10DA50 at 801C4500) but for マジンガーZ (JS 263 -> 262).
        source = (ROOT / "src/host/sprite_text.cpp").read_text()
        works = [int(row.split(",")[0]) for row in source.split("kDemoWorks[] = {", 1)[1].split("};", 1)[0].split("{")[1:]]
        units = [int(row.split(",")[0]) for row in source.split("kDemoUnits[] = {", 1)[1].split("};", 1)[0].split("{")[1:]]
        self.assertEqual(len(works), 23)
        self.assertEqual(len(units), 23)
        if ROM.exists():
            rom = ROM.read_bytes()
            table = [int.from_bytes(rom[0x114674 + 4 * i:0x114676 + 4 * i], "big") for i in range(23)]
            self.assertEqual([262 if unit == 263 else unit for unit in table], units)
        for locale in ("zh-Hans", "en"):
            entries = {e["key"]: e["target"] for e in json.loads((ROOT / f"content/locales/{locale}.json").read_text())["entries"]}
            for work in works:
                self.assertIn(f"base:t00_{85 + work:05d}", entries, f"{locale} work {work}")
            missing = [unit for unit in units if f"base:t00_{527 + unit:05d}" not in entries]
            self.assertEqual(missing, [42], locale)   # F91 reads the same everywhere


if __name__ == "__main__":
    unittest.main()
