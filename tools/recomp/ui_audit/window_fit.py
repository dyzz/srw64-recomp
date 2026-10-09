#!/usr/bin/env python3
"""Static check of translated labels against the original windows they sit in, without
running the game (docs/native/native-ui-text.md §6).

Every resident layout (table D_800C8BB8, 24 bytes an entry) gives its labels as
{text id, x, y} and its window frame as a grid scene of the line atlas. The frame is composed
from the ROM (as tools/hd_ai/frame_hd.py does, with the widened menus of
src/host/menu_widen.cpp applied), and each label's room runs from its start to the first frame
line on its row, or to the next label on that row. The translation is measured with the font
the game uses (HarmonyOS Sans Condensed for English, SC otherwise) at the overlay's label size
and compared with that room under the overlay's rules (src/host/ui_text.cpp): narrowed to 70 %
first, then down to 1.5 points smaller.

A label whose Japanese starts with a particle (のデータをロードします after ROMカートリッジ)
continues text the game prints before it at run time, and the overlay sets the two as one
sentence: such a label is checked against the whole window row, and "prefix room" says how
much is left for what comes before it. Two labels the overlay joins (the second starting
within a cell of where the first's original ends, 主人公|：名前を入力) are checked as one.

The unit command menu is built at run time (801CA4D8): its commands are checked against the
widened panel's room.

Kinds of finding: "overflow" (does not fit even narrowed and shrunk) and "squeezed" (fits only
narrowed below --squeeze, default 80 %, so it looks visibly smaller than its neighbours)."""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT))
from PIL import ImageFont  # noqa: E402
from srw64_native.catalog import source_catalog  # noqa: E402
from srw64_rom.resources import ResourceTable  # noqa: E402
from tools.hd_ai.frame_hd import atlas_indices, compose  # noqa: E402

RESIDENT_RAM, RESIDENT_ROM = 0x80076610, 0x1000
LAYOUTS, LAYOUT_COUNT, LAYOUT_SIZE = 0x800C8BB8, 150, 24
LINES = 1295
# ui_text.cpp label_style: size, smallest size, narrowest squeeze; kInset.
SIZE = {'en': 12.0, 'zh-Hans': 12.5, 'ja': 12.5}
CONDENSE, SHRINK, INSET = .7, 1.5, 1
FONT = {'en': 'HarmonyOS_Sans_Condensed.ttf', 'zh-Hans': 'HarmonyOS_Sans_SC.ttf', 'ja': 'HarmonyOS_Sans_SC.ttf'}
# menu_widen.cpp: scene -> rows top..bottom, stretched column, shifted columns, delta.
WIDEN = {**{s: (0, 176, 16, 32, 48, 16) for s in range(1161, 1172)}, 1177: (32, 176, 80, 96, 112, 8), 1329: (32, 64, 80, 96, 112, 8)}
# The unit command menu (801CA4D8): the frame at X+8, labels at X+0x12, the dark panel to the
# frame's x + 43, widened by 16; the commands' text ids.
UNIT_MENU_ROOM = 43 + 16 - 10 - INSET * 2
PARTICLES = set('のにをがはでとへもか')   # か: からロードします
UNIT_COMMANDS = ['移動', '攻撃', '変形', '分離', '合体', '精神', '能力', '修理', '補給', '発進', '搭載', '説得', '空中', '地上', '水中', '地中', '待機']


def resident(rom, address, size):
    return rom[address - RESIDENT_RAM + RESIDENT_ROM:][:size]


def labels(rom, pointer):
    out = []
    if not RESIDENT_RAM <= pointer < 0x80100000:
        return out
    for k in range(64):
        text, x, y, _ = struct.unpack('>4H', resident(rom, pointer + 8 * k, 8))
        if text == 0xFFFF:
            break
        out.append((text, x, y))
    return out


