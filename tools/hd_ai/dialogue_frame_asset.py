"""Redraw the resource-1296 dialogue border slices in high definition.

The ROM frame is four bands around a rounded rectangle, lit from the top left: white, silver and grey
on the top and left, light grey, grey and dark on the bottom and right, then a navy inner line. Raised
tabs with blue lights sit on the top and bottom edges, and two corners carry a vertical light. The
game places thirteen different 16x16 slices; several top and bottom slices differ only in where their
tab and light are.

The redraw keeps that design and its layout. The bands are drawn once as geometry for the whole
192x64 frame (smooth corners instead of pixel stairs, the bevel split along the corner diagonals) and
cut at each slice's place. The tabs and lights are read from each original slice, by column and row,
and drawn on top as shapes, so every replacement keeps its own decoration and runs that cross a slice
boundary join up.

    PYTHONPATH=src:. .venv/bin/python -B tools/hd_ai/dialogue_frame_asset.py \\
        --pack assets/hd-ai/worldmap-surfaces/pack-v1 --output build/hd-ai/dialogue-frame \\
        --art content/art/stage1-hd.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter
from srw64_rom.resources import ResourceTable
from tools.hd_ai.rt64_hash import ROOT, hasher

PALETTE = bytes.fromhex('22081095ffffc6338c693331ffc784016b61ad6df801f801f801f801f801f801')
# Source x of each slice in the strip, and where the game places it in the 192x64 frame.
LOCATIONS = {320: (0, 0), 336: (16, 0), 352: (16, 0), 48: (16, 0), 368: (16, 0), 384: (176, 0),
             464: (0, 16), 480: (176, 16), 400: (0, 48), 208: (16, 48),
             416: (16, 48), 432: (16, 48), 448: (176, 48)}
# ROM palette entries by role: highlight, silver, light grey, grey, dark, navy line, blue light.
ROLES = {0xffff: 'W', 0xc633: 's', 0xad6d: 'l', 0x8c69: 'g', 0x6b61: 'd', 0x1095: 'N', 0x3331: 'B'}

LIGHT = [(250, 251, 255), (206, 211, 225), (140, 146, 176)]   # outer -> inner, top and left
DARK = [(176, 182, 199), (132, 138, 166), (92, 98, 130)]      # outer -> inner, bottom and right
NAVY = (18, 21, 72)
TAB_TOP, TAB_BOTTOM = (246, 248, 253), (176, 182, 199)
GLASS_LIT, GLASS_DEEP, SPECULAR = (140, 192, 255), (30, 74, 182), (232, 243, 255)
SCALE, SS = 4, 4                     # replacement is 4x the slice; drawn 4x larger again, then reduced
U = SCALE * SS                       # drawing pixels per original pixel
OUTER = (1, 1, 191, 63)              # the frame's outer edge in the 192x64 layout
RADIUS = [2.6, 2.4, 2.4, 2.6, 3.0]   # corner radius at the outer edge and inside each band


def slice_roles(data: bytes, x: int) -> list[list[str]]:
    """The 16x16 CI4 slice at source x as roles ('.' is transparent)."""
    colours = struct.unpack('>16H', PALETTE)
    out = []
    for y in range(16):
        row = data[8 + y * 256 + x // 2:8 + y * 256 + x // 2 + 8]
        indices = [n for v in row for n in (v >> 4, v & 15)]
        out.append([ROLES.get(colours[n], '.') if colours[n] & 1 else '.' for n in indices])
    return out


def rounded(size, box, radius) -> Image.Image:
    mask = Image.new('L', size)
    ImageDraw.Draw(mask).rounded_rectangle([v * U for v in box], radius=radius * U, fill=255)
    return mask


def bands() -> Image.Image:
    """The whole 192x64 frame without decoration, at drawing resolution."""
    size = (192 * U, 64 * U)
    x0, y0, x1, y1 = OUTER
    rings = [rounded(size, (x0 + k, y0 + k, x1 - k, y1 - k), RADIUS[k]) for k in range(5)]
    split = Image.new('L', size)
    d = 10  # the bevel split runs diagonally out of each corner
    ImageDraw.Draw(split).polygon([(0, 0), (192 * U, 0), ((192 - d) * U, d * U), (d * U, (64 - d) * U), (0, 64 * U)],
                                  fill=255)
    split = split.filter(ImageFilter.GaussianBlur(0.5 * U))
    rgb = Image.new('RGB', size, NAVY)
    for k in range(3):
        rgb.paste(Image.composite(Image.new('RGB', size, LIGHT[k]), Image.new('RGB', size, DARK[k]), split), (0, 0), rings[k])
    rgb.paste(NAVY, (0, 0), rings[3])
    alpha = rings[0].copy()
    alpha.paste(0, (0, 0), rings[4])
    frame = rgb.convert('RGBA')
    frame.putalpha(alpha)
    return frame


def runs(cells, wanted) -> list[tuple[int, int]]:
    out, start = [], None
    for i, c in enumerate(list(cells) + ['.']):
        if c in wanted and start is None:
            start = i
        elif c not in wanted and start is not None:
            out.append((start, i))
            start = None
    return out


def glass(size, vertical) -> Image.Image:
    """Blue light fill: bright on the lit side, deep on the far side."""
    ramp = Image.linear_gradient('L')
    ramp = (ramp.transpose(Image.Transpose.ROTATE_90) if vertical else ramp).resize(size)
    return Image.composite(Image.new('RGB', size, GLASS_DEEP), Image.new('RGB', size, GLASS_LIT), ramp)


def light(tile, a, b, line, vertical, cap_lo, cap_hi):
    """A blue light from a to b along band row/column `line`: navy surround, glass, specular line.
    Ends at the slice edge run on flat so the neighbouring slice continues them."""
    size = tile.size
    lo = a - (1 if cap_lo else 0) - (2 if a == 0 else 0)
    hi = b + (1 if cap_hi else 0) + (2 if b == 16 else 0)
    glo, ghi = a - (2 if a == 0 else 0), b + (2 if b == 16 else 0)
    surround, shape, spec = Image.new('L', size), Image.new('L', size), Image.new('L', size)
    if vertical:
        ImageDraw.Draw(surround).rounded_rectangle([(line - .05) * U, lo * U, (line + 1.05) * U, hi * U], radius=.55 * U, fill=255)
        ImageDraw.Draw(shape).rounded_rectangle([(line + .12) * U, glo * U, (line + .88) * U, ghi * U], radius=.38 * U, fill=255)
        ImageDraw.Draw(spec).rectangle([(line + .3) * U, glo * U, (line + .42) * U, ghi * U], fill=170)
        fill = glass((int(.76 * U), size[1]), True)
        where = (int((line + .12) * U), 0)
    else:
        ImageDraw.Draw(surround).rounded_rectangle([lo * U, (line - .05) * U, hi * U, (line + 1.05) * U], radius=.55 * U, fill=255)
        ImageDraw.Draw(shape).rounded_rectangle([glo * U, (line + .12) * U, ghi * U, (line + .88) * U], radius=.38 * U, fill=255)
        ImageDraw.Draw(spec).rectangle([glo * U, (line + .28) * U, ghi * U, (line + .4) * U], fill=170)
        fill = glass((size[0], int(.76 * U)), False)
        where = (0, int((line + .12) * U))
    layer = Image.new('RGB', size)
    layer.paste(fill, where)
    tile.paste(NAVY, (0, 0), surround)
    tile.paste(layer, (0, 0), shape)
    spec = Image.composite(spec, Image.new('L', size), shape).filter(ImageFilter.GaussianBlur(.08 * U))
    tile.paste(SPECULAR, (0, 0), spec)


def decorate(tile: Image.Image, cells: list[list[str]], place: tuple[int, int]) -> None:
    px, py = place
    draw = ImageDraw.Draw(tile)
    alpha = tile.getchannel('A')
    reach = ImageDraw.Draw(alpha)
    # Raised tabs: the outermost row of a top slice (row 0) or a bottom slice (row 15).
    edges = [(0, TAB_TOP)] if py == 0 else [(15, TAB_BOTTOM)] if py == 48 else []
    for row, colour in edges:
        for a, b in runs(cells[row], set('WslgdNB')):
            y0, y1 = (row, row + 1.6) if row == 0 else (row - .6, row + 1)
            box = [(a - (2 if a == 0 else 0)) * U, y0 * U, (b + (2 if b == 16 else 0)) * U, y1 * U]
            reach.rounded_rectangle(box, radius=.45 * U, fill=255)
            draw.rounded_rectangle(box, radius=.45 * U, fill=colour)
    tile.putalpha(alpha)
    # Lights in the outermost band row (top and bottom slices) or band column (side slices).
    for row in [1] if py == 0 else [14] if py == 48 else []:
        for a, b in runs(cells[row], {'B'}):
            light(tile, a, b, row, False, a > 0 and cells[row][a - 1] == 'N', b < 16 and cells[row][b] == 'N')
    for col in [3] if px == 0 else [12] if px == 176 else []:
        for a, b in runs([cells[y][col] for y in range(16)], {'B'}):
            if b - a > 1:
                light(tile, a, b, col, True, True, True)


def frame_tiles(data: bytes) -> dict[int, Image.Image]:
    whole = bands()
    tiles = {}
    for x, (dx, dy) in LOCATIONS.items():
        tile = whole.crop((dx * U, dy * U, (dx + 16) * U, (dy + 16) * U))
        decorate(tile, slice_roles(data, x), (dx, dy))
        tiles[x] = tile.resize((16 * SCALE, 16 * SCALE), Image.Resampling.LANCZOS)
    return tiles


def preview(tiles: dict[int, Image.Image]) -> Image.Image:
    """The 192x64 frame at 4x with one plausible order of the decorated edge slices."""
    s = 16 * SCALE
    top = [336, 352, 48, 48, 48, 48, 368, 48, 48, 48]
    bottom = [208, 208, 208, 208, 208, 208, 208, 416, 432, 208]
    image = Image.new('RGBA', (12 * s, 4 * s))
    image.alpha_composite(tiles[320], (0, 0))
    image.alpha_composite(tiles[384], (11 * s, 0))
    image.alpha_composite(tiles[400], (0, 3 * s))
    image.alpha_composite(tiles[448], (11 * s, 3 * s))
    for i, x in enumerate(top):
        image.alpha_composite(tiles[x], ((i + 1) * s, 0))
    for i, x in enumerate(bottom):
        image.alpha_composite(tiles[x], ((i + 1) * s, 3 * s))
    for r in (1, 2):
        image.alpha_composite(tiles[464], (0, r * s))
        image.alpha_composite(tiles[480], (11 * s, r * s))
    return image


def rt64_hash(data: bytes, x: int, xxh) -> str:
    rows = [data[8 + y * 256 + x // 2:8 + y * 256 + x // 2 + 8] for y in range(16)]
    physical = b''.join(row[4:] + row[:4] if y % 2 else row for y, row in enumerate(rows))
    used = sorted({n for v in physical for n in (v >> 4, v & 15)})
    return xxh(physical + b''.join(PALETTE[n * 2:n * 2 + 2] * 4 for n in used) + struct.pack('<HHIHBB', 16, 16, 32768, 1, 0, 2))


def captured_hashes(capture: Path, data: bytes) -> tuple[int, dict[int, str]]:
    """The live RDRAM copy of the strip and the RT64 hashes of the real TMEM loads."""
    ram = (capture / 'latest-gfx-rdram.bin').read_bytes()
    address = ram.find(data)
    assert address >= 0 and ram.find(data, address + 1) < 0
    assert ram[0x2f5c00:0x2f5c20] == PALETTE
    captured = {}
    for path in (ROOT / 'build/recomp/font-probe/replay-original-1/textures').glob('*.rice.json'):
        load = json.loads(path.read_text())
        if load['texture']['address'] != 2788752:
            continue
        tile = json.loads(path.with_name(path.name.replace('.rice.json', '.tile.json')).read_text())
        assert (tile['width'], tile['height'], tile['tile']['line'], tile['tile']['fmt'], tile['tile']['siz']) == (16, 16, 1, 2, 0)
        tmem = path.with_name(path.name.replace('.rice.json', '.tmem')).read_bytes()
        assert b''.join(tmem[2048 + i * 8:2050 + i * 8] for i in range(16)) == PALETTE
        captured[load['tile']['uls'] // 2] = path.name.split('.')[0]
    assert captured.keys() == LOCATIONS.keys()
    return address, captured


def build_frame(pack: Path, output: Path, capture: Path | None = None) -> dict:
    data = ResourceTable((ROOT / 'rom.z64').read_bytes()).extract(1296)[0]
    assert struct.unpack_from('>4H', data) == (5, 512, 16, 0)
    address, captured = captured_hashes(capture, data) if capture else (None, None)
    tiles = frame_tiles(data)
    output.mkdir(parents=True, exist_ok=True)
    preview(tiles).save(output / 'dialogue-frame-768x256.png')
    xxh = hasher()
    textures = []
    for x, (dx, dy) in LOCATIONS.items():
        digest = rt64_hash(data, x, xxh)
        if captured:
            assert digest == captured[x], 'Frame source hash must match real RT64 TMEM'
        name = f'frame-resource1296-{digest}.png'
        tiles[x].save(pack / name)
        textures.append({'hashes': {'rt64': digest}, 'path': name, 'source_x': x, 'slice_xy': [dx, dy],
                         'sha256': hashlib.sha256((pack / name).read_bytes()).hexdigest()})
    return {'resource_id': 1296, 'decoded_sha256': hashlib.sha256(data).hexdigest(),
            'exact_live_rdram_address': address, 'original_slice_size': [16, 16], 'replacement_slice_size': [64, 64],
            'construction': 'Original design redrawn: rounded bevel bands, per-slice tabs and blue lights from the ROM slices',
            'captured_tmem_hashes_verified': len(captured) if captured else 0, 'textures': textures}


def update_art(path: Path, textures: list[dict]) -> int:
    """Rewrite the frame entries' sha256 in an art pack manifest in place (other lines untouched)."""
    text = path.read_text()
    changed = 0
    for item in textures:
        pattern = re.compile(r'("hash":\s*"%s",\s*"kind":\s*"frame",\s*"sha256":\s*")[0-9a-f]{64}(")' % item['hashes']['rt64'])
        text, n = pattern.subn(lambda m: m.group(1) + item['sha256'] + m.group(2), text)
        changed += n
    path.write_text(text)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pack', type=Path, required=True, help='RT64 pack directory to write the 13 slices into')
    parser.add_argument('--output', type=Path, required=True, help='directory for the preview and build record')
    parser.add_argument('--capture', type=Path, help='a capture directory to check the hashes against real TMEM loads')
    parser.add_argument('--art', type=Path, help='art pack manifest whose frame sha256 values to update')
    args = parser.parse_args()
    record = build_frame(args.pack, args.output, args.capture)
    (args.output / 'frame.json').write_text(json.dumps(record, indent=1) + '\n')
    if args.art:
        changed = update_art(args.art, record['textures'])
        print(f'{args.art}: {changed} frame entries updated')
    print(f"{len(record['textures'])} slices written to {args.pack}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
