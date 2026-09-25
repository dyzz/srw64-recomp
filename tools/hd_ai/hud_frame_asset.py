"""Redraw the battle HUD frame (scene 1193: resource-1296 slices, palette 1301) in high definition.

The HUD is two 16-pixel-high rows of resource-1296 slices: the HP/EN panels of both sides, with
bevel bands, raised tabs carrying blue lights, the yellow HP and EN letters and the slashes between
current and maximum values. The dialogue box (docs/native/native-dialogue-runtime-hd.md) uses other
slices of the same strip and palette; its plain top and bottom edge slices (source x 48 and 208) are
also HUD slices and stay as that tool draws them.

The redraw keeps every band where the ROM has it, in the dialogue redraw's colours (light bands on
the top and left, dark on the bottom and right, the navy inner line), crisp at 4x. The blue lights
become the dialogue redraw's glass lights, the letters are HarmonyOS Sans Bold in the ROM yellow with
its darker shadow, and the slashes are straight antialiased strokes. Each slice is cut from that one
drawing at its place in the HUD, so neighbouring slices join; RT64 replaces them by texture hash.

    PYTHONPATH=src:. .venv/bin/python -B tools/hd_ai/hud_frame_asset.py \\
        --pack assets/hd-ai/worldmap-surfaces/pack-v2 --output build/hd-ai/hud-frame \\
        --art content/art/stage1-hd.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from srw64_rom.resources import ResourceTable
from tools.hd_ai.dialogue_frame_asset import DARK, LIGHT, NAVY, PALETTE, SCALE, SS, TAB_BOTTOM, TAB_TOP, U, light, rt64_hash
from tools.hd_ai.rt64_hash import ROOT, hasher

SCENE = 1193
DIALOGUE_SLICES = {48, 208}           # drawn by dialogue_frame_asset.py
ROLES = {0x1095: 'N', 0xffff: 'W', 0xc633: 's', 0x8c69: 'g', 0x3331: 'B', 0xffc7: 'Y', 0x8401: 'y', 0x6b61: 'd', 0xad6d: 'l'}
YELLOW, YELLOW_SHADOW = (255, 255, 57), (132, 132, 8)
FONT = ROOT / 'build/fonts/HarmonyOS_Sans_SC.ttf'


def placements(scene: bytes) -> tuple[int, int, list[tuple[int, int, int]]]:
    """(width, height, [(source x, x, y)]) of a type-6 grid scene of 16x16 cells."""
    kind, groups, w, h = struct.unpack_from('>4H', scene, 0)
    assert kind == 6
    out = []
    for g in range(groups):
        tile, count, offset = struct.unpack_from('>3H', scene, 8 + w * h + g * 6)
        sx = (tile & 15) * 8 + ((tile & 0x300) >> 1)
        for k in range(count):
            flags, x, y = struct.unpack_from('>3H', scene, offset + k * 6)
            assert flags == 0, 'HUD slices are never flipped'
            out.append((sx, x, y))
    return w * 8, h * 8, out


def roles(data: bytes, width: int, height: int, places) -> list[list[str]]:
    colours = struct.unpack('>16H', PALETTE)
    grid = [['.'] * width for _ in range(height)]
    for sx, x, y in places:
        for r in range(16):
            row = data[8 + r * 256 + sx // 2:8 + r * 256 + sx // 2 + 8]
            for c, n in enumerate(v for byte in row for v in (byte >> 4, byte & 15)):
                if colours[n] & 1:
                    grid[y + r][x + c] = ROLES.get(colours[n], '?')
    return grid


def clusters(grid, wanted: set[str]) -> list[set[tuple[int, int]]]:
    """8-connected groups of the wanted roles."""
    seen, out = set(), []
    for y, row in enumerate(grid):
        for x, c in enumerate(row):
            if c not in wanted or (x, y) in seen:
                continue
            group, stack = set(), [(x, y)]
            while stack:
                px, py = stack.pop()
                if (px, py) in seen or not (0 <= py < len(grid) and 0 <= px < len(grid[0])) or grid[py][px] not in wanted:
                    continue
                seen.add((px, py)); group.add((px, py))
                stack += [(px + dx, py + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
            out.append(group)
    return out


def band_colour(role: str, x: int, y: int, height: int) -> tuple[int, int, int]:
    lit = y < height // 2
    return {'W': LIGHT[0], 's': LIGHT[1], 'g': LIGHT[2] if lit else DARK[1], 'l': DARK[0], 'd': DARK[2], 'N': NAVY}[role]


def draw_hud(grid) -> Image.Image:
    height, width = len(grid), len(grid[0])
    image = Image.new('RGBA', (width * U, height * U))
    draw = ImageDraw.Draw(image)
    # Letters and slashes: the yellow clusters, and the W/g diagonals inside the panels.
    # One word per line and panel: H and P are separate clusters of the same word.
    words: dict[tuple[bool, bool], set] = {}
    for group in clusters(grid, {'Y', 'y'}):
        x, y = min(group)
        words.setdefault((y < height // 2, x < width // 2), set()).update(group)
    letters = list(words.values())
    slashes = [g for g in clusters(grid, {'W', 'g'}) if len(g) >= 6 and
               all(4 < y < height - 5 for _, y in g) and len({x for x, _ in g}) >= 5 and len({y for _, y in g}) >= 5]
    text_cells = set().union(*letters, *slashes) if letters or slashes else set()
    for y, row in enumerate(grid):
        for x, c in enumerate(row):
            if (x, y) in text_cells or c in '.YyB?':
                continue
            draw.rectangle([x * U, y * U, (x + 1) * U - 1, (y + 1) * U - 1], fill=band_colour(c, x, y, height) + (255,))
    # Raised tabs keep their row; the blue lights in them become glass.
    for y in (0, height - 1):
        for x, c in enumerate(grid[y]):
            if c in 'Wl':
                draw.rectangle([x * U, y * U, (x + 1) * U - 1, (y + 1) * U - 1], fill=(TAB_TOP if y == 0 else TAB_BOTTOM) + (255,))
    for y in range(height):
        x = 0
        while x < width:
            if grid[y][x] != 'B':
                x += 1
                continue
            end = x
            while end < width and grid[y][end] == 'B':
                end += 1
            tile = Image.new('RGBA', ((end - x + 2) * U, 3 * U))
            light(tile, 1, end - x + 1, 1, False, True, True)
            image.alpha_composite(tile, ((x - 1) * U, (y - 1) * U))
            x = end
    # The letters: HP on the upper line, EN on the lower, cap height as the ROM glyphs.
    font_path = FONT if FONT.exists() else None
    for group in letters:
        xs, ys = [x for x, _ in group], [y for _, y in group]
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        text = 'HP' if y0 < height // 2 else 'EN'
        cap = (y1 - y0 - 1) * U                       # the last row is the shadow
        size = cap / 0.72
        font = ImageFont.truetype(str(font_path), int(size)) if font_path else ImageFont.load_default()
        if font_path:
            font.set_variation_by_axes([706])
        box = font.getbbox(text)
        tx = x0 * U - box[0] + ((x1 - x0) * U - (box[2] - box[0])) / 2 - .5 * U
        ty = y0 * U - box[1]
        draw.text((tx + .9 * U, ty + .9 * U), text, font=font, fill=YELLOW_SHADOW + (255,))
        draw.text((tx, ty), text, font=font, fill=YELLOW + (255,))
    # The slashes: from the lowest to the highest cell, white over a grey shadow.
    for group in slashes:
        low = max(group, key=lambda p: (p[1], -p[0]))
        high = min(group, key=lambda p: (p[1], -p[0]))
        a, b = ((low[0] + .5) * U, (low[1] + .5) * U), ((high[0] + .5) * U, (high[1] + .5) * U)
        draw.line([(a[0] + .7 * U, a[1] + .4 * U), (b[0] + .7 * U, b[1] + .4 * U)], fill=LIGHT[2] + (255,), width=int(1.1 * U))
        draw.line([a, b], fill=LIGHT[0] + (255,), width=int(1.0 * U))
    return image


def build(pack: Path, output: Path) -> dict:
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    data = table.extract(1296)[0]
    assert table.extract(1301)[0][8:40] == PALETTE
    width, height, places = placements(table.extract(SCENE)[0])
    grid = roles(data, width, height, places)
    whole = draw_hud(grid)
    output.mkdir(parents=True, exist_ok=True)
    whole.resize((width * SCALE, height * SCALE), Image.Resampling.LANCZOS).save(output / 'hud-frame-1152x128.png')
    xxh = hasher()
    textures, done = [], set()
    for sx, x, y in places:
        if sx in DIALOGUE_SLICES or sx in done:
            continue
        done.add(sx)
        tile = whole.crop((x * U, y * U, (x + 16) * U, (y + 16) * U)).resize((16 * SCALE, 16 * SCALE), Image.Resampling.LANCZOS)
        digest = rt64_hash(data, sx, xxh)
        name = f'frame-resource1296-{digest}.png'
        tile.save(pack / name)
        textures.append({'hashes': {'rt64': digest}, 'path': name, 'source_x': sx, 'place': [x, y],
                         'sha256': hashlib.sha256((pack / name).read_bytes()).hexdigest()})
    return {'scene': SCENE, 'resource_id': 1296, 'palette': 1301, 'decoded_sha256': hashlib.sha256(data).hexdigest(),
            'original_slice_size': [16, 16], 'replacement_slice_size': [64, 64], 'supersampling': SS,
            'construction': 'ROM bands in the dialogue redraw colours, glass lights, HarmonyOS Sans Bold HP/EN, stroked slashes',
            'textures': textures}


def register(pack: Path, art: Path, textures: list[dict]) -> None:
    """Add the slices to the pack database and the art manifest (both in place, idempotent)."""
    database_path = pack / 'rt64.json'
    database = json.loads(database_path.read_text())
    known = {t['hashes']['rt64'] for t in database['textures']}
    for item in textures:
        if item['hashes']['rt64'] not in known:
            database['textures'].append({'hashes': item['hashes'], 'path': item['path']})
    database_path.write_text(json.dumps(database, indent=2, ensure_ascii=False) + '\n')
    manifest = json.loads(art.read_text())
    manifest['source']['manifest_sha256'] = hashlib.sha256(database_path.read_bytes()).hexdigest()
    rows = {row['hash']: row for row in manifest['textures']}
    for item in textures:
        digest = item['hashes']['rt64']
        if digest in rows:
            rows[digest]['sha256'] = item['sha256']
        else:
            manifest['textures'].append({'hash': digest, 'kind': 'frame', 'sha256': item['sha256']})
    art.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pack', type=Path, required=True, help='RT64 pack directory to write the slices into')
    parser.add_argument('--output', type=Path, required=True, help='directory for the preview and build record')
    parser.add_argument('--art', type=Path, help='art pack manifest to register the slices in')
    args = parser.parse_args()
    record = build(args.pack, args.output)
    (args.output / 'hud-frame.json').write_text(json.dumps(record, indent=1) + '\n')
    if args.art:
        register(args.pack, args.art, record['textures'])
    print(f"{len(record['textures'])} HUD slices written to {args.pack}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