def widened(scene, data, atlas):
    """The scene composed as the game now draws it (menu_widen.cpp)."""
    if scene not in WIDEN:
        return compose(data, atlas)
    top, bottom, stretch, shift, shift_end, delta = WIDEN[scene]
    kind, groups, w8, h8 = struct.unpack_from('>4H', data, 0)
    w, h = max(w8 * 8, shift_end + delta), h8 * 8
    grid = [[0] * w for _ in range(h)]
    for g in range(groups):
        tile, count, offset = struct.unpack_from('>3H', data, 8 + w8 * h8 + g * 6)
        if not tile:
            continue
        sx, sy = (tile & 15) * 8 + ((tile & 0x300) >> 1), ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3)
        if sy + 16 > len(atlas) or sx + 16 > len(atlas[0]):
            continue
        for k in range(count):
            flags, x, y = struct.unpack_from('>3H', data, offset + k * 6)
            cells = 16
            if top <= y <= bottom:
                if x == stretch:
                    cells = 16 + delta
                elif shift <= x < shift_end:
                    x += delta
            for r in range(16):
                for c in range(cells):
                    u = c * 16 // cells
                    n = atlas[sy + (15 - r if flags & 0x8000 else r)][sx + (15 - u if flags & 0x4000 else u)]
                    if n and y + r < h and x + c < w:
                        grid[y + r][x + c] = n
    return grid


def line_at(grid, row, col):
    return any(grid[r][col] for r in (row - 2, row, row + 2) if 0 <= r < len(grid))


def window_left(grid, x, y):
    """The first column inside the frame left of the label."""
    row = min(len(grid) - 1, y + 7)
    for col in range(x - 1, -1, -1):
        if line_at(grid, row, col):
            return col + 2
    return 0


def room(grid, x, y, others):
    """From the label's start to the first frame line on its row, or the next label."""
    end = 316
    row = min(len(grid) - 1, y + 7)
    for col in range(x + 1, min(len(grid[0]), 320)):
        if line_at(grid, row, col):
            end = col - 1
            break
    for ox, oy in others:
        if abs(oy - y) <= 6 and ox > x + 2:
            end = min(end, ox - 4)
    return end - (x + INSET)


def original_width(source):
    """The ROM's cells: kanji and full-width forms 14 pixels, kana and the rest 8."""
    return sum(14 if '\u4e00' <= c <= '\u9fff' or c == '々' or '\uff00' <= c <= '\uffef' else 8 for c in source)


def joins(items, sources, x, y, text_id):
    """The label the overlay sets after this one as one sentence (ui_text.cpp): on its row,
    starting within a cell of where this one's original ends, alone in its column, and
    starting with a particle or punctuation."""
    end = x + original_width(sources.get(text_id, ''))
    for t, ox, oy in items:
        if abs(oy - y) > 2 or not end - 2 <= ox <= end + 8:
            continue
        if sources.get(t, '')[:1] not in PARTICLES | set('：:、。！？'):
            continue   # a word of its own (レベルアップ|レベル 12)
        if any(abs(cx - ox) <= 1 and 0 < abs(cy - oy) <= 24 for _, cx, cy in items):
            continue
        return t, ox, oy
    return None


