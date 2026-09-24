from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'src'))
from models import build_native_models as models

# load_000A7EC0 (world-map overlay): VRAM 801C5670 is ROM 0xAAF30.
def overlay(vram):
    return 0xAAF30 + vram - 0x801C5670


class NativeModelPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT/'rom.z64').exists():
            raise unittest.SkipTest('Local original ROM required for asset identity checks')
        cls.rom = (ROOT/'rom.z64').read_bytes()
        from srw64_rom.resources import ResourceTable
        cls.resource, _ = ResourceTable(cls.rom).extract(5591)

    def test_ra_cailum_is_world_map_vehicle_14(self):
        table = [struct.unpack_from('>H', self.rom, overlay(0x801C5670) + 2*i)[0] for i in range(25)]
        vehicles = [struct.unpack_from('>h', self.rom, overlay(0x801C5644) + 2*i)[0] for i in range(16)]
        pairs, offset = {}, overlay(0x801C560C)
        while (pair := struct.unpack_from('>hh', self.rom, offset))[0] != -1:
            pairs[pair[0]] = pair[1]; offset += 4
        self.assertEqual(vehicles[15], -1)               # why 3D72 takes its parameter mod 15
        self.assertEqual(table[vehicles[14]], 5591)      # 3D72 14
        self.assertEqual(table[pairs[69]], 5591)          # unit 69 ラー・カイラム, the default via ブライト
        self.assertEqual(table[15], 5600)                 # the story marker in the same table

    def test_triangle_commands_cover_the_whole_model(self):
        commands = models.triangle_commands(self.resource)
        self.assertEqual((len(commands), commands[0]), (105, 0x10A8))
        self.assertTrue(all(self.resource[c] in (0x05, 0x06) for c in commands))
        triangles = sum(1 if self.resource[c] == 0x05 else 2 for c in commands)
        self.assertEqual(triangles, 196)

    def test_pack_round_trip_and_drift(self):
        mesh = {'positions': [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]],
                'normals': [[0, 0, 1], [0, 0, 1], [0, 1, 0], [1, 0, 0]],
                'colors': [[255, 255, 255, 255]] * 4,
                'faces': [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]]}
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'mesh.json'
            source.write_text(json.dumps(mesh))
            ra_cailum = next(m for m in models.MODELS if m['resource_id'] == 5591)
            table = [{**ra_cailum, 'mesh': source}]
            with mock.patch.object(models, 'MODELS', table):
                report = models.build(Path(folder)/'pack')
                self.assertEqual(report['models'][0]['triangles'], 4)
                reference = (Path(folder)/'pack/5591.reference.bin').read_bytes()
                self.assertEqual(models.rdram_image(reference), self.resource)
                vertices = Path(folder)/'pack/5591.vertices.bin'
                self.assertEqual(len(vertices.read_bytes()), 4*models.STRIDE)
                data = bytearray(vertices.read_bytes()); data[0] ^= 1
                vertices.write_bytes(data)
                with self.assertRaisesRegex(ValueError, 'asset drift'):
                    models.validate(Path(folder)/'pack')
                source.write_text(json.dumps({**mesh, 'faces': [[0, 1, 4]]}))
                with self.assertRaisesRegex(ValueError, 'invalid face'):
                    models.build(Path(folder)/'pack')


    def test_plated_model_packs_its_plate_per_locale(self):
        if not models.FONT.exists():
            self.skipTest('fonts not prepared (tools/content/prepare_fonts.py)')
        mesh = {'positions': [[0, 0, 0], [1, 0, 0], [0, 1, 0]], 'normals': [[0, 0, 1]] * 3,
                'colors': [[255, 255, 255, 255]] * 3, 'faces': [[0, 1, 2]]}
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'mesh.json'
            source.write_text(json.dumps(mesh))
            libra = next(m for m in models.MODELS if m['resource_id'] == 5590)
            with mock.patch.object(models, 'MODELS', [{**libra, 'mesh': source}]):
                models.build(Path(folder)/'pack')
                entry = json.loads((Path(folder)/'pack/manifest.json').read_text())['models'][0]
                self.assertEqual(len(entry['plate']['triangle_commands']), 8)   # frame and two lettering quads
                self.assertFalse(set(entry['plate']['triangle_commands']) & set(entry['triangle_commands']))
                self.assertEqual(entry['plate']['text'], {'ja': 'リーブラ', 'zh-Hans': '天秤座', 'en': 'Libra'})
                self.assertEqual(sorted(entry['plate']['textures']), sorted(models.PLATE_LOCALES))

    def test_space_region_plates_pack_without_a_mesh(self):
        if not models.FONT.exists():
            self.skipTest('fonts not prepared (tools/content/prepare_fonts.py)')
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(models, 'MODELS', []):
            report = models.build(Path(folder)/'pack')
            self.assertEqual(report['plates'], ['Space region: Side 3', 'Space region: Side 2', 'Space region: Side 5',
                                                'Space region: Side 7', 'Space region: Side 1', 'Space region: Side 6',
                                                'Space region: Sweetwater'])
            plates = json.loads((Path(folder)/'pack/manifest.json').read_text())['plates']
            self.assertEqual(plates[-1]['text'], {'ja': 'スウィートウォーター', 'zh-Hans': '甘泉', 'en': 'Sweetwater'})
            self.assertEqual(plates[4]['text'], {'ja': 'サイド1', 'zh-Hans': 'Side 1', 'en': 'Side 1'})
            commands = [c for p in plates for c in p['triangle_commands']]
            self.assertEqual((len(commands), len(set(commands))), (56, 56))   # 8 quads per board, none shared
            texture = Path(folder)/'pack'/plates[0]['textures']['zh-Hans']
            texture.write_bytes(texture.read_bytes() + b'\0')
            with self.assertRaisesRegex(ValueError, 'plate asset drift'):
                models.validate(Path(folder)/'pack')


class NamePlateTests(unittest.TestCase):
    def test_plate_names_come_from_the_term_tables(self):
        self.assertEqual(models.plate_names('リーブラ'), {'zh-Hans': '天秤座', 'en': 'Libra'})
        self.assertEqual(models.plate_names('アクシズ'), {'zh-Hans': '阿克西斯', 'en': 'Axis'})
        self.assertEqual(models.plate_names('フィフス・ルナ')['en'], 'Fifth Luna')  # story terms only
        with self.assertRaisesRegex(ValueError, 'no term'):
            models.plate_names('存在しない名前')

    def test_plate_keeps_the_original_board(self):
        if not models.FONT.exists():
            self.skipTest('fonts not prepared (tools/content/prepare_fonts.py)')
        plate = models.render_plate('La Vie en Rose')
        k = models.PLATE_SCALE
        self.assertEqual(plate.size, (200 * k, 30 * k))
        self.assertEqual(plate.getpixel((k // 2, k // 2)), (0, 170, 0, 255))        # frame
        self.assertEqual(plate.getpixel((3 * k, 3 * k)), (0, 0, 0, 255))            # board
        self.assertTrue(any(plate.getpixel((x, 15 * k))[0] > 200 for x in range(0, 200 * k, 4)))  # lettering


if __name__ == '__main__':
    unittest.main()
