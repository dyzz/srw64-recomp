import copy
import json
from pathlib import Path
import struct
import unittest

from srw64_native.catalog import source_catalog, text_headers
from srw64_native.original_data import check_layout, extract_gameplay
from srw64_native.original_scripts import (Resolver, attach_script_catalog, bank_offset, decode_event,
                                          decode_position, emulate_skip_scan, event_lists, opcode_catalogs,
                                          pointer_table, verify_vm_tables)

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = json.loads((ROOT / 'config/data/original-jp-v1.json').read_text())
SPEC = LAYOUT['stage_scripts']


def words(*values: int) -> bytes:
    return bytes(10) + b''.join(struct.pack('>H', v) for v in values)


class ScriptSafetyTests(unittest.TestCase):
    resolver = Resolver({}, {}, {})

    def test_unknown_command_does_not_resynchronize_on_text_like_operand(self):
        raw = words(0x3D30, 0x3D3E, 0x4402, 0xFFFF)
        result = decode_event(raw, 0x100, SPEC, self.resolver)
        self.assertEqual(result['status'], 'unknown-opcode')
        self.assertFalse(result['instructions'])
        self.assertEqual(result['remainder_hex'], raw[10:].hex())
        self.assertEqual(result['stop_word'], 0x3D30)

    def test_truncated_operand_and_trailing_bytes_survive(self):
        result = decode_event(words(0x3D38), 0, SPEC, self.resolver)
        self.assertEqual(result['status'], 'truncated-operands')
        self.assertEqual(result['remainder_hex'], '3d38')
        result = decode_event(words(0xFFFF, 0x1234), 0, SPEC, self.resolver)
        self.assertEqual(result['status'], 'terminator-reached')
        self.assertEqual(result['remainder_hex'], '1234')
        self.assertIsNone(result['stop_word'])

    def test_null_handler_and_unreachable_slots_are_not_decoded_as_commands(self):
        for opcode in (0x3D76, 0x3D77):
            self.assertEqual(decode_event(words(opcode, 0xFFFF), 0, SPEC, self.resolver)['status'], 'unknown-opcode')
        result = decode_event(words(0x3D78, 0xFFFF), 0, SPEC, self.resolver)
        self.assertEqual(result['status'], 'terminator-reached')

    def test_condition_blocks_markers_and_depth(self):
        raw = words(0x3DD1, 0x3E03, 0x0005, 0x0001, 0x0000, 0x3E08, 0x0000, 0x3D3E, 0x0001, 0x3E1D, 0x3E1D, 0x3DD0, 0x3E1D, 0xFFFF)
        result = decode_event(raw, 0, SPEC, self.resolver)
        depths = [(f"{i['opcode']:04X}", i['depth'], i['section']) for i in result['instructions']]
        self.assertEqual(depths[:4], [('3DD1', 0, '3DD1'), ('3E03', 0, '3DD1'), ('3E08', 1, '3DD1'), ('3D3E', 2, '3DD1')])
        self.assertEqual(result['max_depth'], 2)
        self.assertEqual(len(result['blocks']), 2)
        self.assertEqual(result['unbalanced_block_ends'], 1)
        self.assertEqual(result['hazards'], [])
        self.assertEqual(result['instructions'][3]['fields'][0]['role'], 'text')

    def test_skip_scan_emulation_flags_operands_that_look_like_openers(self):
        # 3E08 0001 | 3D38 3E03 | 3E1D | 3D3E 0005 | FFFF: a counter operand equal to 3E03 fools the word scan.
        raw = words(0x3E08, 0x0001, 0x3D38, 0x3E03, 0x3E1D, 0x3D3E, 0x0005, 0xFFFF)
        result = decode_event(raw, 0, SPEC, self.resolver)
        self.assertEqual(result['status'], 'terminator-reached')
        self.assertEqual([h['kind'] for h in result['hazards']], ['skip-scan'])
        self.assertEqual(emulate_skip_scan(raw, 14, SPEC), {'landing': 24, 'reason': 'end'})
        # An opener closed by FFFF is the same landing for the engine and the structure.
        raw = words(0x3E08, 0x0001, 0x3D38, 0x0002, 0xFFFF)
        result = decode_event(raw, 0, SPEC, self.resolver)
        self.assertEqual(result['hazards'], [])
        self.assertEqual(result['blocks'][0]['closed_by'], 'end')

    def test_marker_scan_hazard_only_outside_the_common_section(self):
        safe = decode_event(words(0x3DD0, 0x3D39, 0xFFFF, 0xFFFF), 0, SPEC, self.resolver)
        self.assertEqual(safe['hazards'], [])
        self.assertEqual(safe['instructions'][1]['fields'][0]['meaning'], '停止音效（-1）')
        risky = decode_event(words(0x3DD2, 0x3D39, 0xFFFF, 0x3DD0, 0xFFFF), 0, SPEC, self.resolver)
        self.assertEqual([h['kind'] for h in risky['hazards']], ['marker-scan-control-word'])

    def test_speaker_header_rule_and_route_narrowing(self):
        actors = [{'key': f'base:actors:{i:04d}', 'label': f'actor{i}'} for i in range(40)]
        resolver = Resolver({'actors': actors}, {'base:t00_00007': 'x<END>', 'base:t00_00008': 'y<END>', 'base:t00_00009': 'z<END>'},
                            {'base:t00_00007': b'0240 0004', 'base:t00_00008': b'0250 0004', 'base:t00_00009': b'\x00\x01\x02\x03'})
        self.assertEqual(resolver.text(7)['speaker_key'], 'base:actors:0024')
        relative = resolver.text(8)
        self.assertEqual(relative['speaker_status'], 'route-relative')
        self.assertEqual([c['key'] for c in relative['speaker_candidates']], [f'base:actors:{i:04d}' for i in (25, 26, 27, 28)])
        self.assertEqual(resolver.text(9)['speaker_status'], 'unparsed')
        result = decode_event(words(0x3DD4, 0x3D3E, 0x0008, 0x3DD6, 0x3D3E, 0x0008, 0x3DD0, 0x3D3E, 0x0008, 0xFFFF), 0, SPEC, resolver)
        first, second, third = (i for i in result['instructions'] if i['kind'] == 'dialogue')
        self.assertEqual((first['speaker_key'], first['speaker_status']), ('base:actors:0028', 'route-resolved-by-section'))
        self.assertEqual([c['key'] for c in second['speaker_candidates']], ['base:actors:0027', 'base:actors:0028'])
        self.assertEqual(third['speaker_status'], 'route-relative')

    def test_position_and_special_operands(self):
        self.assertEqual(decode_position(0x0C15), {'x': 12, 'y': 21})
        self.assertEqual(decode_position(0x4103)['direction'], '上')
        result = decode_event(words(0x3D46, 0x01F6, 0x0001, 0x3D5A, 0x03E7, 0x0000, 0x03E7, 0x0FA0, 0xFFFF), 0, SPEC, self.resolver)
        self.assertEqual(result['instructions'][0]['fields'][0]['group'], 2)
        self.assertEqual(result['instructions'][1]['fields'][0]['meaning'], '无（999）')

    def test_pointer_and_list_boundaries(self):
        bank = {'vram': 0x1000, 'rom_offset': 0, 'byte_size': 64,
                'index_rom_offset': 32, 'index_count': 1}
        for pointer in [0xFFF, 0x1040, 0x1001]:
            with self.assertRaises(ValueError):
                bank_offset(bank, pointer)
        raw = bytearray(64)
        struct.pack_into('>II', raw, 32, 0x1010, 1)
        with self.assertRaisesRegex(ValueError, 'trailing zero'):
            pointer_table(raw, bank)
        struct.pack_into('>I', raw, 16, 0x1020)  # points beyond its payload
        with self.assertRaisesRegex(ValueError, 'outside its payload'):
            event_lists(raw, bank, [0x1010])


