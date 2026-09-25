"""HD window frames: every resource-1295 frame scene as an HD colour-index image.

The original windows (docs/native/native-ui-text.md §5) are type-6 grid scenes drawn in sprite mode 9
by 800945D4: 16x16 cells of the line-segment atlas 1295, some mirrored. Their palettes cycle
(1016-1019, 1304), so a texture replaced by its hash would match only one step. The HD tactical map
path (src/host/native_map.cpp) already draws an HD index image with the palette the game has loaded
this frame; these assets use it.

Each scene is composed at 1x as palette indices, upscaled 4x with Scale2x applied twice (EPX: it only
ever repeats an existing index, so lines stay crisp, stairs become diagonals and the palette still
applies), and cropped to the cells it uses. Per scene the folder holds meta.json (the HD map
runtime schema plus the crop's origin in scene pixels), index.png (one index per pixel) and base.png
(the indices painted with the scene's own palette; index 0 is transparent).

    PYTHONPATH=src:. .venv/bin/python -B tools/hd_ai/frame_hd.py --output assets/hd-ai/frames/v1
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image
from srw64_rom.resources import ResourceTable
from tools.hd_ai.rt64_hash import ROOT

ATLAS, ARROWS = 1295, 1302   # window lines; the red page arrows (layouts 0x5C 0x5E 0x60 0x62-0x67 0x95)
LAYOUTS, LAYOUT_COUNT, LAYOUT_SIZE = 0x800C8BB8, 150, 24
RESIDENT_RAM, RESIDENT_ROM = 0x80076610, 0x1000
# Frames the tactical overlay builds itself (801CD718 small status window, 801CABAC terrain panel)
# and one it shows from a table: scene -> palette.
EXTRA = {1014: 1015, 1165: 1297, 1172: 1297, 1173: 1297}
SCALE = 4
FRAME_FIRST, FRAME_LAST, DEFAULT_PALETTE = 1165, 1330, 1297
HUD_STRIP = {1193, 1194}   # resource-1296 scenes: hud_frame_asset.py and the dialogue frame


def resident(rom: bytes, address: int, size: int) -> bytes:
    offset = address - RESIDENT_RAM + RESIDENT_ROM
    return rom[offset:offset + size]


def frame_scenes(rom: bytes, table: ResourceTable) -> dict[int, tuple[int, int]]:
    """Scene -> (atlas, palette): every layout drawn from the 1295 or 1302 atlas, the overlay's
    own frames, and the other grid scenes in the frame range (menus pick some at run time, e.g.
    1198/1199). Scenes of the 1296 dialogue and HUD strips stay out."""
    out, other = {}, set()
    for i in range(LAYOUT_COUNT):
        entry = resident(rom, LAYOUTS + i * LAYOUT_SIZE, LAYOUT_SIZE)
        scene, atlas, palette = struct.unpack_from('>3H', entry, 12)
        if atlas in (ATLAS, ARROWS):
            out.setdefault(scene, (atlas, palette))
        else:
            other.add(scene)
    for scene, palette in EXTRA.items():
        out.setdefault(scene, (ATLAS, palette))
    for scene in range(FRAME_FIRST, FRAME_LAST + 1):
        if scene in out or scene in other or scene in HUD_STRIP:
            continue
        data = table.extract(scene)[0]
        if len(data) >= 8 and struct.unpack_from('>H', data)[0] == 6:
            out[scene] = (ATLAS, DEFAULT_PALETTE)
    return out


def atlas_indices(table: ResourceTable, atlas: int) -> tuple[list[list[int]], int, int]:
    data = table.extract(atlas)[0]
    fmt, width, height, _ = struct.unpack_from('>4H', data)
    rows = []
    for y in range(height):
        row = data[8 + y * width // 2:8 + (y + 1) * width // 2]
        rows.append([n for v in row for n in (v >> 4, v & 15)])
    return rows, width, height


def compose(scene: bytes, atlas: list[list[int]]) -> list[list[int]]:
    kind, groups, w, h = struct.unpack_from('>4H', scene, 0)
    assert kind == 6, 'not a grid scene'
    grid = [[0] * (w * 8) for _ in range(h * 8)]
    for g in range(groups):
        tile, count, offset = struct.unpack_from('>3H', scene, 8 + w * h + g * 6)
        if not tile:
            continue
        sx = (tile & 15) * 8 + ((tile & 0x300) >> 1)
        sy = ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3)
        if sy + 16 > len(atlas) or sx + 16 > len(atlas[0]):
            continue   # 0xFFEF in scene 1246 lies outside the atlas: no picture
        for k in range(count):
            flags, x, y = struct.unpack_from('>3H', scene, offset + k * 6)
            for r in range(16):
                for c in range(16):
                    src_r = 15 - r if flags & 0x8000 else r
                    src_c = 15 - c if flags & 0x4000 else c
                    n = atlas[sy + src_r][sx + src_c]
                    if n and 0 <= y + r < h * 8 and 0 <= x + c < w * 8:
                        grid[y + r][x + c] = n
    return grid


def scale2x(grid: list[list[int]]) -> list[list[int]]:
    h, w = len(grid), len(grid[0])
    out = [[0] * (w * 2) for _ in range(h * 2)]
    for y in range(h):
        for x in range(w):
            p = grid[y][x]
            a = grid[y - 1][x] if y else p
            b = grid[y][x + 1] if x + 1 < w else p
            c = grid[y][x - 1] if x else p
            d = grid[y + 1][x] if y + 1 < h else p
            out[2 * y][2 * x] = a if c == a and c != d and a != b else p
            out[2 * y][2 * x + 1] = b if a == b and a != c and b != d else p
            out[2 * y + 1][2 * x] = c if d == c and d != b and c != a else p
            out[2 * y + 1][2 * x + 1] = d if b == d and b != a and d != c else p
    return out


def palette_rgba(table: ResourceTable, palette: int) -> list[list[int]]:
    data = table.extract(palette)[0]
    count = (len(data) - 8) // 2
    colours = struct.unpack_from(f'>{count}H', data, 8)
    out = []
    for n in range(256):
        c = colours[n] if n < count else 0
        out.append([(c >> 11 & 31) * 255 // 31, (c >> 6 & 31) * 255 // 31, (c >> 1 & 31) * 255 // 31, 255 if c & 1 else 0])
    out[0][3] = 0
    return out


def build(output: Path) -> dict:
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    atlases = {a: atlas_indices(table, a)[0] for a in (ATLAS, ARROWS)}
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for scene, (atlas_id, palette) in sorted(frame_scenes(rom, table).items()):
        try:
            grid = compose(table.extract(scene)[0], atlases[atlas_id])
        except (struct.error, AssertionError):
            continue   # a resource in the range that only looks like a grid scene
        used = [(x, y) for y, row in enumerate(grid) for x, n in enumerate(row) if n]
        if not used:
            continue
        x0 = max(0, min(x for x, _ in used) - 1); y0 = max(0, min(y for _, y in used) - 1)
        x1 = min(len(grid[0]), max(x for x, _ in used) + 2); y1 = min(len(grid), max(y for _, y in used) + 2)
        crop = [row[x0:x1] for row in grid[y0:y1]]
        hd = scale2x(scale2x(crop))
        reference = palette_rgba(table, palette)
        folder = output / str(scene)
        folder.mkdir(exist_ok=True)
        index = Image.frombytes('L', (len(hd[0]), len(hd)), bytes(n for row in hd for n in row))
        index.save(folder / 'index.png')
        base = Image.new('RGBA', index.size)
        base.putdata([tuple(reference[n]) for row in hd for n in row])
        base.save(folder / 'base.png')
        meta = {'schema': 'srw64.hd-map-runtime.v0', 'layout': scene, 'width': x1 - x0, 'height': y1 - y0, 'scale': SCALE,
                'origin': [x0, y0], 'alpha': True, 'reference_palette': reference, 'kind': 'frame', 'atlas': atlas_id,
                'palette': palette}
        (folder / 'meta.json').write_text(json.dumps(meta) + '\n')
        rows.append({'scene': scene, 'atlas': atlas_id, 'palette': palette, 'origin': [x0, y0], 'size': [x1 - x0, y1 - y0],
                     'sha256': {name: hashlib.sha256((folder / name).read_bytes()).hexdigest()
                                for name in ('meta.json', 'index.png', 'base.png')}})
    record = {'schema': 'srw64.hd-frames.v1', 'atlases': [ATLAS, ARROWS], 'scale': SCALE, 'method': 'Scale2x twice on palette indices',
              'scenes': rows}
    (output / 'frames.json').write_text(json.dumps(record, indent=1) + '\n')
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    record = build(args.output)
    print(f"{len(record['scenes'])} frame scenes written to {args.output}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
