import json
from pathlib import Path
import unittest

from srw64_native.catalog import source_catalog, text_headers
from srw64_native.original_data import check_layout, extract_gameplay
from srw64_native.original_scripts import attach_script_catalog
from srw64_native.original_story import (STORY_SCHEMA, StoryBuilder, display_text, trigger_summary, story_search_rows)

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = json.loads((ROOT / 'config/data/original-jp-v1.json').read_text())


class StoryTextTests(unittest.TestCase):
    def test_name_runs_collapse_to_one_placeholder_and_controls_survive(self):
        text = '「よろしく、<G:0126><G:0126><G:0126>くん<BR> <G:0124><G:0124>さん<STOP>おわり」<END>'
        self.assertEqual(display_text(text), '「よろしく、【主角名字】くん<BR> 【主角昵称】さん<STOP>おわり」')
        self.assertEqual(display_text('<G:012C><G:012C><END>'), '【主角机体名（8010F698，候选）】')
        self.assertEqual(display_text('<G:0100>x<END>'), '<G:0100>x')
        self.assertEqual(display_text('<G:0126><G:0126><G:012A><G:012A>'), '【主角名字】【搭档名字】')

    def test_trigger_summaries_use_decoded_fields(self):
        trigger = {'type': 7, 'name': 'x', 'fields': [
            {'name': '阵营选择', 'value': 1}, {'name': '数量上限', 'value': 8},
            {'name': '阶段', 'value': 4, 'meaning': '任意阶段'}, {'name': '门槛变量', 'value': 0, 'meaning': '无门槛'}]}
        self.assertEqual(trigger_summary(trigger), '敌方残存 ≤ 8 · 任意阶段')
        trigger = {'type': 8, 'name': 'x', 'fields': [
            {'name': '回合或模式', 'value': 255, 'meaning': '到达即触发'}, {'name': '目标', 'value': 300, 'label': 'ゲリラ'},
            {'name': 'x 范围', 'value': 186, 'start': 18, 'span': 6}, {'name': 'y 范围', 'value': 212, 'start': 21, 'span': 2}]}
        self.assertEqual(trigger_summary(trigger), 'ゲリラ 到达区域 x[18, 24) y[21, 23) · 到达即触发')


