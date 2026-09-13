"""The English demo must preserve the Chinese scope and script structure."""
import json
from pathlib import Path
import unittest

from srw64_native.catalog import signature
from srw64_native.profile import UI_KEYS, load_profile
from srw64_native.presentation_settings import selected_locale

ROOT = Path(__file__).resolve().parents[1]


class EnglishLocaleTests(unittest.TestCase):
    def test_draft_parity_and_original_script_parameters(self):
        chinese = json.loads((ROOT / 'content/locales/zh-Hans.json').read_text())
        english = json.loads((ROOT / 'content/locales/en.json').read_text())
        source = {row['key']: row for row in chinese['entries']}
        target = {row['key']: row for row in english['entries']}
        self.assertEqual(len(target), len(english['entries']))
        self.assertEqual(source.keys(), target.keys())
        self.assertEqual(set(english['ui']), UI_KEYS)
        for key, row in target.items():
            with self.subTest(key=key):
                self.assertEqual(row['source_sha256'], source[key]['source_sha256'])
                self.assertEqual(signature(row['target']), signature(source[key]['target']))
                self.assertEqual(row['review_status'], 'draft')
                self.assertTrue(row['target'].strip())
        self.assertTrue(all(english['ui'].values()))

    def test_english_launch_and_saved_preference(self):
        import tempfile
        profile = load_profile(ROOT / 'config/recomp/play-profile.json', locale='en', images='original')
        self.assertEqual(profile['presentation']['locale'], 'en')
        self.assertEqual(list(profile['locales']), ['ja', 'zh-Hans', 'en'])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'presentation.json'
            path.write_text(json.dumps({'schema': 'srw64.presentation-settings.v1', 'locale': 'en'}))
            self.assertEqual(selected_locale(path, profile['locales'], 'ja'), 'en')
