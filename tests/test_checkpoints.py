import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from srw64_native.checkpoints import CheckpointError, commit, recover, read_index
from srw64_native.coverage import locale_coverage
from srw64_native.presentation_settings import selected_locale

IDENTITY = {'baseline': 'srw64-jp-rev0', 'state_schema': 'srw64.original-sram-with-rng.v1', 'rules': {}}


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.members = {'sram.bin': bytes(0x8000), 'extensions.json': json.dumps({
            'schema': 'srw64.checkpoint-extensions.v1', 'rng_index': 520,
            'rng_table': '00' * 2084, 'read_state': {}}).encode()}
        self.metadata = {'node_verified': True, 'node': 'test-fixture'}

    def put(self):
        return commit(self.root, self.members, IDENTITY, self.metadata, retain=2)

    def test_complete_collection_and_rotation(self):
        first, second, third = self.put(), self.put(), self.put()
        entries = read_index(self.root)['generations']
        self.assertEqual([r['id'] for r in entries], [third, second])
        self.assertTrue((self.root / first).exists())
        manifest, members, rejected = recover(self.root, IDENTITY)
        self.assertEqual(members, self.members)
        self.assertEqual(rejected, [])
        self.assertEqual(manifest['identity'], IDENTITY)

    def test_each_corrupted_member_recovers_previous_whole_generation(self):
        for name in ['sram.bin', 'extensions.json', 'manifest.json']:
            with self.subTest(name=name):
                old = self.put()
                new = self.put()
                path = self.root / new / name
                raw = path.read_bytes()
                path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                manifest, members, rejected = recover(self.root, IDENTITY)
                self.assertEqual(members, self.members)
                self.assertEqual(rejected[0]['id'], new)
                self.assertEqual(read_index(self.root)['generations'][1]['id'], old)

    def test_failure_before_index_commit_preserves_prior_generation(self):
        old = self.put()
        previous = (self.root / 'index.json').read_bytes()
        with patch('srw64_native.checkpoints.os.replace', side_effect=OSError('disk full')):
            with self.assertRaises(OSError):
                self.put()
        self.assertEqual((self.root / 'index.json').read_bytes(), previous)
        self.assertEqual(read_index(self.root)['generations'][0]['id'], old)
        self.assertEqual(recover(self.root, IDENTITY)[1], self.members)

    def test_partial_member_write_does_not_publish_orphan(self):
        self.put()
        previous = (self.root / 'index.json').read_bytes()
        with patch('srw64_native.checkpoints.write_new', side_effect=OSError('interrupted')):
            with self.assertRaises(OSError):
                self.put()
        self.assertEqual((self.root / 'index.json').read_bytes(), previous)
        self.assertEqual(recover(self.root, IDENTITY)[1], self.members)

    def test_unverified_node_unknown_extensions_and_rules_are_blocked(self):
        self.metadata['node_verified'] = False
        with self.assertRaisesRegex(CheckpointError, 'unverified'):
            self.put()
        self.metadata['node_verified'] = True
        self.members['extensions.json'] = b'{}'
        with self.assertRaisesRegex(CheckpointError, 'extension'):
            self.put()
        identity = copy.deepcopy(IDENTITY)
        identity['rules']['balance'] = 1
        with self.assertRaisesRegex(CheckpointError, 'rules'):
            recover(self.root, identity)

    def test_corrupt_index_never_promotes_uncommitted_directories(self):
        self.put()
        for raw in ['{}', '[]', '{broken', '{"schema":"srw64.checkpoint-index.v1","generations":[null]}']:
            (self.root / 'index.json').write_text(raw)
            with self.assertRaisesRegex(CheckpointError, 'index'):
                recover(self.root, IDENTITY)

    def test_symlinked_member_is_rejected(self):
        self.put()
        new = self.put()
        target = self.root / 'outside.bin'
        target.write_bytes(self.members['sram.bin'])
        (self.root / new / 'sram.bin').unlink()
        (self.root / new / 'sram.bin').symlink_to(target)
        self.assertIn('Symlink', recover(self.root, IDENTITY)[2][0]['reason'])


