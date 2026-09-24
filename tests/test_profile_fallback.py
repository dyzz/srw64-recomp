"""Presentation fallback must not discard locale data or accept damaged HD art."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from srw64_native.catalog import sha
from srw64_native.name_assets import prepare_name_assets
from srw64_native.profile import prepare_profile, UI_KEYS


class ProfileFallbackTests(unittest.TestCase):
    def setUp(self):
        battle = patch("srw64_native.battle_assets.prepare_battle_assets", return_value={"units": {}, "portraits": {}})
        battle.start()
        self.addCleanup(battle.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.rom = self.root / 'rom.z64'
        self.rom.write_bytes(b'fixture')
        for locale in ('ja', 'zh-Hans', 'en'):
            (self.root / f'{locale}.json').write_text(json.dumps({
                'schema': 'srw64.locale.v1', 'locale': locale, 'source_locale': 'ja',
                'font': f'font-{locale}', 'entries': [], 'ui': {key: locale for key in UI_KEYS}}))
        self.profile = {'presentation': {'locale': 'zh-Hans', 'images': 'original', 'font_size': 13},
                        'locales': {'ja': 'ja.json', 'zh-Hans': 'zh-Hans.json', 'en': 'en.json'}, 'art_pack': 'missing.json'}

    def prepare(self, name_mock):
        with patch('srw64_native.profile.source_catalog', return_value=({}, {}, {})), \
             patch('srw64_native.name_assets.prepare_name_assets', name_mock):
            return prepare_profile(self.root, self.profile, self.rom, self.root / 'out')

    def test_original_missing_art_retains_each_language_and_font(self):
        for locale in ('ja', 'zh-Hans', 'en'):
            with self.subTest(locale=locale), tempfile.TemporaryDirectory() as tmp:
                from unittest.mock import Mock
                names = Mock(return_value={'portraits': {}})
                self.profile['presentation']['locale'] = locale
                with patch('srw64_native.profile.source_catalog', return_value=({}, {}, {})), \
                     patch('srw64_native.name_assets.prepare_name_assets', names):
                    result = prepare_profile(self.root, self.profile, self.rom, Path(tmp) / 'out')
                self.assertFalse(result['hd_available'])
                self.assertIsNone(result['art'])
                self.assertIn('missing.json', result['hd_unavailable_reason'])
                self.assertIsNone(names.call_args.kwargs.get('hd_portrait'))
                data = json.loads(Path(result['dialogue']['path']).read_text())
                self.assertEqual(data['config']['locale'], locale)
                self.assertEqual(data['config']['font'], f'font-{locale}')
                self.assertEqual(data['ui']['name_title'], locale)
                self.assertEqual(set(data['locale_catalogs']), {'ja', 'zh-Hans', 'en'})
                for target in ('ja', 'zh-Hans', 'en'):
                    bundle = data['locale_catalogs'][target]
                    self.assertEqual(bundle['config']['locale'], target)
                    self.assertEqual(bundle['config']['font'], f'font-{target}')
                    self.assertEqual(bundle['ui']['name_title'], target)
                    self.assertEqual(bundle['catalog_sha256'], sha((self.root / f'{target}.json').read_bytes()))

    def test_explicit_hd_missing_art_fails(self):
        from unittest.mock import Mock
        self.profile['presentation']['images'] = 'hd'
        names = Mock()
        with self.assertRaises(FileNotFoundError):
            self.prepare(names)
        names.assert_not_called()

    def test_missing_portrait_disables_whole_hd_toggle(self):
        from unittest.mock import Mock
        (self.root / 'missing.json').write_text('{}')
        names = Mock(side_effect=[FileNotFoundError(2, 'missing', 'portrait.png'), {'portraits': {}}])
        with patch('srw64_native.profile.compile_art', return_value={'path': 'compiled'}):
            result = self.prepare(names)
        self.assertFalse(result['hd_available'])
        self.assertIsNone(result['art'])
        self.assertIn('portrait.png', result['hd_unavailable_reason'])
        self.assertIsNone(names.call_args.kwargs.get('hd_portrait'))

    def test_intact_art_keeps_live_toggle_in_original(self):
        from unittest.mock import Mock
        (self.root / 'missing.json').write_text('{}')
        with patch('srw64_native.profile.compile_art', return_value={'path': 'compiled'}):
            result = self.prepare(Mock(return_value={'portraits': {}}))
        self.assertTrue(result['hd_available'])
        self.assertIsNone(result['hd_unavailable_reason'])
        self.assertEqual(result['art'], {'path': 'compiled'})

    def test_invalid_art_does_not_silently_fall_back(self):
        from unittest.mock import Mock
        (self.root / 'missing.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'language-neutral'):
            self.prepare(Mock())


class NamePortraitFallbackTests(unittest.TestCase):
    def test_rom_portraits_do_not_need_hd_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rom = bytearray(0x110000)
            faces = (27, 28, 25, 26, 31, 32, 29, 30)
            struct.pack_into('>8H', rom, 0x1090A0 + 0x801C6BF0 - 0x801C2600, *faces)
            for face in faces + (41, 230, 133, 131, 132, 152, 151, 153):
                struct.pack_into('>2H', rom, 0x84220 + 4*face, 33, 34)
            pixels = struct.pack('>4H', 15, 96, 96, 0) + bytes(96*96)
            palette = bytes.fromhex('0003008000000000ffff')
            with patch('srw64_native.name_assets.ResourceTable') as table:
                table.return_value.extract.side_effect = lambda index: ((pixels if index == 33 else palette), None)
                result = prepare_name_assets(bytes(rom), root / 'out')
            self.assertIsNone(result['source_sha256'])
            self.assertEqual(len(result['portraits']), 16)
            self.assertEqual(result['link_faces'], [[41, 230], [133, 131, 132], [152, 151, 153]])
            for row in result['portraits'].values():
                self.assertNotIn('hd', row)
                self.assertEqual(sha(Path(row['original']).read_bytes()), row['original_sha256'])

    def test_whole_hd_portraits_come_from_the_lookup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rom = bytearray(0x110000)
            faces = (27, 28, 25, 26, 31, 32, 29, 30)
            struct.pack_into('>8H', rom, 0x1090A0 + 0x801C6BF0 - 0x801C2600, *faces)
            for face in faces + (41, 230, 133, 131, 132, 152, 151, 153):
                struct.pack_into('>2H', rom, 0x84220 + 4*face, 33 if face == 27 else 35, 34)
            pixels = struct.pack('>4H', 15, 96, 96, 0) + bytes(96*96)
            palette = bytes.fromhex('0003008000000000ffff')
            lookup = lambda image, pal: 'portraits/33-34.png' if (image, pal) == (33, 34) else None
            with patch('srw64_native.name_assets.ResourceTable') as table:
                table.return_value.extract.side_effect = lambda index: ((palette if index == 34 else pixels), None)
                result = prepare_name_assets(bytes(rom), root / 'out', hd_portrait=lookup)
            self.assertEqual(result['portraits']['27']['hd'], 'portraits/33-34.png')
            self.assertEqual([k for k, row in result['portraits'].items() if 'hd' in row], ['27'])
