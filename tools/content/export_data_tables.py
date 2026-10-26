"""Tables for number MOD authors (docs/guide/data-mods/*.csv).

  units.csv   every unit by number (data/units.json): its names and terrain ranks
  pilots.csv  every pilot by number (data/pilots.json): names, ability record, the
              pilots sharing that record, and terrain ranks

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
PILOT_MAP, PILOTS = 0x800CA9C4 - 0x80075610, 360
PILOT_RECORDS, PILOT_SIZE, PILOT_RECORD_COUNT, PILOT_TERRAIN = 0x7A1A0, 0x10, 257, 0x07
RANKS = '-DCBA'
CHANNELS = ('air', 'land', 'sea', 'space')


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

    units = [(u, name(UNIT_NAMES + u), ranks(UNIT_RECORDS + u * UNIT_SIZE + UNIT_TERRAIN)) for u in range(UNITS)]
    record_of = {a: s16(PILOT_MAP + 2 * a) for a in range(PILOTS)}
    on_record = defaultdict(list)
    for a, r in record_of.items():
        if 0 <= r < PILOT_RECORD_COUNT:
            on_record[r].append(a)
    pilots = [(a, name(ACTOR_NAMES + a), r, [b for b in on_record[r] if b != a],
               ranks(PILOT_RECORDS + r * PILOT_SIZE + PILOT_TERRAIN))
              for a, r in record_of.items() if 0 <= r < PILOT_RECORD_COUNT]

    write('units.csv', ['unit', 'name_ja', 'name_zh', 'name_en', *CHANNELS],
          [[u, *n, *t] for u, n, t in units])
    write('pilots.csv', ['pilot', 'name_ja', 'name_zh', 'name_en', 'record', 'shares_record_with', *CHANNELS],
          [[a, *n, r, ' '.join(map(str, shared)), *t] for a, n, r, shared, t in pilots])

    def names3(n: list[str]) -> dict[str, str]:
        ja, zh, en = n
        return {'ja': ja, 'zh': zh or ja, 'en': en or ja}
    web = {
        'schema': 'srw64.web-data-mods.v1',
        'units': [{'id': u, 'name': names3(n), 'terrain': ''.join(t)} for u, n, t in units if n[0]],
        'pilots': [{'id': a, 'name': names3(n), 'terrain': ''.join(t), 'shared': shared}
                   for a, n, r, shared, t in pilots if n[0]],
    }
    WEB.write_text(json.dumps(web, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'{WEB.relative_to(ROOT)}: {len(web["units"])} units, {len(web["pilots"])} pilots')


if __name__ == '__main__':
    main()