class PresentationTests(unittest.TestCase):
    def test_saved_language_explicit_override_and_invalid_preferences(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'settings.json'
            registered = {'ja': 'ja.json', 'zh-Hans': 'zh.json'}
            self.assertEqual(selected_locale(path, registered, 'ja'), 'ja')
            path.write_text(json.dumps({'schema': 'srw64.presentation-settings.v1', 'locale': 'zh-Hans'}))
            self.assertEqual(selected_locale(path, registered, 'ja'), 'zh-Hans')
            self.assertEqual(selected_locale(path, registered, 'ja', 'ja'), 'ja')
            for raw in ['{}', '[]', 'null', '{"schema":"srw64.presentation-settings.v1","locale":[]}']:
                path.write_text(raw)
                with self.assertRaises(ValueError):
                    selected_locale(path, registered, 'ja')
            self.assertEqual(selected_locale(path, registered, 'ja', 'ja'), 'ja')
            with self.assertRaises(ValueError):
                selected_locale(path, registered, 'ja', 'fr')

    def test_coverage_does_not_count_fallback_or_drafts_as_reviewed(self):
        sources = {'base:t00_00001': 'a', 'base:t00_00002': 'b', 'base:t01_00001': 'c'}
        doc = {'locale': 'zh-Hans', 'ui': {'a': 'A'}, 'entries': [
            {'key': 'base:t00_00001'}, {'key': 'base:t01_00001', 'review_status': 'reviewed'}]}
        result = locale_coverage(doc, sources, {row['key']: 'translated' for row in doc['entries']}, {'a', 'b'})
        self.assertEqual(result['fallback_keys'], ['base:t00_00002'])
        self.assertEqual(result['reviewed_records'], 1)
        self.assertEqual(result['draft_records'], 1)
        self.assertEqual(result['ui']['fallback_keys'], ['b'])
        self.assertEqual(result['consumers']['original_menus'], 'not-adapted')


class StateComparisonTests(unittest.TestCase):
    def state(self):
        return {'schema': 'srw64.game-state-observation.v1', 'boundary': 'dialogue-fragment',
                'argument': 12, 'vi': 200, 'coverage_complete': False,
                'regions': {'game_rng': {'address': 0x800D49D0, 'size': 2, 'bytes': '0001'}}}

    def test_equal_partial_regions_are_not_full_equivalence(self):
        from srw64_native.state_compare import compare_observations
        a, b = self.state(), self.state()
        b['vi'] = 700
        result = compare_observations(a, b)
        self.assertTrue(result['observed_regions_equal'])
        self.assertFalse(result['equivalence_verified'])

    def test_rng_difference_is_never_masked_as_timing_noise(self):
        from srw64_native.state_compare import compare_observations
        a, b = self.state(), self.state()
        b['regions']['game_rng']['bytes'] = '0002'
        result = compare_observations(a, b)
        self.assertFalse(result['observed_regions_equal'])
        self.assertEqual(result['differences'][0]['first_differences'][0]['address'], 0x800D49D1)

    def test_mismatched_boundary_or_coverage_is_rejected(self):
        from srw64_native.state_compare import compare_observations
        for field, value in [('argument', 13), ('regions', {})]:
            a, b = self.state(), self.state()
            b[field] = value
            with self.assertRaises(ValueError):
                compare_observations(a, b)


class OriginalSaveTests(unittest.TestCase):
    def test_slot_checksum_and_tactical_uncovered_tail_are_explicit(self):
        from srw64_native.original_saves import candidate_sram, inspect_sram
        for tactical, size, slot in [(False, 0x1F00, 'intermission-1'), (True, 0x3AE0, 'tactical')]:
            data = candidate_sram(bytes(size), tactical=tactical)
            self.assertTrue(inspect_sram(data)['slots'][slot]['checksum_matches'])
            damaged = bytearray(data)
            offset = 0x3E10 if tactical else 0x10
            damaged[offset + 5] ^= 1
            self.assertFalse(inspect_sram(bytes(damaged))['slots'][slot]['checksum_matches'])
            if tactical:
                damaged = bytearray(data)
                damaged[offset + 0x3A00] ^= 1
                self.assertTrue(inspect_sram(bytes(damaged))['slots'][slot]['checksum_matches'])
                self.assertEqual(inspect_sram(data)['slots'][slot]['checksum_coverage_bytes'], 0x1F00)
