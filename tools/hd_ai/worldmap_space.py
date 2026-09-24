"""HD space region of the story world map (5599) and its starfield (5582).

Space is the most used world-map region in the story (369 of the 827
3D32/3D33 placements, 32 of the 127 locations). It is drawn from:
- the starfield 5582 (CI4 320x240, palette 5583) on sprite slot 0 behind
  everything (native_background.cpp draws it whole, like the intermission
  backgrounds);
- billboards in 5599: the Earth (part 4, 4x4 tiles of 64x64 CI4), the Moon
  (part 5, 2x2), four asteroids (parts 13-16, 32x32);
- two small textured models (parts 1 and 3, a 32x32 texture pair each);
- seven name plates (parts 6-12, Side 1/2/3/5/6/7 and Sweetwater): text, redrawn
  per language by the native model pack (tools/models/build_native_models.py BOARDS).

`prepare` assembles each billboard (Y up) and packs the 32x32 textures into
one grid; `run` sends one qwen-image-3.0-pro request per picture; `compose`
registers the outputs, keeps each picture's alpha from its source mask
(smoothed) and cuts the RT64 tiles; `pack` adds them to a pack and the
starfield to the whole-background set.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time

from PIL import Image, ImageFilter

from srw64_rom.resources import ResourceTable
from tools.hd_ai.aliyun import ROOT, load_env, run_one
from tools.hd_ai.rom_images import indexed, rgba16
from tools.hd_ai.rt64_hash import hasher
from tools.hd_ai.worldmap_surfaces import parts, place, register

SPACE, STARFIELD, STARFIELD_PALETTE = 5599, 5582, 5583
BILLBOARDS = {'earth-globe': 4, 'moon': 5}
SMALL = {'asteroid-1': 13, 'asteroid-2': 14, 'asteroid-3': 15, 'asteroid-4': 16, 'model-a': 1, 'model-b': 3}
SCALE = 8
GRID_CELL, GRID_GUTTER = 32, 16
MODEL, CANDIDATE = 'qwen-image-3.0-pro', 1
PROMPTS = {
    'billboard': ('忠实高清修复图1这张1999年游戏里的天体贴图（黑色是太空背景）。严格保留天体的轮廓、大小、位置、'
                  '云层与地形图案、颜色和光照方向，只把低分辨率像素恢复为清晰细腻的高清写实细节。'
                  '背景保持纯黑，不添加星星、文字或任何新物体，不改变画幅。'),
    'grid': ('图1是按网格排列的几张互不相关的1999年游戏小贴图（陨石和太空设施的表面），格子之间是纯黑色。'
             '逐格忠实高清修复：保留每格的形状、轮廓、颜色和明暗，只把低分辨率像素恢复为清晰细腻的高清细节。'
             '各格互不影响，不越过格子，背景保持纯黑，不添加文字或新物体，不改变画幅。'),
    'starfield': ('忠实高清修复图1这张1999年游戏的太空星空背景。严格保留每一片星云和亮星的位置、形状、颜色与亮度，'
                  '只把低分辨率像素恢复为清晰细腻的高清星空，星点锐利、星云细腻。不添加行星、文字或新天体，不改变画幅。'),
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tiles_of(d: bytes, start: int) -> list[dict]:
    """Every textured quad of one part: vertices, texture/palette offsets and size."""
    cache, pal, tex, size, out = {}, None, None, None, []
    for pos in range(start, len(d) - 8, 8):
        w0, w1 = struct.unpack_from('>II', d, pos)
        op = w0 >> 24
        if op == 0xDF:
            break
        if op == 0x01:
            count = (w0 >> 12) & 255
            first = ((w0 >> 1) & 127) - count
            for k in range(count):
                cache[first + k] = struct.unpack_from('>hhhHhh4B', d, (w1 & 0xFFFFFF) + k * 16)
        elif op == 0xFD and w1 >> 24 == 4:
            if (w0 >> 21) & 7 == 0:
                pal = w1 & 0xFFFFFF
            else:
                tex = w1 & 0xFFFFFF
        elif op == 0xF2 and (w1 >> 24) & 7 == 0:
            size = (((w1 >> 12) & 4095) // 4 + 1, (w1 & 4095) // 4 + 1)
        elif op in (0x05, 0x06):
            ids = sorted({(w >> s & 255) // 2 for w in ((w0, w1) if op == 6 else (w0,)) for s in (16, 8, 0)})
            out.append({'verts': [cache[i] for i in ids], 'tex': tex, 'pal': pal, 'size': size})
    return out


def texture(d: bytes, t: dict) -> Image.Image:
    w, h = t['size']
    return indexed(d[t['tex']:t['tex'] + w * h // 2], (w, h), rgba16(d[t['pal']:t['pal'] + 32]), 4)


def billboard(d: bytes, part: int) -> tuple[Image.Image, list[dict]]:
    """A billboard's tiles placed by vertex X/Y (Y up), with their UV flips."""
    qs = [q for q in tiles_of(d, parts(d)[part][2]) if len(q['verts']) == 4]
    size = qs[0]['size']
    xs = sorted({round(min(v[0] for v in q['verts'])) for q in qs})
    ys = sorted({round(max(v[1] for v in q['verts'])) for q in qs}, reverse=True)
    atlas = Image.new('RGBA', (len(xs) * size[0], len(ys) * size[1]))
    placed = []
    for q in qs:
        vx = [v[0] for v in q['verts']]; vy = [v[1] for v in q['verts']]
        left = [v for v in q['verts'] if v[0] == min(vx)]
        top = [v for v in q['verts'] if v[1] == max(vy)]
        flip_x = min(v[4] for v in left) > 32 * size[0] // 2
        flip_y = min(v[5] for v in top) > 32 * size[1] // 2
        tile = texture(d, q)
        if flip_x: tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if flip_y: tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        xy = (xs.index(round(min(vx))) * size[0], ys.index(round(max(vy))) * size[1])
        atlas.alpha_composite(tile, xy)
        placed.append({'xy': list(xy), 'flip': [flip_x, flip_y], 'tex': q['tex'], 'pal': q['pal'], 'size': list(size)})
    return atlas, placed


