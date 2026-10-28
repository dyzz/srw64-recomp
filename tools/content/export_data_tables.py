"""Tables for number MOD authors (docs/guide/data-mods/*.csv).

  units.csv    every unit by number (data/units.json): names, stats, terrain ranks,
               size and special abilities
  pilots.csv   every pilot by number (data/pilots.json): names, ability record, the
               pilots sharing that record, stats, terrain ranks, skills and spirits
  weapons.csv  every weapon by number (data/weapons.json): names, power, range, hit,
               ammo, EN, morale, critical, terrain ranks and the units carrying it

and web/src/data/data-mods.json, the same for the website's ID table
(web/src/pages/[lang]/mods/ids.astro). The records and what the game does with them are
in src/host/mod_data.hpp and docs/design/mod-packages.md §5; names come from the text
export (assets/text-export), as in export_audio_tables.py.

    .venv/bin/python tools/content/export_data_tables.py
"""
from __future__ import annotations

from collections import defaultdict
import csv
import json
from pathlib import Path
import struct

from export_audio_tables import plain, LOCALES, UNIT_NAMES, ACTOR_NAMES

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/guide/data-mods'
WEB = ROOT / 'web/src/data/data-mods.json'
# mod_data.hpp
UNIT_RECORDS, UNIT_SIZE, UNITS, UNIT_TERRAIN = 0x71B80, 0x24, 363, 0x0E
UNIT_ICONS = 0x100D78                                        # u16 map icon resource per unit (layout images.units)
PILOT_MAP, PILOTS = 0x800CA9C4 - 0x80075610, 360
PILOT_RECORDS, PILOT_SIZE, PILOT_RECORD_COUNT, PILOT_TERRAIN = 0x7A1A0, 0x10, 257, 0x07
WEAPON_RECORDS, WEAPON_SIZE, WEAPONS, WEAPON_NAMES = 0x74E90, 0x10, 1329, 1370
WEAPON_LISTS, WEAPON_ENTRY = 0x7E210, 12
THRESHOLDS, THRESHOLD_SIZE = 0x7B1B0, 30
SPIRIT_MAP, SPIRIT_RECORDS, SPIRIT_SIZE = 0x800CA6F4 - 0x80075610, 0x7CFB0, 12
RANKS = '-DCBA'
CHANNELS = ('air', 'land', 'sea', 'space')
# mod_data.hpp's keys.
ABILITIES = (('beam_coat', 0x200), ('i_field', 0x20), ('planet_defensor', 0x800), ('aura_barrier', 0x4000),
             ('afterimage', 0x10), ('mach_special', 0x1000), ('true_mach_special', 0x2000), ('god_shadow', 0x1),
             ('getter_vision', 0x20000), ('shungensoku', 0x40000), ('hyper_jammer', 0x100000),
             ('hp_regen_10', 0x4), ('hp_regen_20', 0x8))
SIZES = ('SS', 'S', 'M', 'L', 'LL')
SKILLS = (('potential', 0x04, 0), ('newtype', 0x08, 0), ('enhanced', 0x10, 0), ('holy_warrior', 0x20, 0),
          ('esp', 0x40, 0), ('parry', 0x01, 1), ('shield_defense', 0x02, 2))
SPIRITS = ('self_destruct', 'scout', 'accel', 'mercy', 'focus', 'alert', 'guts', 'strike', 'trust', 'wall', 'cover',
           'valor', 'spirit', 'great_guts', 'luck', 'drain', 'effort', 'soul', 'stealth', 'awaken', 'refuel', 'provoke',
           'encourage', 'disrupt', 'friendship', 'love', 'reactivate', 'miracle', 'prayer', 'revive')


def write(name: str, header: list[str], rows: list[list]) -> None:
    with (OUT / name).open('w', encoding='utf-8', newline='') as f:
        out = csv.writer(f, lineterminator='\n')
        out.writerow(header)
        out.writerows(rows)
    print(f'{name}: {len(rows)} rows')


