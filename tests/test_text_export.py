import json
from pathlib import Path
import unittest

from srw64_native.text_export import (BATTLE_FIRST_ID, CATEGORIES, RANGES, STORY_FIRST_ID, TextExporter,
                                      category_of, flags_of, summarize, visible)

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'assets/original-data'


class RangeTests(unittest.TestCase):
    def test_ranges_tile_the_non_story_ids_without_gaps(self):
        expected = 0
        for r in RANGES:
            self.assertEqual(r.start, expected, r)
            self.assertLessEqual(r.start, r.end)
            self.assertIn(r.category, CATEGORIES)
            self.assertIn(r.evidence, ('code', 'content'))
            expected = r.end + 1
        self.assertEqual(expected, STORY_FIRST_ID)

    def test_code_confirmed_formulas(self):
        self.assertEqual(category_of(0, 527)[0], 'names.unit')
        self.assertEqual(category_of(0, 527 + 362)[0], 'names.unit')
        self.assertEqual(category_of(0, 1370)[0], 'names.weapon')
        self.assertEqual(category_of(0, 2699)[0], 'names.weapon_menu')
        self.assertEqual(category_of(0, 969 + 29)[0], 'names.spirit')
        self.assertEqual(category_of(0, 4382)[0], 'names.actor')
        self.assertEqual(category_of(0, 4743 + 360)[0], 'names.actor_full')
        self.assertEqual(category_of(0, 281 + 142)[0], 'story.chapter_title')
        self.assertEqual(category_of(0, BATTLE_FIRST_ID)[0], 'battle.quote')
        self.assertEqual(category_of(0, STORY_FIRST_ID)[0], 'story.dialogue')
        self.assertEqual(category_of(7, 3)[0], 'extras.lyrics')

    def test_flags(self):
        self.assertIn('numeric-gap', flags_of('system.ui', '命中率    %<END>'))
        self.assertNotIn('numeric-gap', flags_of('story.dialogue', '「あ、<BR> <G:0126><G:0126>  さん」<END>'))
        self.assertIn('fragment', flags_of('system.ui', 'をおぼえた<END>'))
        self.assertIn('dynamic-name', flags_of('story.dialogue', '「<G:0124><G:0124>!」<END>'))
        self.assertIn('paged', flags_of('battle.quote', '「くっ!」<STOP>「まだだ!」<END>'))
        self.assertIn('blank', flags_of('extras.lyrics', ' <END>'))
        self.assertEqual(visible('「あ、<BR> い」<STOP>う<END>'), '「あ、い」う')

    def test_summary_counts_unique_sources(self):
        rows = [{'category': 'names.unit', 'source': 'A<END>', 'chars': 1, 'native_page': True, 'flags': []},
                {'category': 'names.unit', 'source': 'A<END>', 'chars': 1, 'native_page': True, 'flags': []},
                {'category': 'names.unit', 'source': 'BB<END>', 'chars': 2, 'native_page': False, 'flags': [],
                 'existing': {'zh-Hans': 'x'}}]
        s = summarize(rows)['names.unit']
        self.assertEqual((s['records'], s['unique'], s['chars'], s['unique_chars'], s['native_page_records']),
                         (3, 2, 4, 3, 2))
        self.assertEqual(s['existing_translations'], {'zh-Hans': 1})

    def test_intro_pages_keep_route_order(self):
        doc = {'groups': {'common': {'pages': [1, 2]}, 'route-1': {'pages': [2, 3]}},
               'pages': [{'resource': r, 'image_sha256': 'x', 'lines': ['あ', '', 'い']} for r in (1, 2, 3)]}
        rows = TextExporter.intro_records(doc)
        self.assertEqual(rows[1]['context']['played_in'], ['common#1', 'route-1#0'])
        self.assertEqual(rows[0]['source'], 'あ\n\nい')
        self.assertEqual(rows[0]['chars'], 2)


@unittest.skipUnless((ROOT / 'rom.z64').exists() and (DATA / 'manifest.json').exists(),
                     'Local ROM and extracted original data required')
class FullExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from srw64_native.catalog import source_catalog, text_headers
        sources, hashes, _ = source_catalog(ROOT, ROOT / 'rom.z64')
        headers = text_headers(ROOT, ROOT / 'rom.z64')

        def jsonl(name):
            return [json.loads(l) for l in (DATA / f'records/{name}.jsonl').read_text().splitlines() if l]
        index = json.loads((DATA / 'story/index.json').read_text())
        docs = [json.loads((DATA / s['file']).read_text()) for s in index['scenes']]
        locales = {'ja': {'entries': {}, 'ui': {'a.b': 'x'}}}
        cls.exporter = TextExporter(sources, hashes, headers, {n: jsonl(n) for n in ('actors', 'units', 'weapons')},
                                    docs, locales)
        cls.rows = cls.exporter.records()
        cls.by_key = {r['key']: r for r in cls.rows}
        cls.sources = sources

    def test_every_record_once_and_lossless(self):
        self.assertEqual(len(self.rows), len(self.sources))
        self.assertTrue(all(self.by_key[k]['source'] == v for k, v in self.sources.items()))

    def test_story_range_is_exactly_the_script_references(self):
        story = [r for r in self.rows if r['category'] in ('story.dialogue', 'story.choice')]
        self.assertEqual(len(story), 50975 - STORY_FIRST_ID)
        self.assertEqual(sum(r['category'] == 'story.choice' for r in story), 46)
        self.assertTrue(all(r['context']['occurrences'] for r in story))
        choice = self.by_key['base:t00_21815']
        self.assertEqual(choice['category'], 'story.choice')
        self.assertEqual(len(choice['choice_options']), 2)

    def test_names_link_back_to_their_records(self):
        self.assertEqual(self.by_key['base:t00_00527']['context']['unit_id'], 0)
        self.assertEqual(self.by_key['base:t00_02699']['context']['pure_name_key'], 'base:t00_01370')
        self.assertEqual(self.by_key['base:t00_04382']['context']['pair_key'], 'base:t00_04743')
        self.assertEqual(self.by_key['base:t00_00969']['context']['help_key'], 'base:t00_05150')

    def test_battle_speaker_comes_from_header(self):
        row = self.by_key['base:t00_05813']
        self.assertEqual(row['speaker_id'], 40)
        self.assertEqual(row['context']['speaker_id'], 40)
        self.assertTrue(self.by_key['base:t00_14248']['context'].get('combo_lead'))
