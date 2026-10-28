"""Tables for music and battle voice MOD authors (docs/guide/audio-mods/*.csv).

  songs.csv          the 49 songs of the サウンドセレクト list and where the game plays them
  pilots.csv         each character's battle song (ROM 0x7D6A0), voice part and line count
  units.csv          each unit's battle song (ROM 0x7DF30), used when no player unit fights
  battle-voices.csv  every battle line by record number: who says it, when, and its text

and web/src/data/bgm.json, the same songs, pilots and units for the website's BGM MOD
maker (web/src/pages/[lang]/mods/bgm.astro), and web/src/data/voices.json, when each battle
line shows as fields the battle voice maker words in each language (mods/voices.astro; its
text comes from battle-voices.csv).

Names and Japanese text come from the text export (assets/text-export), the Chinese and
English text from the shipped dialogue files (content/dialogue/<locale>/battle). The
rules behind the columns are in docs/design/audio-mods.md §2 and docs/data/battle-quotes.md.

    .venv/bin/python tools/content/export_audio_tables.py
"""
from __future__ import annotations

from collections import defaultdict
import csv
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/guide/audio-mods'
WEB = ROOT / 'web/src/data/bgm.json'
WEB_VOICES = ROOT / 'web/src/data/voices.json'
SOUND_SELECT, SONG_ROWS = 0x1147E0, 49
KARAOKE, KARAOKE_ROWS = 0x1148A4, 19
PILOT_SONGS, ACTORS = 0x7D6A0, 361
UNIT_SONGS, UNITS = 0x7DF30, 363
MAPS, MAP_COUNT, MAP_SIZE = 0x10267C, 158, 12
VOICE_PARTS = 0x800CA9C4 - 0x80075610
UNIT_NAMES, ACTOR_NAMES = 527, 4382
LOCALES = ('zh-Hans', 'en')
# Songs the code plays outright (docs/design/battle-viewer.md §6.7).
FIXED = {
    0x0A: '战斗：ゴッドガンダム 明鏡止水形态（H）',
    0x0B: '战斗：シャッフル同盟 S 形态',
    0x0D: '战斗：V-MAX 发动中',
    0x22: '战斗：ヴァル＝ア／アヴィ＝ルー／ジェイ＝レン 任一方在场',
    0x26: '敌增援登场 jingle（默认只放一次）',
    0x27: '出击准备（推断）',
}
# The same, as keys the web page words in each language.
FIXED_KEYS = {0x0A: 'h_form', 0x0B: 's_form', 0x0D: 'vmax', 0x22: 'valua', 0x26: 'reinforcements', 0x27: 'sortie'}


def plain(text: str) -> str:
    return text.replace('<END>', '').replace('<BR>', '').strip()


def dialogue(locale: str) -> dict[int, str]:
    """Record number -> translation, pages joined with ' / '."""
    lines: dict[int, str] = {}
    for path in sorted((ROOT / 'content/dialogue' / locale / 'battle').glob('*.txt')):
        record, pages, page = None, [], []
        def close() -> None:
            if record is not None:
                lines[record] = ' / '.join(' '.join(p) for p in pages + [page] if p)
        for raw in path.read_text(encoding='utf-8').splitlines() + ['']:
            line = raw.strip()
            if line.startswith('@'):
                close()
                record, pages, page = int(line[1:].split()[0]), [], []
            elif record is None or line.startswith(('#', '>')):
                continue
            elif line == '---':
                pages.append(page); page = []
            elif line:
                page.append(line)
        close()
    return lines


def scene(t: dict) -> dict:
    """One trigger (battle_quotes.Trigger.as_dict) as the short fields the voice maker reads."""
    out: dict = {'k': t['kind']}
    if t.get('situation') is not None:
        out['s'] = t['situation']
    code = t.get('code')
    if t['kind'] == 'situation' and code is not None and (code - 10000) // 2500 == 7:
        out['ai'] = 1                       # 27500: evaded by an afterimage
    if t['kind'] == 'generic':
        m = re.search(r'通用，(\d+)/(\d+)', t['desc'])
        out['g'] = [int(m[1]), int(m[2])]
    if t['kind'] == 'weapon' and code is not None and code < 2600:
        out['lo'] = 1                       # may yield to a general line
    for key, short in (('weapon', 'w'), ('weapon_raw', 'wr'), ('own_unit', 'u'), ('opponent_unit', 'ou'),
                       ('opponent_voice', 'ov'), ('copilot', 'cp'), ('k', 'c'), ('captain', 'cap')):
        if t.get(key) is not None:
            out[short] = t[key]
    if 'flag' in t:
        out['f'] = [t['flag'], t['flag_value']] if 'flag_value' in t else [t['flag']]
    if t.get('female_opponent'):
        out['fe'] = 1
    if 'sequence' in t:
        out['q'] = [t['position'] + 1, len(t['sequence'])]
    if t.get('unreachable'):
        out['x'] = 1
    return out


def write(name: str, header: list[str], rows: list[list]) -> None:
    with (OUT / name).open('w', encoding='utf-8', newline='') as f:
        out = csv.writer(f, lineterminator='\n')
        out.writerow(header)
        out.writerows(rows)
    print(f'{name}: {len(rows)} rows')