def main() -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    s16 = lambda a: struct.unpack_from('>h', rom, a)[0]
    ranks = lambda at: [RANKS[rom[at + n]] for n in range(4)]
    records = {}
    with (ROOT / 'assets/text-export/records.jsonl').open(encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            if r.get('table') == 0:
                records[r['id']] = r
    def name(record: int) -> list[str]:
        r = records[record]
        existing = r.get('existing', {})
        return [plain(r['display'])] + [plain(existing.get(l, '')) for l in LOCALES]
    OUT.mkdir(parents=True, exist_ok=True)

    u8 = lambda a: rom[a]
    s8 = lambda a: struct.unpack_from('>b', rom, a)[0]
    u16 = lambda a: struct.unpack_from('>H', rom, a)[0]
    u32 = lambda a: struct.unpack_from('>I', rom, a)[0]

    def unit_row(u: int) -> dict:
        at = UNIT_RECORDS + u * UNIT_SIZE
        size = next((SIZES[b] for b in range(5) if u8(at + 4) & (1 << b)), '')
        bits = u32(at + 0x1C)
        return {'hp': u16(at), 'en': u16(at + 2), 'move': u8(at + 6), 'mobility': u16(at + 8), 'armor': u16(at + 0xA),
                'limit': u16(at + 0xC), 'repair_cost': u16(at + 0x14), 'upgrade_cap': u8(at + 0x20), 'size': size,
                'shield': bool(u8(at + 0x18) & 2), 'sword': bool(u8(at + 0x18) & 1), 'parts_slots': u8(at + 0x19),
                'abilities': [k for k, m in ABILITIES if bits & m]}
    units = [(u, name(UNIT_NAMES + u), ranks(UNIT_RECORDS + u * UNIT_SIZE + UNIT_TERRAIN), unit_row(u)) for u in range(UNITS)]
    # The map icon each unit draws (layout images.units) and the way it looks, for icon MODs
    # (src/host/native_icon.cpp; content/art/unit-icon-facing.json).
    facing = json.loads((ROOT / 'content/art/unit-icon-facing.json').read_text(encoding='utf-8'))['icons']
    icons = {u: {'icon': u16(UNIT_ICONS + 2 * u), 'icon_facing': facing.get(str(u16(UNIT_ICONS + 2 * u)), {}).get('facing', 'front')}
             for u in range(UNITS)}
    record_of = {a: s16(PILOT_MAP + 2 * a) for a in range(PILOTS)}
    on_record = defaultdict(list)
    for a, r in record_of.items():
        if 0 <= r < PILOT_RECORD_COUNT:
            on_record[r].append(a)

    def pilot_row(a: int, r: int) -> dict:
        at = PILOT_RECORDS + r * PILOT_SIZE
        row = dict(zip(('melee', 'ranged', 'evade', 'hit', 'reaction', 'skill'), (u8(at + n) for n in range(1, 7))))
        row['sp'] = u8(at + 0xC)
        row['double_move'] = u8(at + 0xE)
        skills = {}
        first = False
        for key, bit, line in SKILLS:
            if not u8(at + 0xF) & bit or (line == 0 and first):
                continue
            first = first or line == 0
            levels = sorted(v for v in rom[THRESHOLDS + r * THRESHOLD_SIZE + line * 10:][:9] if v)
            if levels:
                skills[key] = levels
        row['skills'] = skills
        sr = s16(SPIRIT_MAP + 2 * a)
        row['spirits'] = [[SPIRITS[u8(SPIRIT_RECORDS + sr * SPIRIT_SIZE + n * 2 + 1)], u8(SPIRIT_RECORDS + sr * SPIRIT_SIZE + n * 2)]
                          for n in range(6) if sr >= 0 and 0 < u8(SPIRIT_RECORDS + sr * SPIRIT_SIZE + n * 2) < 100
                          and u8(SPIRIT_RECORDS + sr * SPIRIT_SIZE + n * 2 + 1) < 30]
        return row
    pilots = [(a, name(ACTOR_NAMES + a), r, [b for b in on_record[r] if b != a],
               ranks(PILOT_RECORDS + r * PILOT_SIZE + PILOT_TERRAIN), pilot_row(a, r))
              for a, r in record_of.items() if 0 <= r < PILOT_RECORD_COUNT]

    # Which units carry each weapon (the entries of a unit's list that name it among their forms).
    carried = defaultdict(list)
    for u in range(UNITS):
        at = WEAPON_LISTS + u32(WEAPON_LISTS + u * 4)
        while u16(at) != 0xFFFF:
            n = u16(at)
            if n < WEAPONS and u in [u16(at + 2 * k) for k in range(1, 6)] and u not in carried[n]:
                carried[n].append(u)
            at += WEAPON_ENTRY

    def weapon_row(n: int) -> dict:
        at = WEAPON_RECORDS + n * WEAPON_SIZE
        return {'power': u8(at + 1) * 100, 'range_min': u8(at + 2), 'range_max': u8(at + 3), 'hit': s8(at + 4),
                'ammo': s8(at + 5), 'en': u8(at + 6), 'morale': u8(at + 7), 'critical': s8(at + 0xD)}
    weapons = [(n, name(WEAPON_NAMES + n), ranks(WEAPON_RECORDS + n * WEAPON_SIZE + 9), weapon_row(n), carried[n]) for n in range(WEAPONS)]

    unit_stats = ('hp', 'en', 'move', 'mobility', 'armor', 'limit', 'repair_cost', 'upgrade_cap', 'size', 'parts_slots')
    write('units.csv', ['unit', 'name_ja', 'name_zh', 'name_en', *CHANNELS, *unit_stats, 'shield', 'sword', 'abilities'],
          [[u, *n, *t, *(d[k] for k in unit_stats), int(d['shield']), int(d['sword']), ' '.join(d['abilities'])] for u, n, t, d in units])
    pilot_stats = ('melee', 'ranged', 'evade', 'hit', 'reaction', 'skill', 'sp', 'double_move')
    write('pilots.csv', ['pilot', 'name_ja', 'name_zh', 'name_en', 'record', 'shares_record_with', *CHANNELS, *pilot_stats, 'skills', 'spirits'],
          [[a, *n, r, ' '.join(map(str, shared)), *t, *(d[k] for k in pilot_stats),
            ' '.join(f"{k}:{'/'.join(map(str, v))}" for k, v in d['skills'].items()), ' '.join(f'{k}:{l}' for k, l in d['spirits'])]
           for a, n, r, shared, t, d in pilots])
    weapon_stats = ('power', 'range_min', 'range_max', 'hit', 'ammo', 'en', 'morale', 'critical')
    write('weapons.csv', ['weapon', 'name_ja', 'name_zh', 'name_en', *CHANNELS, *weapon_stats, 'units'],
          [[n, *nm, *t, *(d[k] for k in weapon_stats), ' '.join(map(str, c))] for n, nm, t, d, c in weapons])

    def names3(n: list[str]) -> dict[str, str]:
        ja, zh, en = n
        return {'ja': ja, 'zh': zh or ja, 'en': en or ja}
    web = {
        'schema': 'srw64.web-data-mods.v2',
        'units': [{'id': u, 'name': names3(n), 'terrain': ''.join(t), **d, **icons[u]} for u, n, t, d in units if n[0]],
        'pilots': [{'id': a, 'name': names3(n), 'terrain': ''.join(t), 'shared': shared, **d}
                   for a, n, r, shared, t, d in pilots if n[0]],
        'weapons': [{'id': w, 'name': names3(n), 'terrain': ''.join(t), 'units': c, **d}
                    for w, n, t, d, c in weapons if n[0]],
    }
    WEB.write_text(json.dumps(web, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'{WEB.relative_to(ROOT)}: {len(web["units"])} units, {len(web["pilots"])} pilots, {len(web["weapons"])} weapons')


if __name__ == '__main__':
    main()