@unittest.skipUnless((ROOT / 'rom.z64').exists(), 'Local original ROM required')
class OriginalScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (ROOT / 'rom.z64').read_bytes()
        cls.layout = json.loads((ROOT / 'config/data/original-jp-v1.json').read_text())
        check_layout(cls.rom, cls.layout)
        cls.sources, _, _ = source_catalog(ROOT, ROOT / 'rom.z64')
        cls.headers = text_headers(ROOT, ROOT / 'rom.z64')
        cls.data = extract_gameplay(cls.rom, cls.layout, cls.sources)
        cls.audit = attach_script_catalog(cls.data, cls.rom, cls.layout, cls.sources, cls.headers)
        cls.events = {r['key']: r for r in cls.data['stage_events']}

    def test_loader_and_vm_machine_code(self):
        # Independent instructions: event PC = header+10, opcode range = 73, condition dispatch range = 30.
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x29888)[0], 0x24A5000A)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x2C3DC)[0], 0x2C820049)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x2C784)[0], 0x2C62001E)
        self.assertEqual(struct.unpack_from('>I', self.rom, 0x1E3D98 + 142*4)[0], 0)
        verify_vm_tables(self.rom, SPEC)
        for path, value in [(('commands', '3d38', 'handler_vram'), '0x800A00F1'), (('conditions', '3e03', 'handler_vram'), '0x800A2138'),
                            (('conditions', '3e13', 'kind'), 'opener'), (('event_types', '2', 'handler_vram'), '0x8009E6E4')]:
            tampered = copy.deepcopy(SPEC)
            target = tampered
            for step in path[:-1]:
                target = target[step]
            target[path[-1]] = value
            with self.assertRaises(ValueError):
                verify_vm_tables(self.rom, tampered)

    def test_aliases_and_counts_not_title_inference(self):
        self.assertEqual(self.audit['scene_index_slots'], 142)
        self.assertEqual(self.audit['unique_event_lists'], 131)
        self.assertEqual(self.audit['unique_events'], 1812)
        self.assertEqual(self.audit['event_references'], 1899)
        scenes = self.data['scenarios']
        self.assertEqual(scenes[0]['scenario']['shared_scene_indices'], [125, 126, 127])
        self.assertEqual(scenes[0]['scenario']['events'], scenes[125]['scenario']['events'])
        self.assertEqual(scenes[1]['scenario']['map_key'], 'base:stage_maps:0001')
        self.assertEqual(len(scenes[1]['scenario']['events']), 7)
        self.assertFalse(any(l['key'].startswith('candidate:') for s in scenes for l in s['links']))

    def test_every_event_decodes_to_its_terminator_and_reassembles(self):
        self.assertEqual(self.audit['decode_status'], {'terminator-reached': 1812})
        self.assertEqual(self.audit['stop_words'], {})
        self.assertEqual(self.audit['hazards'], {})
        self.assertEqual(self.audit['instructions'], 67160)
        self.assertEqual(self.audit['condition_blocks'], 2678)
        self.assertEqual(self.audit['trailing_bytes_after_end'], {0: 790, 2: 1022})
        for row in self.data['stage_events']:
            raw = bytes.fromhex(row['raw_hex']); ir = row['script']
            reconstructed = raw[:10] + b''.join(bytes.fromhex(i['raw_hex']) for i in ir['instructions']) + bytes.fromhex(ir['remainder_hex'])
            self.assertEqual(reconstructed, raw, row['key'])
            self.assertEqual(raw, self.rom[row['rom_offset']:row['rom_offset']+row['byte_size']])
            self.assertEqual(ir['instructions'][-1]['opcode'], 0xFFFF)
            self.assertLessEqual(len(bytes.fromhex(ir['remainder_hex'])), 2)

    def test_dialogue_speakers_and_first_stage_content(self):
        self.assertEqual(self.audit['dialogue_references'], 33582)
        self.assertEqual(self.audit['dialogue_speakers'], {'resolved': 30154, 'route-resolved-by-section': 2983, 'route-relative': 445})
        first = self.data['stage_events'][0]['script']['instructions']
        text = next(i for i in first if 'text_key' in i)
        self.assertEqual((text['rom_offset'], text['opcode'], text['text_key']), (0x19BF28, 0x3D3E, 'base:t00_17347'))
        self.assertEqual(text['text'], '「師匠……」<END>')
        intro = self.events['base:stage_events:0019c1b0']['script']
        self.assertEqual(intro['trigger']['name'], '开场事件（阶段 C1）')
        lines = [i for i in intro['instructions'] if i['kind'] == 'dialogue']
        self.assertEqual((lines[0]['text_key'], lines[0]['speaker_key']), ('base:t00_17410', 'base:actors:0024'))
        self.assertEqual(lines[1]['speaker_status'], 'route-relative')
        self.assertEqual([c['label'] for c in lines[1]['speaker_candidates']], ['アーク', 'セレイン', 'ブラッド', 'マナミ'])
        by_section = next(i for i in self.events['base:stage_events:0019d04c']['script']['instructions'] if i['offset'] == 112)
        self.assertEqual((by_section['speaker_key'], by_section['speaker_status']), ('base:actors:0028', 'route-resolved-by-section'))

    def test_actor_movement_uses_pilot_identity_and_preserves_positions(self):
        # Independent ROM instructions load unit->pilot, then its identity halfword.
        offset = 0xAB160 + 0x8020A10C - 0x801C2600
        self.assertEqual(struct.unpack_from('>2I', self.rom, offset), (0x8C420038, 0x84420002))
        # The movement completion function commits x/y to roster slot +4/+5.
        for vram, instruction in [(0x801CBF74, 0xA022E104), (0x801CC018, 0xA022E105)]:
            self.assertEqual(struct.unpack_from('>I', self.rom, 0xAB160 + vram - 0x801C2600)[0], instruction)
        moves = [i for i in self.events['base:stage_events:0019bf10']['script']['instructions']
                 if i['opcode'] == 0x3D3C]
        expected = [(298, 21, 8), (27, 25, 28), (27, 25, 18), (31, 23, 28), (31, 23, 19)]
        self.assertEqual(len(moves), len(expected))
        for move, (actor, x, y) in zip(moves, expected):
            identity, position = move['fields']
            self.assertEqual((identity['role'], identity['key']), ('actor', f'base:actors:{actor:04d}'))
            self.assertEqual((position['x'], position['y']), (x, y))
        opcode = next(i for i in self.data['script_opcodes'] if i['opcode']['value'] == 0x3D3C)
        self.assertIn('script_actor_movement', opcode['evidence'])
        self.assertIn('script_actor_movement_commit', opcode['evidence'])

    def test_triggers_slots_choices_and_scene_flow(self):
        scene = self.data['scenarios'][1]['scenario']
        self.assertEqual(scene['events_by_type']['7'], ['base:stage_events:0019c3bc', 'base:stage_events:0019c404', 'base:stage_events:0019c43c'])
        self.assertEqual(scene['next_scene_keys'], ['base:scenarios:0004'])
        self.assertEqual(self.audit['scene_flow_edges'], 148)
        trigger = self.events['base:stage_events:0019c3bc']['script']['trigger']
        self.assertEqual([f['value'] for f in trigger['fields']], [1, 8, 4, 0])
        self.assertEqual(trigger['fields'][2]['meaning'], '任意阶段')
        region = self.events['base:stage_events:001a0c38']['script']['trigger']
        self.assertEqual(region['name'], '区域到达触发（需 3D57 启用）')
        self.assertEqual((region['fields'][2]['start'], region['fields'][2]['span'], region['fields'][3]['start']), (18, 6, 21))
        armed = next(i for i in self.events['base:stage_events:0019c9e4']['script']['instructions'] if i['opcode'] == 0x3D52)
        self.assertEqual(armed['fields'][0]['scene_targets'], [{'scene': 2, 'key': 'base:stage_events:0019ca34'}])
        enabled = next(i for i in self.events['base:stage_events:001a0838']['script']['instructions'] if i['opcode'] == 0x3D57)
        self.assertEqual(enabled['fields'][0]['scene_targets'], [{'scene': 14, 'key': 'base:stage_events:001a0c38'}])
        choice = self.events['base:stage_events:0019ca34']['script']['instructions']
        index = next(n for n, i in enumerate(choice) if i['opcode'] == 0x3D44)
        self.assertEqual((choice[index]['fields'][1]['value'], choice[index + 1]['opcode']), (3, 0x3E10))

    def test_deployment_records_and_catalogs(self):
        self.assertEqual(self.audit['deployment_records'], 6223)
        self.assertEqual(self.audit['auxiliary_blocks_without_terminator'], 13)
        scene = self.data['scenarios'][1]['scenario']
        first = next(r for r in self.data['stage_deployments'] if r['key'] == scene['deployment_group_keys']['0'][0])
        self.assertEqual({k: first['deployment'][k] for k in ('group', 'x', 'y', 'unit', 'side', 'level_offset')},
                         {'group': 0, 'x': 6, 'y': 5, 'unit': 266, 'side': 1, 'level_offset': 1})
        self.assertIn('base:units:0266', [l['key'] for l in first['links']])
        for row in self.data['stage_deployments']:
            self.assertEqual(bytes.fromhex(row['raw_hex']), self.rom[row['rom_offset']:row['rom_offset'] + 28])
        catalogs = opcode_catalogs(self.rom, SPEC)
        self.assertEqual({k: len(v) for k, v in catalogs.items()},
                         {'script_opcodes': 73, 'script_conditions': 30, 'script_markers': 12, 'script_event_types': 15})
        commands = {r['opcode']['value']: r['opcode'] for r in self.data['script_opcodes']}
        self.assertEqual(commands[0x3D38]['handler_vram'], '0x800A00F0')
        self.assertIsNone(commands[0x3D76]['handler_vram'])
        self.assertEqual(commands[0x3D3F]['operand_words'], 1)
        self.assertEqual(commands[0x3D3D]['operand_words'], 5)
        conditions = {r['opcode']['value']: r['opcode'] for r in self.data['script_conditions']}
        self.assertEqual((conditions[0x3E03]['block'], conditions[0x3E03]['operand_words']), ('opener', 3))
        self.assertEqual((conditions[0x3E13]['block'], conditions[0x3E1D]['block']), ('statement', 'block-end'))
        self.assertEqual(self.data['script_markers'][4]['links'][0]['key'], 'base:actors:0028')


if __name__ == '__main__':
    unittest.main()
