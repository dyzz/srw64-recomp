"""Recovery must preserve old bytes and reject incomplete/tampered history."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, Mock

from srw64_native.save_history import (SaveHistoryError, SRAM_SIZE, digest,
    inspect_initial, inventory, select, stage_selection)

ROOT = Path(__file__).resolve().parents[1]


class SaveHistoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sessions = self.root / 'sessions'
        self.sessions.mkdir()
        self.data = bytes(range(256)) * 128
        self.seed = self.root / 'initial.sram'
        self.seed.write_bytes(self.data)
        self.identity = dict(game_id='srw64-jp-rev0', variant='jp', rom_sha256='a' * 64)

    def session(self, index: int) -> tuple[Path, Path]:
        run = self.sessions / f'20260912T1200{index:02d}.000000Z'
        save = run / 'runtime-data/saves/srw64-jp-rev0.bin'
        save.parent.mkdir(parents=True)
        save.write_bytes(self.data)
        report = run / 'report.json'
        report.write_text(json.dumps({'schema': 'srw64.recomp-native-host-probe.v1',
            'status': 'native-run-ended-before-VI-limit', 'exit_code': 0,
            'rom_variant': 'jp', 'rom_sha256': 'a' * 64,
            'final_save': {'size': SRAM_SIZE, 'sha256': digest(self.data)}}))
        return save, report

    def rows(self) -> list:
        return inventory(self.sessions, **self.identity)

    def initial(self):
        return inspect_initial(self.seed, digest(self.data))

    def test_recovers_older_when_newest_is_truncated_or_same_size_corrupted(self) -> None:
        old, _ = self.session(1)
        new, _ = self.session(2)
        for corrupt in (self.data[:100], b'!' + self.data[1:]):
            new.write_bytes(corrupt)
            chosen, skipped = select(self.rows(), self.initial())
            self.assertEqual(chosen.path, old)
            self.assertEqual(len(skipped), 1)
            self.assertEqual(new.read_bytes(), corrupt)
            self.assertEqual(old.read_bytes(), self.data)

    def test_report_and_rom_identity_are_required(self) -> None:
        _, report = self.session(1)
        original = json.loads(report.read_text())
        for changes in ({'status': 'incomplete'}, {'status': 'unknown'}, {'exit_code': -11},
                        {'exit_code': False}, {'rom_variant': 'model5600'}, {'rom_sha256': 'b'*64},
                        {'final_save': None}, {'comparison_fixture': {'sha256': 'a'*64}},
                        {'script_move_probe': 'target17'}, {'profile': {'profile': {'gameplay_mods': ['rules']}}}):
            report.write_text(json.dumps({**original, **changes}))
            self.assertFalse(self.rows()[0].accepted, changes)
        for raw in ('{broken', '[]'):
            report.write_text(raw)
            self.assertFalse(self.rows()[0].accepted)
        report.unlink()
        self.assertFalse(self.rows()[0].accepted)

    def test_explicit_restore_never_silently_chooses_other_progress(self) -> None:
        self.session(1)
        new, _ = self.session(2)
        new.write_bytes(b'bad')
        for requested in (new.parents[2].name, '../other', 'missing'):
            with self.assertRaises(SaveHistoryError):
                select(self.rows(), self.initial(), requested)

    def test_initial_fallback_and_no_acceptable_source(self) -> None:
        self.assertEqual(select([], self.initial())[0].session, 'initial')
        save, _ = self.session(1)
        save.unlink()
        chosen, skipped = select(self.rows(), self.initial())
        self.assertEqual(chosen.session, 'initial')
        self.assertEqual(len(skipped), 1)
        self.seed.write_bytes(b'bad')
        with self.assertRaises(SaveHistoryError): select(self.rows(), self.initial())

    def test_empty_sram_is_not_a_resume_candidate(self) -> None:
        for data in (bytes(SRAM_SIZE), b'\xff' * SRAM_SIZE):
            self.seed.write_bytes(data)
            self.assertFalse(inspect_initial(self.seed, digest(data)).accepted)

    def test_non_session_artifacts_are_ignored_and_symlink_rejected(self) -> None:
        save, _ = self.session(1)
        (self.sessions / (save.parents[2].name + '.content')).mkdir()
        (self.sessions / '20260912T120002.000000Z').symlink_to(save.parents[2], target_is_directory=True)
        rows = self.rows()
        self.assertEqual(len(rows), 2)
        self.assertFalse(rows[0].accepted)
        self.assertTrue(rows[1].accepted)

    def test_staging_freezes_bytes_and_rechecks_the_selected_digest(self) -> None:
        save, _ = self.session(1)
        chosen, skipped = select(self.rows(), self.initial())
        before = save.read_bytes()
        output = self.sessions / '20260912T130000.123456Z'
        staged = stage_selection(output, chosen, skipped)
        self.assertFalse(output.exists())
        self.assertEqual(staged.name, output.name + '.source.sram')
        self.assertEqual(staged.read_bytes(), before)
        self.assertEqual(save.read_bytes(), before)
        with self.assertRaises(FileExistsError): stage_selection(output, chosen, skipped)
        save.write_bytes(b'?' + before[1:])
        with self.assertRaises(SaveHistoryError):
            stage_selection(self.sessions / 'next', chosen, skipped)
        self.assertFalse((self.sessions / 'next.source.sram').exists())

    def launcher(self):
        config = self.root / 'config/recomp'
        config.mkdir(parents=True)
        (config / 'playtest.json').write_text(json.dumps({'schema': 'srw64.native-playtest.v1',
            'variant': 'jp', 'initial_save': 'initial.sram', 'initial_save_sha256': digest(self.data)}))
        (config / 'rom-variants.json').write_text(json.dumps({'variants': {'jp': self.identity | {'sha256': 'a'*64}}}))
        spec = importlib.util.spec_from_file_location('save_test_launcher', ROOT / 'tools/recomp/play_native.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = self.root
        return module

    def test_launcher_lists_without_starting_or_creating_play_directory(self) -> None:
        module = self.launcher()
        with patch('sys.argv', ['play_native.py', '--list-saves']), patch.object(module.subprocess, 'run') as run:
            with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(module.main(), 0)
            run.assert_not_called()
        self.assertFalse((self.root / 'build').exists())

    def test_launcher_passes_frozen_bytes_and_digest_to_probe(self) -> None:
        module = self.launcher()
        with patch('sys.argv', ['play_native.py', '--mute']), patch.object(module.subprocess, 'run', return_value=Mock(returncode=0)) as run:
            with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(module.main(), 0)
        command = run.call_args.args[0]
        staged = Path(command[command.index('--save-from')+1])
        self.assertEqual(staged.read_bytes(), self.data)
        self.assertEqual(command[command.index('--save-sha256')+1], digest(self.data))
        self.assertNotEqual(staged, self.seed)
        self.assertEqual(self.seed.read_bytes(), self.data)

    def test_explicit_new_game_does_not_require_any_save(self) -> None:
        module = self.launcher()
        self.seed.unlink()
        with patch('sys.argv', ['play_native.py', '--new-game', '--mute']), patch.object(module.subprocess, 'run', return_value=Mock(returncode=0)) as run:
            with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(module.main(), 0)
        self.assertNotIn('--save-from', run.call_args.args[0])

    def test_profile_launcher_uses_saved_language_and_keeps_cli_override(self) -> None:
        module = self.launcher()
        settings = self.root / 'build/recomp/profile-play/presentation.json'
        settings.parent.mkdir(parents=True)
        settings.write_text(json.dumps({'schema': 'srw64.presentation-settings.v1', 'locale': 'en'}))
        profile = {'locales': {'ja': 'ja.json', 'zh-Hans': 'zh.json', 'en': 'en.json'},
                   'presentation': {'locale': 'zh-Hans', 'images': 'original'}}
        for override, expected in [([], 'en'), (['--language', 'zh-Hans'], 'zh-Hans'), (['--language', 'ja'], 'ja')]:
            with patch('sys.argv', ['play_native.py', '--new-game', '--mute', '--profile', 'profile.json', *override]), \
                 patch('srw64_native.profile.load_profile', return_value=profile), \
                 patch.object(module.subprocess, 'run', return_value=Mock(returncode=0)) as run:
                with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(module.main(), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[command.index('--language')+1], expected)
            self.assertEqual(Path(command[command.index('--presentation-settings')+1]), settings)
            self.assertEqual(json.loads(settings.read_text())['locale'], 'en')
