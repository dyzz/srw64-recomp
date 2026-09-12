import copy
import json
from pathlib import Path
import struct
import unittest

from srw64_native.catalog import source_catalog
from srw64_native.original_data import extract_gameplay
from srw64_native.original_profiles import attach_profiles
from srw64_native.original_abilities import attach_ability_catalog, flag_value

ROOT = Path(__file__).resolve().parents[1]


class AbilityBoundsTests(unittest.TestCase):
    def test_flag_read_rejects_partial_record(self):
        with self.assertRaises(ValueError):
            flag_value(bytes(31), 28, 4)
        self.assertEqual(flag_value(bytes(28) + bytes.fromhex('80000080'), 28, 4), 0x80000080)


@unittest.skipUnless((ROOT / 'rom.z64').exists(), 'Local original ROM required')
class OriginalAbilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (ROOT / 'rom.z64').read_bytes()
        cls.layout = json.loads((ROOT / 'config/data/original-jp-v1.json').read_text())
        cls.sources, _, _ = source_catalog(ROOT, ROOT / 'rom.z64')
        cls.original = extract_gameplay(cls.rom, cls.layout, cls.sources)
        attach_profiles(cls.original, cls.sources)

    def setUp(self):
        self.data = copy.deepcopy(self.original)
        self.audit = attach_ability_catalog(self.data, self.rom, self.layout, self.sources)

    def test_original_display_table_and_loader_instructions(self):
        expected = [(2,1028),(64,1030),(256,1029),(16,1023),(4,1027),(8,1027),
                    (4096,1024),(8192,1025),(1,1026),(32,1020),(512,1019),(2048,1021),
                    (16384,1022),(131072,1100),(262144,1101),(1048576,1116)]
        self.assertEqual([struct.unpack_from('>IH', self.rom, 0x100cec + i*8) for i in range(16)], expected)
        # Independent opcodes: raw +1C -> runtime +28, raw +18 -> runtime +20.
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x31adc)[0], 0x8e42001c)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x31ae8)[0], 0xac22a238)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x31aac)[0], 0x92420018)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x31ab8)[0], 0xa022a230)
        # 10.0 / 20.0 constants in the HP branch; percentage flag = 1.
        offset = 0xab160 + 0x801fa560 - 0x801c2600
        self.assertEqual(struct.unpack_from('>I', self.rom, offset)[0], 0x3c064120)
        self.assertEqual(struct.unpack_from('>I', self.rom, offset+20)[0], 0x3c0641a0)

    def test_all_unit_flags_are_accounted_for_and_form_specific(self):
        self.assertEqual(len(self.data['unit_abilities']), 22)
        self.assertFalse(self.audit['unit_unknown_bits'])
        by_id = {r['key'].rsplit(':',1)[1]:r['definition'] for r in self.data['unit_abilities']}
        self.assertEqual(by_id['flag_00000004']['holder_count'], 10)
        self.assertEqual(by_id['flag_00000008']['holder_count'], 4)
        self.assertEqual(by_id['repair']['holder_count'], 10)
        self.assertEqual(by_id['supply']['holder_count'], 3)
        self.assertEqual(by_id['sword']['holder_count'], 147)
        self.assertEqual(by_id['shield']['holder_count'], 61)
        self.assertTrue(any(a['key'].endswith(':shield') for a in self.data['units'][216]['profile']['abilities']))
        self.assertFalse(any(a['key'].endswith(':shield') for a in self.data['units'][218]['profile']['abilities']))
        for unit in self.data['units']:
            raw = bytes.fromhex(unit['raw_hex'])
            for offset,size in [(28,4),(24,1)]:
                masks = [r['definition']['mask'] for r in self.data['unit_abilities']
                         if r['definition']['offset']==offset and any(m['key']==unit['key'] for m in r['definition']['members'])]
                self.assertEqual(sum(masks), flag_value(raw,offset,size), unit['key'])

    def test_pilot_holders_and_learning_are_not_conflated(self):
        self.assertEqual(len(self.data['pilot_skills']),7)
        self.assertFalse(self.audit['pilot_unknown_bits'])
        self.assertEqual(len(self.audit['actors_without_stats']),97)
        self.assertIn('base:actors:0284',self.audit['actors_without_thresholds'])
        by_key = {a['key']:a for a in self.data['actors']}
        for row in self.data['pilot_skills']:
            d = row['definition']
            self.assertEqual(d['holder_count'], d['learnable_count']+d['zero_threshold_count']+d['missing_threshold_count'])
            self.assertEqual(len(d['members']),len({m['key'] for m in d['members']}))
            for m in d['members']:
                actor = by_key[m['key']]
                skill = next(s for s in actor['profile']['skills'] if s['mask']==d['mask'])
                self.assertEqual(skill['ranks'],m['ranks'])
                self.assertEqual(skill['definition_key'],row['key'])
                self.assertIn(actor['key'],[l['key'] for l in row['links']])

    def test_unknown_bits_and_zero_thresholds_survive(self):
        unit = self.data['units'][0]
        raw=bytearray.fromhex(unit['raw_hex']);raw[28]|=128;raw[24]|=128;unit['raw_hex']=raw.hex()
        actor=self.data['actors'][28]
        actor['profile']['skills'][0]['ranks']=[None]*9
        audit=attach_ability_catalog(self.data,self.rom,self.layout,self.sources)
        self.assertEqual({a['offset']:a['bits'] for a in audit['unit_unknown_bits'] if a['key']==unit['key']}, {24:128,28:0x80000000})
        d=next(d['definition'] for d in self.data['pilot_skills'] if d['definition']['mask']==actor['profile']['skills'][0]['mask'])
        member=next(m for m in d['members'] if m['key']==actor['key'])
        self.assertEqual(member['max_rank'],0)
        self.assertIsNone(member['first_level'])

    def test_projection_keeps_source_bytes_and_is_idempotent(self):
        for cat in self.original:
            for a,b in zip(self.original[cat],self.data[cat]):
                for key in ('key','raw_hex','source_sha256','fields','weapon_list'):
                    self.assertEqual(a.get(key),b.get(key))
        before=copy.deepcopy(self.data)
        audit=attach_ability_catalog(self.data,self.rom,self.layout,self.sources)
        self.assertEqual(audit,self.audit)
        self.assertEqual(self.data,before)


if __name__ == '__main__':
    unittest.main()