def main() -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    u16 = lambda a: struct.unpack_from('>H', rom, a)[0]
    s16 = lambda a: struct.unpack_from('>h', rom, a)[0]
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

    pilot_song = {a: s16(PILOT_SONGS + 2 * a) for a in range(ACTORS)}
    unit_song = {u: s16(UNIT_SONGS + 2 * u) for u in range(UNITS)}
    voice_part = {a: s16(VOICE_PARTS + 2 * a) for a in range(ACTORS)}
    karaoke = {u16(KARAOKE + 4 * k + 2) for k in range(KARAOKE_ROWS)}
    stage_maps: dict[int, int] = defaultdict(int)
    for m in range(MAP_COUNT):
        stage_maps[rom[MAPS + m * MAP_SIZE + 0xB]] += 1

    # Battle lines.
    texts = {l: dialogue(l) for l in LOCALES}
    voices, lines_of, scenes = [], defaultdict(int), []
    for record in sorted(records):
        r = records[record]
        if not r['category'].startswith('battle.'):
            continue
        context = r.get('context', {})
        triggers = context.get('triggers', [])
        speaker = r.get('speaker_id', 0)
        part = context.get('voice', '')
        if triggers:
            lines_of[speaker] += 1
        scenes.append([record, [scene(t) for t in triggers]])
        voices.append([
            record, speaker, *name(ACTOR_NAMES + speaker), part,
            '；'.join(t['desc'] for t in triggers) or '原版不会显示',
            plain(r['display']), *(texts[l].get(record, '') for l in LOCALES)])
    write('battle-voices.csv',
          ['record', 'speaker', 'speaker_ja', 'speaker_zh', 'speaker_en', 'voice_part',
           'trigger', 'text_ja', 'text_zh', 'text_en'], voices)

    write('pilots.csv',
          ['pilot', 'name_ja', 'name_zh', 'name_en', 'battle_song', 'voice_part', 'battle_lines'],
          [[a, *name(ACTOR_NAMES + a), pilot_song[a], voice_part[a] if voice_part[a] >= 0 else '',
            lines_of.get(a, 0)] for a in range(ACTORS)])
    write('units.csv', ['unit', 'name_ja', 'name_zh', 'name_en', 'battle_song'],
          [[u, *name(UNIT_NAMES + u), unit_song[u]] for u in range(UNITS)])

    def names(table: dict[int, int], base: int, song: int) -> str:
        return '、'.join(plain(records[base + k]['display']) for k, s in table.items() if s == song)
    songs = []
    for row in range(SONG_ROWS):
        text, song = u16(SOUND_SELECT + 4 * row), u16(SOUND_SELECT + 4 * row + 2)
        uses = []
        if song in FIXED:
            uses.append(FIXED[song])
        if stage_maps.get(song):
            uses.append(f'关卡曲（{stage_maps[song]} 张地图）')
        pilots, units = names(pilot_song, ACTOR_NAMES, song), names(unit_song, UNIT_NAMES, song)
        if pilots:
            uses.append(f'我方战斗曲（人物）：{pilots}')
        if units:
            uses.append(f'敌方战斗曲（机体）：{units}')
        songs.append([song, row + 1, plain(records[text]['display']),
                      'yes' if song in karaoke else '', '；'.join(uses)])
    write('songs.csv', ['song', 'sound_select_row', 'title_ja', 'karaoke', 'uses'], songs)

    def names3(record: int) -> dict[str, str]:
        ja, zh, en = name(record)
        return {'ja': ja, 'zh': zh or ja, 'en': en or ja}
    web = {
        'schema': 'srw64.web-bgm-data.v1',
        'songs': [{
            'song': song, 'row': row + 1, 'title': plain(records[u16(SOUND_SELECT + 4 * row)]['display']),
            'karaoke': song in karaoke, 'fixed': FIXED_KEYS.get(song), 'stageMaps': stage_maps.get(song, 0),
            'pilots': [a for a, s in pilot_song.items() if s == song], 'units': [u for u, s in unit_song.items() if s == song],
        } for row in range(SONG_ROWS) for song in [u16(SOUND_SELECT + 4 * row + 2)]],
        'pilots': [{'id': a, 'name': names3(ACTOR_NAMES + a), 'song': pilot_song[a]}
                   for a in range(ACTORS) if plain(records[ACTOR_NAMES + a]['display'])],
        'units': [{'id': u, 'name': names3(UNIT_NAMES + u), 'song': unit_song[u]}
                  for u in range(UNITS) if plain(records[UNIT_NAMES + u]['display'])],
    }
    WEB.write_text(json.dumps(web, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'{WEB.relative_to(ROOT)}: {len(web["songs"])} songs, {len(web["pilots"])} pilots, {len(web["units"])} units')

    # Who an opponent's voice part is (the "vs <pilot>" lines), and each line's triggers.
    actors_of_voice: dict[int, list[int]] = defaultdict(list)
    for a, v in voice_part.items():
        if v >= 0 and plain(records[ACTOR_NAMES + a]['display']):
            actors_of_voice[v].append(a)
    WEB_VOICES.write_text(json.dumps({
        'schema': 'srw64.web-voices.v1',
        'actorsOfVoice': {str(v): a for v, a in sorted(actors_of_voice.items())},
        'scenes': scenes,
    }, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'{WEB_VOICES.relative_to(ROOT)}: {len(scenes)} lines')


if __name__ == '__main__':
    main()