def ci4_hash(pixels: bytes, palette: bytes, size: tuple[int, int], xxh) -> str:
    """RT64 v5 hash of a CI4 tile loaded as 16-bit LoadBlock (rt64_hash.map_hash for any width)."""
    w, h = size
    row_bytes = w // 2
    rows = []
    for y in range(h):
        row = pixels[y * row_bytes:(y + 1) * row_bytes]
        if y % 2:
            row = b''.join(row[x + 4:x + 8] + row[x:x + 4] for x in range(0, row_bytes, 8))
        rows.append(row)
    data = b''.join(rows)
    used = sorted({n for v in data for n in (v >> 4, v & 15)})
    data += b''.join(palette[i * 2:i * 2 + 2] * 4 for i in used)
    return xxh(data + struct.pack('<HHIHBB', w, h, 32768, row_bytes // 8, 0, 2))


def on_black(image: Image.Image) -> Image.Image:
    base = Image.new('RGBA', image.size, (0, 0, 0, 255))
    base.alpha_composite(image)
    return base.convert('RGB')


def small_grid(images: list[Image.Image]) -> tuple[Image.Image, list[tuple[int, int]]]:
    cols = 3
    rows = -(-len(images) // cols)
    step = GRID_CELL + GRID_GUTTER
    grid = Image.new('RGBA', (GRID_GUTTER + cols * step, GRID_GUTTER + rows * step))
    origins = []
    for i, image in enumerate(images):
        xy = (GRID_GUTTER + (i % cols) * step, GRID_GUTTER + (i // cols) * step)
        grid.alpha_composite(image, xy)
        origins.append(xy)
    return grid, origins


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    d = table.extract(SPACE)[0]
    samples, pictures = [], {}

    def request(name: str, source: Image.Image, prompt: str, scale: int) -> None:
        source_path = out / 'inputs' / f'{name}-source.png'
        source.save(source_path)
        flat = on_black(source)
        path = out / 'inputs' / f'{name}.png'
        big = flat.resize((flat.width * scale, flat.height * scale), Image.Resampling.NEAREST)
        big.save(path)
        w, h = big.size
        k = min(1.0, 2048 / max(w, h))
        samples.append({'id': name, 'input': str(path.relative_to(out)), 'input_sha256': sha(path),
                        'output_size': f'{round(w * k)}*{round(h * k)}', 'prompt': PROMPTS[prompt]})
        pictures[name] = {'source': str(source_path.relative_to(out))}

    for name, part in BILLBOARDS.items():
        atlas, placed = billboard(d, part)
        request(name, atlas, 'billboard', 2048 // max(atlas.size))
        pictures[name]['tiles'] = placed
    smalls, small_rows = [], []
    for name, part in SMALL.items():
        for i, t in enumerate(q for q in tiles_of(d, parts(d)[part][2]) if q['tex'] is not None):
            key = (t['tex'], t['pal'])
            if key in [(r['tex'], r['pal']) for r in small_rows]:
                continue
            smalls.append(texture(d, t))
            small_rows.append({'name': f'{name}-{i}', 'tex': t['tex'], 'pal': t['pal'], 'size': list(t['size'])})
    grid, origins = small_grid(smalls)
    for row, xy in zip(small_rows, origins):
        row['origin'] = list(xy)
    request('small-textures', grid, 'grid', 2048 // max(grid.size))
    pictures['small-textures']['cells'] = small_rows
    stars = indexed(table.extract(STARFIELD)[0][8:8 + 320 * 240 // 2], (320, 240),
                    rgba16(table.extract(STARFIELD_PALETTE)[0][8:8 + 32]), 4)
    request('starfield', stars, 'starfield', 6)
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': 'story world map, space region and starfield', 'pictures': pictures, 'samples': samples},
        ensure_ascii=False, indent=2) + '\n')
    print({'requests': len(samples), 'estimated_cny': round(len(samples) * .52, 2),
           'sizes': {s['id']: s['output_size'] for s in samples}})


def run(args: argparse.Namespace) -> None:
    out = args.output
    config = load_env(args.env_file)
    for sample in json.loads((out / 'samples.json').read_text())['samples']:
        folder = out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}"
        for _ in range(3):
            report = run_one(out, sample, MODEL, CANDIDATE, config)
            print(json.dumps({k: report.get(k) for k in ('sample_id', 'status', 'http_status', 'error_code', 'elapsed_seconds')}), flush=True)
            if report['status'] == 'completed' or not (report.get('http_status') == 400 and report.get('error_code') == 'InvalidParameter'):
                break
            (out / 'rejected').mkdir(exist_ok=True)
            shutil.move(folder, out / 'rejected' / f'{folder.name}--400-{int(time.time())}')
            time.sleep(5)
        if report['status'] != 'completed':
            raise SystemExit(f"stopped at {sample['id']}: {report['status']}")
        time.sleep(13)


def smooth_alpha(source: Image.Image, scale: int) -> Image.Image:
    alpha = source.getchannel('A').point(lambda v: 255 if v else 0)
    big = alpha.resize((alpha.width * scale, alpha.height * scale), Image.Resampling.BICUBIC)
    big = big.filter(ImageFilter.GaussianBlur(scale * .35))
    return big.point(lambda v: max(0, min(255, int((v - 128) * 4 + 128))))


def hd_picture(out: Path, name: str, source: Image.Image) -> Image.Image:
    """The model output registered to the source at SCALE, alpha from the source mask."""
    request = json.loads((out / 'runs' / f'{name}--{MODEL}--{CANDIDATE}' / 'request.json').read_text())
    output = Image.open(out / request['output'])
    fit = register(output, on_black(source))
    size = (source.width * SCALE, source.height * SCALE)
    hd = place(output, fit, size).convert('RGBA')
    alpha = smooth_alpha(source, SCALE)
    hd.putalpha(alpha)
    # transparent texels carry black, the colour behind the picture
    black = Image.new('RGBA', size, (0, 0, 0, 0))
    return Image.composite(hd, black, alpha.point(lambda v: 255 if v else 0))


def compose(args: argparse.Namespace) -> None:
    out = args.output
    spec = json.loads((out / 'samples.json').read_text())
    (out / 'compose').mkdir(exist_ok=True)
    for name, picture in spec['pictures'].items():
        source = Image.open(out / picture['source']).convert('RGBA')
        if name == 'starfield':
            request = json.loads((out / 'runs' / f'{name}--{MODEL}--{CANDIDATE}' / 'request.json').read_text())
            output = Image.open(out / request['output'])
            fit = register(output, on_black(source))
            place(output, fit, (1920, 1440)).save(out / 'compose' / 'starfield.png')
            continue
        hd_picture(out, name, source).save(out / 'compose' / f'{name}.png')
        print(name, 'composed')


def pack(args: argparse.Namespace) -> None:
    out = args.output
    spec = json.loads((out / 'samples.json').read_text())
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    d = table.extract(SPACE)[0]
    xxh = hasher()
    target = args.pack
    database = json.loads((target / 'rt64.json').read_text())
    entries = {e['hashes']['rt64']: e for e in database['textures']}
    added = {}
    for name, picture in spec['pictures'].items():
        if name == 'starfield':
            continue
        hd = Image.open(out / 'compose' / f'{name}.png').convert('RGBA')
        rows = picture.get('tiles') or picture['cells']
        for row in rows:
            w, h = row['size']
            x, y = row.get('xy') or row['origin']
            tile = hd.crop((x * SCALE, y * SCALE, (x + w) * SCALE, (y + h) * SCALE))
            flip = row.get('flip', [False, False])
            if flip[0]: tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip[1]: tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            digest = ci4_hash(d[row['tex']:row['tex'] + w * h // 2], d[row['pal']:row['pal'] + 32], (w, h), xxh)
            if digest in entries and not entries[digest]['path'].startswith('space-'):
                raise ValueError(f'{digest} already names another texture')
            file = f'space-{digest}.png'
            tile.save(target / file)
            entries[digest] = {'hashes': {'rt64': digest}, 'path': file}
            added[digest] = sha(target / file)
    database['textures'] = sorted(entries.values(), key=lambda e: e['hashes']['rt64'])
    (target / 'rt64.json').write_text(json.dumps(database, indent=2) + '\n')
    # The starfield joins the whole-image backgrounds (native_background.cpp).
    backgrounds = args.backgrounds_to
    shutil.copytree(args.backgrounds_from, backgrounds)
    index = json.loads((backgrounds / 'backgrounds.json').read_text())
    name = f'background-{STARFIELD}-{STARFIELD_PALETTE}.png'
    Image.open(out / 'compose' / 'starfield.png').convert('RGB').save(backgrounds / name)
    index['images'] = [r for r in index['images'] if r['image'] != STARFIELD]
    index['images'].append({'image': STARFIELD, 'palette': STARFIELD_PALETTE, 'file': name, 'sha256': sha(backgrounds / name),
                            'model': MODEL})
    (backgrounds / 'backgrounds.json').write_text(json.dumps(index, indent=2) + '\n')
    (out / 'pack-report.json').write_text(json.dumps({'space_tiles': added}, indent=2) + '\n')
    print({'space_tiles': len(added), 'backgrounds': len(index['images'])})
    if args.bind:
        path = ROOT / 'content/art/stage1-hd.json'
        manifest = json.loads(path.read_text())
        rows = [r for r in manifest['textures'] if r['kind'] != 'space']
        rows += [{'hash': h, 'kind': 'space', 'sha256': v} for h, v in sorted(added.items())]
        manifest['textures'] = rows
        manifest['source'] = {'path': str(target.resolve().relative_to(ROOT)), 'manifest_sha256': sha(target / 'rt64.json')}
        manifest['backgrounds'] = {'path': str(backgrounds.resolve().relative_to(ROOT)), 'manifest_sha256': sha(backgrounds / 'backgrounds.json')}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', path.relative_to(ROOT), len(rows), 'textures')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'run', 'compose', 'pack'):
        sub = commands.add_parser(name)
        sub.add_argument('--output', type=Path, required=True)
        if name == 'run':
            sub.add_argument('--env-file', type=Path, required=True)
        if name == 'pack':
            sub.add_argument('--pack', type=Path, required=True, help='RT64 pack folder to add the space tiles to')
            sub.add_argument('--backgrounds-from', type=Path, required=True)
            sub.add_argument('--backgrounds-to', type=Path, required=True)
            sub.add_argument('--bind', action='store_true')
    args = parser.parse_args()
    {'prepare': prepare, 'run': run, 'compose': compose, 'pack': pack}[args.command](args)


if __name__ == '__main__':
    main()