@unittest.skipUnless((ROOT / 'rom.z64').exists(), 'Local original ROM required')
class OriginalStoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rom = (ROOT / 'rom.z64').read_bytes()
        check_layout(rom, LAYOUT)
        cls.sources, _, _ = source_catalog(ROOT, ROOT / 'rom.z64')
        headers = text_headers(ROOT, ROOT / 'rom.z64')
        cls.data = extract_gameplay(rom, LAYOUT, cls.sources)
        attach_script_catalog(cls.data, rom, LAYOUT, cls.sources, headers)
        # Portraits are attached by the image extractor; the builder must read the actor thumbnail as-is.
        cls.data['actors'][24]['thumbnail'] = {'kind': 'portrait', 'thumbnail': 'images/portrait/0034-0334.png'}
        cls.index, cls.documents = StoryBuilder(cls.data, cls.sources, LAYOUT['stage_scripts']).build()

    def test_titles_follow_the_intermission_rule(self):
        self.assertEqual(self.data['scenarios'][1]['title'], {'key': 'base:t00_00282', 'text_id': 282, 'text': '出撃!スイームルグ',
                                                              'basis': self.data['scenarios'][1]['title']['basis']})
        self.assertEqual(self.data['scenarios'][1]['label'], '场景索引 001 · 出撃!スイームルグ')
        self.assertEqual(self.data['stages'][1]['confidence'], 'code-confirmed')
        self.assertIn('base:scenarios:0001', [l['key'] for l in self.data['stages'][1]['links']])
        self.assertEqual(self.data['stages'][142]['confidence'], 'candidate')
        self.assertEqual(self.index['scenes'][4]['title'], '怒りの甲児 魔神立つ!')
        self.assertEqual(sum(d['title'] is not None for d in self.documents), 142)

    def test_scene_document_keeps_event_order_phases_and_speakers(self):
        doc = self.documents[1]
        self.assertEqual(doc['schema'], STORY_SCHEMA)
        self.assertEqual([e['phase'] for e in doc['events']], ['opening', 'deployment', 'map', 'map', 'map', 'map', 'ending'])
        self.assertEqual(doc['events'][2]['trigger'], '敌方残存 ≤ 8 · 任意阶段')
        self.assertEqual(doc['next_scenes'], [{'scene': 4, 'title': '怒りの甲児 魔神立つ!'}])
        self.assertEqual([p['scene'] for p in self.documents[4]['previous_scenes']], [0, 1, 125, 126, 127])  # 125–127 share scene 0's script
        lines = [l for l in doc['events'][0]['lines'] if l['kind'] == 'dialogue']
        self.assertEqual((lines[0]['speaker']['label'], lines[0]['speaker']['portrait']), ('ローレンス', 'images/portrait/0034-0334.png'))
        self.assertEqual((lines[1]['speaker']['status'], lines[1]['speaker']['key'], lines[1]['speaker']['label']),
                         ('route-resolved-by-scene', 'base:actors:0028', 'マナミ（本话主角）'))
        self.assertEqual([c['label'] for c in lines[1]['speaker']['candidates']], ['アーク', 'セレイン', 'ブラッド', 'マナミ'])
        self.assertEqual(doc['protagonist']['marker'], '3DD4')
        self.assertIsNone(self.documents[4]['protagonist'])
        by_scene = [l for e in self.documents[4]['events'] for l in e['lines'] if l['kind'] == 'dialogue' and l['speaker']['status'] == 'route-resolved-by-scene']
        self.assertEqual(by_scene, [])  # only the four first stages fix the protagonist
        self.assertEqual(doc['counts']['dialogue'], 93)
        deployment = [l for l in doc['events'][1]['lines'] if l['kind'] == 'note']
        self.assertTrue(deployment[0]['text'].startswith('登场：组 0 · 戦闘獣ダンテ'))
        # Scenes sharing one script (0/125/126/127 …) each count their lines, so the per-scene sum exceeds the 33,582 unique references.
        per_scene = sum(e['dialogue_count'] for s in self.data['scenarios'] for e in s['scenario']['events'])
        self.assertEqual(sum(d['counts']['dialogue'] for d in self.documents), per_scene)
        self.assertGreater(per_scene, 33582)

    def test_choices_sections_and_structure_lines(self):
        doc = self.documents[2]
        choices = [l for e in doc['events'] for l in e['lines'] if l['kind'] == 'choice']
        self.assertEqual([o['display'] for o in choices[0]['options']], ['平原', '森', '林'])
        sections = {l['label'] for e in doc['events'] for l in e['lines'] if l['kind'] == 'section'}
        self.assertIn('アーク路线', sections)
        kinds = {l['kind'] for d in self.documents for e in d['events'] for l in e['lines']}
        self.assertEqual(kinds, {'dialogue', 'section', 'condition', 'block-end', 'statement', 'choice', 'note'})
        self.assertTrue(all('<END>' not in l['display'] for d in self.documents for e in d['events'] for l in e['lines'] if l['kind'] == 'dialogue'))

    def test_search_index_links_every_dialogue_to_its_exact_event_offset(self):
        rows = story_search_rows(self.documents)
        identities = set()
        docs = {d['scene']: d for d in self.documents}
        for scene, event_id, offset, text_id, speakers, display in rows:
            identity = (scene, event_id, offset)
            self.assertNotIn(identity, identities)
            identities.add(identity)
            event = next(e for e in docs[scene]['events'] if e['key'].endswith(':' + event_id))
            line = next(l for l in event['lines'] if l['offset'] == offset)
            self.assertEqual((line['text_id'], line['display']), (text_id, display))
            self.assertIn(line['speaker']['label'], speakers)
            for candidate in line['speaker'].get('candidates', []):
                self.assertIn(candidate['label'], speakers)
        self.assertEqual(len(rows), sum(d['counts']['dialogue'] for d in self.documents))


if __name__ == '__main__':
    unittest.main()