def fit(font, size, text, space):
    natural = font.getlength(text) * size / 100
    if natural <= space:
        return 'fits', natural, 1.0
    squeeze = space / natural
    if squeeze >= CONDENSE:
        return 'squeezed', natural, squeeze
    if natural * CONDENSE * (size - SHRINK) / size <= space:
        return 'shrunk', natural, squeeze
    return 'overflow', natural, squeeze


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--locale', default='en')
    parser.add_argument('--squeeze', type=float, default=.8, help='report what needs narrowing below this')
    parser.add_argument('--json', help='write every label checked here')
    args = parser.parse_args()
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    atlas, _, _ = atlas_indices(table, LINES)
    font = ImageFont.truetype(str(ROOT / 'build/fonts' / FONT[args.locale]), 100)
    size = SIZE[args.locale]
    entries = {e['key']: re.sub(r'<END>$', '', e['target']) for e in json.loads((ROOT / f'content/locales/{args.locale}.json').read_text())['entries']}
    terms = json.loads((ROOT / f'content/locales/terms/{args.locale}.json').read_text())['sections']
    sources = source_catalog(ROOT, ROOT / 'rom.z64')[0]
    rows, skipped = [], []
    for i in range(LAYOUT_COUNT):
        _, pointer, _, scene, atlas_id, _, _ = struct.unpack('>IIIHHHH', resident(rom, LAYOUTS + i * LAYOUT_SIZE, 20))
        items = labels(rom, pointer)
        if not items or atlas_id != LINES:
            continue
        data = table.extract(scene)[0]
        _, _, w8, h8 = struct.unpack_from('>4H', data, 0)
        if (w8 * 8, h8 * 8) != (320, 240):
            skipped.append({'layout': hex(i), 'scene': scene, 'why': f'scene {w8 * 8}x{h8 * 8}, placed at run time'})
            continue
        grid = widened(scene, data, atlas)
        source_of = {t: re.sub(r'<END>$', '', sources.get(f'base:t00_{t:05d}', '')) for t, _, _ in items}
        joined = set()
        for text_id, x, y in items:
            if (text_id, x, y) in joined:
                continue
            text = entries.get(f'base:t00_{text_id:05d}', '')
            if not text or '<' in text.replace('<END>', ''):
                continue
            follower = joins(items, source_of, x, y, text_id)
            if follower and not re.search(r' {2,}', text):
                second = entries.get(f'base:t00_{follower[0]:05d}', '').strip()
                if second and '<' not in second:
                    joined.add(follower)
                    sep = '' if second[:1] in ':;,.!?)' or args.locale != 'en' else ' '
                    whole = text.strip() + sep + second
                    space = room(grid, x, y, [(ox, oy) for t, ox, oy in items if (ox, oy) not in ((x, y), follower[1:])])
                    kind, natural, squeeze = fit(font, size, whole, space)
                    rows.append({'layout': hex(i), 'scene': scene, 'text_id': text_id, 'x': x, 'y': y, 'text': whole,
                                 'room': space, 'natural': round(natural, 1), 'squeeze': round(squeeze, 2), 'kind': kind, 'joined': True})
                    continue
            others = [(ox, oy) for t, ox, oy in items if (ox, oy) != (x, y)]
            source = re.sub(r'<END>$', '', sources.get(f'base:t00_{text_id:05d}', '')).lstrip()
            left = window_left(grid, x, y)
            # Continues what the game prints before it (a particle first, and room on its left
            # for what comes before, the translation not capitalised: はい and のりかえ are words).
            if source[:1] in PARTICLES and x - left >= 16 and not text.strip()[:1].isupper():
                space = room(grid, left, y, [o for o in others if o[0] >= x])
                kind, natural, squeeze = fit(font, size, text.strip(), space)
                rows.append({'layout': hex(i), 'scene': scene, 'text_id': text_id, 'x': x, 'y': y, 'text': text.strip(),
                             'room': space, 'natural': round(natural, 1), 'squeeze': round(squeeze, 2), 'kind': kind,
                             'continues': True, 'prefix_room': round(space - natural, 1)})
                continue
            # Runs of two spaces hold numbers other labels print: the first part from its start.
            for part in [p for p in re.split(r' {2,}', text) if p.strip()][:1]:
                space = room(grid, x, y, others)
                kind, natural, squeeze = fit(font, size, part.strip(), space)
                rows.append({'layout': hex(i), 'scene': scene, 'text_id': text_id, 'x': x, 'y': y, 'text': part.strip(),
                             'room': space, 'natural': round(natural, 1), 'squeeze': round(squeeze, 2), 'kind': kind})
    for jp in UNIT_COMMANDS:
        text = terms['map_commands'].get(jp) or terms['abilities'].get(jp)
        if text:
            kind, natural, squeeze = fit(font, size, text, UNIT_MENU_ROOM)
            rows.append({'layout': 'unit menu', 'scene': 1161, 'text_id': None, 'x': None, 'y': None, 'text': text,
                         'room': UNIT_MENU_ROOM, 'natural': round(natural, 1), 'squeeze': round(squeeze, 2), 'kind': kind})
    bad = [r for r in rows if r['kind'] in ('overflow', 'shrunk') or r['squeeze'] < args.squeeze]
    seen = set()
    for r in sorted(bad, key=lambda r: r['squeeze']):
        if (r['text'], r['room']) in seen:
            continue
        seen.add((r['text'], r['room']))
        where = r['layout'] if r['x'] is None else f"{r['layout']} ({r['x']},{r['y']})"
        extra = f"  (after run-time text; {r['prefix_room']} left for it)" if r.get('continues') else ''
        print(f"{r['kind']:9} {r['squeeze']:.2f}  room {r['room']:3}  needs {r['natural']:6.1f}  {where:16} {r['text']}{extra}")
    print(f'{len(rows)} labels checked in {len({r["layout"] for r in rows})} layouts; '
          f'{len(seen)} need narrowing below {args.squeeze:.0%} or more; {len(skipped)} layouts not checked')
    for s in skipped:
        print(f"  not checked: layout {s['layout']} scene {s['scene']}: {s['why']}")
    if args.json:
        Path(args.json).write_text(json.dumps({'labels': rows, 'skipped': skipped}, ensure_ascii=False, indent=1))
    return 1 if any(r['kind'] == 'overflow' for r in rows) else 0


if __name__ == '__main__':
    raise SystemExit(main())
