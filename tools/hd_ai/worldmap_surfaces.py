"""HD Earth surfaces of the story world map: every tile of every region.

The story scenes are staged on the world map overlay (load_000A7EC0). Its model
table 801C5670 names the region surfaces: 5602/5603 the whole Earth (identical
tiles, two meshes), 5604 the Mediterranean, 5605 Central Asia, 5606 a coast, and
5599 space. Each Earth surface is a flat mesh of 64x64 CI4 tiles with their own
16-colour palettes; the transparent ocean shows the clear colour RGB(0,55,90).
The location table 801C5310 has 127 entries (surface, x, y); the scripts' 827
3D32/3D33 placements use space 369 times, 5602/5603 348, 5606 70, 5604 (the first
stage, 57 approved tiles) 28 and 5605 12.

`prepare` assembles each surface into one atlas (north up) and cuts it into
overlapping 256x256 windows, one qwen-image-3.0-pro request each at 2048x2048
(8x). `compose` registers every window to its source, blends the overlaps (optionally
locking the source's low-frequency colour, --colour-lock), and takes the coastline
from the source mask, smoothed, instead of the model's. `pack` cuts the HD
atlas back into 512x512 tiles under their RT64 hashes (rt64_hash.map_hash).

5604 was first drawn outside this pipeline (worldmap-runtime/pack-v6, a built-in
image_gen); its land swatches are still the style reference `prepare` sends. Since
2026-09-25 5604 is that image_gen map redrawn by qwen with more detail
(worldmap-surfaces/europe-2: its 9 windows cut from approved_canvas are the inputs,
samples.json holds them); `restyle` redraws the other surfaces from run-5 toward
the same style. `pack --extra-run` joins the runs, and the pack-v6 tiles leave the pack.
The user preferred image_gen over both: `imagegen` composes the Codex image_gen kit
(assets/hd-ai/imagegen-kit), a whole picture and 3:2 windows per surface.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import time

import colorsys

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageMath, ImageStat

from srw64_rom.resources import ResourceTable
from tools.hd_ai.aliyun import ROOT, load_env, run_one
from tools.hd_ai.rom_images import indexed, rgba16
from tools.hd_ai.rt64_hash import hasher, map_hash

SURFACES = {5602: 'earth', 5605: 'central-asia', 5606: 'coast'}
# 5604's first-stage tiles (worldmap-runtime/pack-v6): their painted style is the reference
# every surface is redrawn in. A run that redraws all of 5604 replaces them in `pack`.
APPROVED = 5604
# ...except five land tiles in its top-right corner (Russia) that never got HD: one window
# there, packed only for tiles without an approved replacement.
FILL = {5604: ('mediterranean', (320, 0, 576, 256))}
SAME_TILES = {5603: 5602}                 # identical tiles on another mesh
OCEAN = (0, 55, 90)                       # 801C3490 clear colour behind the transparent sea
SCALE = 8                                 # HD px per source texel; 512 px tiles
WINDOW, OVERLAP = 256, 48                 # source px
MODEL = 'qwen-image-3.0-pro'
UNIT = 611 / 64                           # world units per texel
COLOUR_RADIUS = 12                        # HD px of low-frequency colour kept from the source
PROMPT = ('图1是1999年游戏里世界地图的一块低分辨率局部，深蓝色部分是海。图2是同一游戏高清地图的画风样张，'
          '由四块地貌纹理拼成（森林、山地、沙丘、沙漠山地），不是地图的一部分，只参考它的笔触、质感和色彩。'
          '把图1重绘成高清手绘游戏地图：山地画成一簇簇带阴影的山峰，森林画成成片的树丛，沙漠画出沙丘纹理，草原平整鲜亮。'
          '严格保留图1每一处海岸线、岛屿、湖泊的位置和形状，以及山地、森林、沙漠、草原、雪地各自的分布范围：'
          '只在图1有明显起伏阴影纹理的地方画山，平坦的区域不要加山。海面保持与图1相同的纯色深蓝，'
          '不添加新的陆地、文字、网格、城市、道路或边框，不改变画幅。')
# Land-only 384 px crops of the approved 5604 canvas (8x): forest, mountains, dunes,
# desert peaks. A whole map crop as the reference got its coastlines pasted in.
SWATCHES = ((2300, 500), (1500, 1050), (1000, 2700), (1750, 2550))
LAND_IOU = .90                             # least land-mask agreement before a retry
CANDIDATES = 3
# `restyle`: a surface's HD windows redrawn toward the first-stage (image_gen) style.
RESTYLE_PROMPT = ('图1是游戏世界地图的一块高清局部，地理位置和地貌都正确，但画法偏写实、偏灰。图2是目标画法的样张，'
                  '由森林、山地、沙丘、沙漠山地四种地貌纹理拼成，不是地图的一部分。只改变画法，不改变内容：用图2那种饱满的手绘笔触重画图1，'
                  '山地画成一簇簇有受光面和投影的立体山峰，树冠分明，沙丘纹理细腻。每一处的颜色和地貌类型必须与图1相同：'
                  '图1里褐色、土黄色的高原和山地仍画成褐色、土黄色的岩石山地（高处可带少量积雪），黄色的地方仍是沙漠，'
                  '只有图1里本来是绿色的地方才画草原和森林。不要把褐色或黄色的地方画成绿色，不要新增湖泊、河流、海、城市、道路、文字或边框。'
                  '严格保持每一处海岸线的位置和形状，海面保持与图1相同的纯色深蓝，不改变画幅。')
KIT_COLOUR_RADIUS = 48                     # HD px: an image_gen kit's whole picture sets the colour above this scale
KIT_BASE_WEIGHT = .02                      # the whole picture's blend weight under the windows
RESTYLE_COLOUR = 1.3                      # saturation boost of the input, so the terrain colours read clearly


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parts(d: bytes) -> list[tuple[int, int, int]]:
    n = struct.unpack_from('>I', d, 4)[0]
    return [struct.unpack_from('>III', d, 12 + 12 * i) for i in range(n)]


def quads(d: bytes, start: int) -> list[dict]:
    cache, pal, tex, out = {}, None, None, []
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
        elif op == 0x06:
            ids = sorted({(w >> s & 255) // 2 for w in (w0, w1) for s in (16, 8, 0)})
            out.append({'verts': [cache[i] for i in ids], 'tex': tex, 'pal': pal})
    return out


def assemble(d: bytes) -> tuple[Image.Image, list[dict]]:
    """Atlas of every type-0 part's quads, north up, and where each tile sits in it."""
    qs = [q for kind, _, off in parts(d) if kind == 0 for q in quads(d, off)]
    x0 = min(v[0] for q in qs for v in q['verts']); x1 = max(v[0] for q in qs for v in q['verts'])
    z0 = min(v[2] for q in qs for v in q['verts']); z1 = max(v[2] for q in qs for v in q['verts'])
    atlas = Image.new('RGBA', (round((x1 - x0) / UNIT), round((z1 - z0) / UNIT)))
    tiles = []
    for q in qs:
        xs = [v[0] for v in q['verts']]; zs = [v[2] for v in q['verts']]
        left = [v for v in q['verts'] if abs(v[0] - min(xs)) <= 4]
        low = [v for v in q['verts'] if abs(v[2] - min(zs)) <= 4]
        # u runs with +x unless the vertex at min x carries u = 64; v runs with +z unless
        # the vertex at min z carries v = 64. North is -z, so +z is down the atlas.
        flip_x = min(v[4] for v in left) > 1000
        flip_y = min(v[5] for v in low) > 1000
        tile = indexed(d[q['tex']:q['tex'] + 2048], (64, 64), rgba16(d[q['pal']:q['pal'] + 32]), 4)
        if flip_x: tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if flip_y: tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        xy = (round((min(xs) - x0) / UNIT), round((min(zs) - z0) / UNIT))
        atlas.alpha_composite(tile, xy)
        tiles.append({'xy': list(xy), 'flip': [flip_x, flip_y], 'tex': q['tex'], 'pal': q['pal']})
    return atlas, tiles


def approved_tiles(table: ResourceTable) -> dict[str, Path]:
    """The reviewed first-stage 5604 tiles by RT64 hash (pack-v6)."""
    pack = ROOT / 'assets/hd-ai/worldmap-runtime/pack-v6/pack'
    spec = json.loads((pack / 'srw64-worldmap-hd.json').read_text())
    paths = {e['hashes']['rt64']: e['path'] for e in json.loads((pack / 'rt64.json').read_text())['textures']}
    return {h: pack / paths[h] for h in spec['hashes']}


def approved_canvas(table: ResourceTable) -> Image.Image:
    """5604 at 8x from the reviewed tiles, the source (nearest) where there is none."""
    d = table.extract(APPROVED)[0]
    atlas, tiles = assemble(d)
    tiles_hd = approved_tiles(table)
    xxh = hasher()
    canvas = atlas.resize((atlas.width * SCALE, atlas.height * SCALE), Image.Resampling.NEAREST)
    base = Image.new('RGBA', canvas.size, OCEAN + (255,))
    base.alpha_composite(canvas)
    for t in tiles:
        digest = map_hash(d[t['tex']:t['tex'] + 2048], d[t['pal']:t['pal'] + 32], xxh)
        if digest not in tiles_hd:
            continue
        tile = Image.open(tiles_hd[digest]).convert('RGBA').resize((64 * SCALE, 64 * SCALE), Image.Resampling.LANCZOS)
        if t['flip'][0]: tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if t['flip'][1]: tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        cell = Image.new('RGBA', tile.size, OCEAN + (255,))
        cell.alpha_composite(tile)
        base.paste(cell, (t['xy'][0] * SCALE, t['xy'][1] * SCALE))
    return base


def windows(size: tuple[int, int]) -> list[tuple[int, int, int, int]]:
    def starts(length: int) -> list[int]:
        if length <= WINDOW:
            return [0]
        n = math.ceil((length - WINDOW) / (WINDOW - OVERLAP)) + 1
        return [round(i * (length - WINDOW) / (n - 1)) for i in range(n)]
    return [(x, y, min(x + WINDOW, size[0]), min(y + WINDOW, size[1])) for y in starts(size[1]) for x in starts(size[0])]


def flat(atlas: Image.Image) -> Image.Image:
    base = Image.new('RGBA', atlas.size, OCEAN + (255,))
    base.alpha_composite(atlas)
    return base.convert('RGB')


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    samples, surfaces = [], {}
    # Style sample (图2). A whole map crop of 5604 got pasted in (Central Asia became the
    # Mediterranean, the Pacific grew a desert continent), so only land textures go in.
    reference = out / 'inputs' / 'style-reference.png'
    canvas = approved_canvas(table).convert('RGB')
    sheet = Image.new('RGB', (1024, 1024), (128, 128, 128))
    for i, (x, y) in enumerate(SWATCHES):
        sheet.paste(canvas.crop((x, y, x + 384, y + 384)).resize((504, 504), Image.Resampling.LANCZOS), (4 + (i % 2) * 516, 4 + (i // 2) * 516))
    sheet.save(reference)
    for rid, name in list(SURFACES.items()) + [(r, n) for r, (n, _) in FILL.items()]:
        atlas, tiles = assemble(table.extract(rid)[0])
        path = out / 'inputs' / f'{name}-source.png'
        atlas.save(path)
        surfaces[name] = {'resource': rid, 'source': str(path.relative_to(out)), 'size': list(atlas.size)}
        if rid in FILL:
            surfaces[name]['fill_only'] = list(FILL[rid][1])
        for i, box in enumerate([FILL[rid][1]] if rid in FILL else windows(atlas.size)):
            crop = flat(atlas).crop(box)
            request = out / 'inputs' / f'{name}-{i:02d}.png'
            crop.resize((crop.width * SCALE, crop.height * SCALE), Image.Resampling.NEAREST).save(request)
            w, h = crop.width * SCALE, crop.height * SCALE
            samples.append({'id': f'{name}-{i:02d}', 'surface': name, 'box': list(box), 'input': str(request.relative_to(out)),
                            'input_sha256': sha(request), 'output_size': f'{w}*{h}', 'prompt': PROMPT,
                            'references': [str(reference.relative_to(out))]})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': 'story world map Earth surfaces, 256 px windows at 8x', 'surfaces': surfaces, 'samples': samples},
        ensure_ascii=False, indent=2) + '\n')
    print({'surfaces': len(surfaces), 'requests': len(samples), 'estimated_cny': round(len(samples) * .52, 2)})


def land_iou(source: Image.Image, output: Image.Image) -> float:
    """Agreement of the land masks at source resolution, away from the coast: the model
    draws a light shallow-water rim along every coast, which says nothing about added or
    moved land, so pixels within one source pixel of the source coastline are ignored."""
    small = output.convert('RGB').resize(source.size, Image.Resampling.BOX)
    sea = Image.new('L', source.size)
    sea.putdata([255 if max(abs(p[i] - OCEAN[i]) for i in range(3)) < 24 else 0 for p in source.get_flattened_data()])
    sea_src = [v > 0 for v in sea.get_flattened_data()]
    if all(sea_src) or not any(sea_src):
        return 1.0
    edge = ImageChops.difference(sea.filter(ImageFilter.MaxFilter(3)), sea.filter(ImageFilter.MinFilter(3)))
    far = [v == 0 for v in edge.get_flattened_data()]
    ocean = [p for p, s_ in zip(small.get_flattened_data(), sea_src) if s_]
    colour = tuple(sorted(p[i] for p in ocean)[len(ocean) // 2] for i in range(3))
    sea_out = [max(abs(p[i] - colour[i]) for i in range(3)) < 30 for p in small.get_flattened_data()]
    both = sum(1 for a, b, f in zip(sea_src, sea_out, far) if f and not a and not b)
    either = sum(1 for a, b, f in zip(sea_src, sea_out, far) if f and (not a or not b))
    return both / either if either else 1.0


def terrain(image: Image.Image, size: int = 96) -> list[str]:
    """Coarse terrain class per cell: water, snow, green, sand or rock."""
    out = []
    for r, g, b in image.convert('RGB').resize((size, size), Image.Resampling.BOX).get_flattened_data():
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        h *= 360
        if b > r + 25 and b > g and v < .75 and s > .35: out.append('water')
        elif s < .18 and v > .72: out.append('snow')
        elif 65 <= h <= 170 and s > .2: out.append('green')
        elif 38 <= h < 65 and v > .62: out.append('sand')
        else: out.append('rock')
    return out


def terrain_agreement(source: Image.Image, output: Image.Image) -> tuple[float, float]:
    """Share of the source's land keeping its class (rock and snow count as one), and share turned to water."""
    a, b = terrain(source), terrain(output)
    land = [i for i, c in enumerate(a) if c != 'water']
    same = sum(1 for i in land if a[i] == b[i] or {a[i], b[i]} <= {'rock', 'snow'})
    water = sum(1 for i in land if b[i] == 'water')
    return same / max(len(land), 1), water / max(len(land), 1)


def restyle(args: argparse.Namespace) -> None:
    """Windows of a composed surface as 图1, the style swatches as 图2 (run with the terrain check)."""
    out, base, name = args.output, getattr(args, 'from'), args.surface
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    spec = json.loads((base / 'samples.json').read_text())
    surface = dict(spec['surfaces'][name])
    shutil.copyfile(base / surface['source'], out / 'inputs' / f'{name}-source.png')
    surface['source'] = f'inputs/{name}-source.png'
    reference = out / 'inputs' / 'style-reference.png'
    shutil.copyfile(base / 'inputs' / 'style-reference.png', reference)
    hd = Image.open(base / json.loads((base / 'compose' / 'report.json').read_text())[name]['file']).convert('RGBA')
    whole = Image.new('RGBA', hd.size, OCEAN + (255,))
    whole.alpha_composite(hd)
    whole = ImageEnhance.Color(whole.convert('RGB')).enhance(RESTYLE_COLOUR)
    samples = []
    for sample in spec['samples']:
        if sample['surface'] != name:
            continue
        request = out / 'inputs' / f"{sample['id']}.png"
        crop = whole.crop(tuple(v * SCALE for v in sample['box']))
        crop.save(request)
        samples.append({'id': sample['id'], 'surface': name, 'box': sample['box'], 'input': str(request.relative_to(out)),
                        'input_sha256': sha(request), 'output_size': f'{crop.width}*{crop.height}', 'prompt': RESTYLE_PROMPT,
                        'references': [str(reference.relative_to(out))], 'check': 'terrain'})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': f'{name} from {base.name} redrawn toward the first-stage style', 'surfaces': {name: surface},
        'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({'surface': name, 'requests': len(samples), 'estimated_cny': round(len(samples) * .52, 2)})


def run(args: argparse.Namespace) -> None:
    out = args.output
    config = load_env(args.env_file)
    choice_path = out / 'choices.json'
    choices = json.loads(choice_path.read_text()) if choice_path.exists() else {}
    for sample in json.loads((out / 'samples.json').read_text())['samples']:
        if sample['id'] in choices:
            continue
        source = Image.open(out / sample['input']).convert('RGB').resize(
            ((sample['box'][2] - sample['box'][0]), (sample['box'][3] - sample['box'][1])), Image.Resampling.BOX)
        tried = []
        for candidate in range(1, CANDIDATES + 1):
            folder = out / 'runs' / f"{sample['id']}--{MODEL}--{candidate}"
            for _ in range(3):
                report = run_one(out, sample, MODEL, candidate, config)
                if report['status'] == 'completed' or not (report.get('http_status') == 400 and report.get('error_code') == 'InvalidParameter'):
                    break
                (out / 'rejected').mkdir(exist_ok=True)
                shutil.move(folder, out / 'rejected' / f'{folder.name}--400-{int(time.time())}')
                time.sleep(5)
            if report['status'] != 'completed':
                raise SystemExit(f"stopped at {sample['id']}: {report['status']}")
            output = Image.open(out / report['output'])
            iou = land_iou(source, output)
            ok = iou >= LAND_IOU
            row = [candidate, round(iou, 4)]
            if sample.get('check') == 'terrain':
                # Recorded for the visual review only: texture changes the colour classes as much
                # as a real terrain swap does, so these numbers cannot pick a candidate.
                agree, water = terrain_agreement(Image.open(out / sample['input']), output)
                row += [round(agree, 4), round(water, 4)]
            tried.append(row)
            print(json.dumps({'sample_id': sample['id'], 'candidate': candidate, 'land_iou': round(iou, 4),
                              **({'terrain': row[2], 'new_water': row[3]} if len(row) > 2 else {}),
                              'elapsed_seconds': report.get('elapsed_seconds')}), flush=True)
            if ok:
                break
            time.sleep(13)
        best = max(tried, key=lambda r: r[1])
        choices[sample['id']] = {'candidate': best[0], 'land_iou': best[1], 'tried': tried}
        choice_path.write_text(json.dumps(choices, indent=2) + '\n')
        time.sleep(13)


def _shift(ref: Image.Image, cand: Image.Image, box: tuple, radius: int) -> tuple[int, int]:
    base = ref.crop(box)
    best = None
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            e = ImageStat.Stat(ImageChops.difference(base, cand.crop((box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy)))).mean[0]
            if best is None or e < best[0]:
                best = (e, dx, dy)
    return best[1], best[2]


def _line(points):
    n = len(points); mp = sum(p for p, _ in points) / n; mq = sum(q for _, q in points) / n
    spread = sum((p - mp) ** 2 for p, _ in points)
    slope = sum((p - mp) * (q - mq) for p, q in points) / spread if spread else 0.0
    return slope, mq - slope * mp


def register(output: Image.Image, source: Image.Image) -> dict:
    """Per-axis scale and offset from source window to model output, in 2x source px."""
    k = 2
    size = (source.width * k, source.height * k)
    blur = ImageFilter.GaussianBlur(1.5)
    ref = source.resize(size, Image.Resampling.BICUBIC).convert('L').filter(blur)
    cand = output.convert('RGB').resize(size, Image.Resampling.LANCZOS).convert('L').filter(blur)
    samples = []
    for cy in range(32, size[1] - 32 + 1, 32):
        for cx in range(32, size[0] - 32 + 1, 32):
            box = (cx - 20, cy - 20, cx + 20, cy + 20)
            if ImageStat.Stat(ref.crop(box)).stddev[0] < 6:
                continue
            dx, dy = _shift(ref, cand, box, 8)
            samples.append((cx, cy, dx, dy))
    if len(samples) < 4:
        return {'x': [1.0, 0.0], 'y': [1.0, 0.0], 'windows': len(samples)}
    sx, ox = _line([(cx, dx) for cx, _, dx, _ in samples])
    sy, oy = _line([(cy, dy) for _, cy, _, dy in samples])
    return {'x': [1 + sx, ox], 'y': [1 + sy, oy], 'windows': len(samples)}


def place(output: Image.Image, fit: dict, size: tuple[int, int]) -> Image.Image:
    """The model output resampled onto the window's source grid at SCALE."""
    (ax, bx), (ay, by) = fit['x'], fit['y']
    w, h = output.size
    per2x = (w / (size[0] / SCALE * 2), h / (size[1] / SCALE * 2))
    step = 2 / SCALE
    pad = 64
    padded = Image.new('RGB', (w + 2 * pad, h + 2 * pad))
    padded.paste(output.convert('RGB'), (pad, pad))
    padded.paste(padded.crop((pad, pad, w + pad, pad + 1)).resize((w, pad)), (pad, 0))
    padded.paste(padded.crop((pad, h + pad - 1, w + pad, h + pad)).resize((w, pad)), (pad, h + pad))
    padded.paste(padded.crop((pad, 0, pad + 1, h + 2 * pad)).resize((pad, h + 2 * pad)), (0, 0))
    padded.paste(padded.crop((w + pad - 1, 0, w + pad, h + 2 * pad)).resize((pad, h + 2 * pad)), (w + pad, 0))
    return padded.transform(size, Image.Transform.AFFINE,
                            (ax * step * per2x[0], 0, (bx + ax * (step / 2 - .5) + .5) * per2x[0] - .5 + pad,
                             0, ay * step * per2x[1], (by + ay * (step / 2 - .5) + .5) * per2x[1] - .5 + pad),
                            resample=Image.Resampling.BICUBIC)


def feather(size: tuple[int, int], box: tuple, full: tuple[int, int]) -> Image.Image:
    """Blend weight of a window ("F"): 1 inside, ramping to 0 across the overlap on
    edges that meet another window."""
    w, h = size
    ramp = OVERLAP * SCALE

    def axis(length: int, inner_start: bool, inner_end: bool) -> list[float]:
        return [min(1.0, (i + .5) / ramp if inner_start else 1.0, (length - i - .5) / ramp if inner_end else 1.0)
                for i in range(length)]
    xs = axis(w, box[0] > 0, box[2] < full[0])
    ys = axis(h, box[1] > 0, box[3] < full[1])
    row = Image.new('F', (w, 1)); row.putdata(xs)
    col = Image.new('F', (1, h)); col.putdata(ys)
    return ImageMath.lambda_eval(lambda a: a['r'] * a['c'], r=row.resize((w, h), Image.Resampling.NEAREST),
                                 c=col.resize((w, h), Image.Resampling.NEAREST))


def coast(source: Image.Image) -> Image.Image:
    """Source alpha at SCALE with a smooth coastline: bicubic upscale of the binary mask,
    then a narrow ramp around its midpoint (about one HD pixel)."""
    alpha = source.getchannel('A').point(lambda v: 255 if v else 0)
    big = alpha.resize((alpha.width * SCALE, alpha.height * SCALE), Image.Resampling.BICUBIC)
    big = big.filter(ImageFilter.GaussianBlur(SCALE * .35))
    return big.point(lambda v: max(0, min(255, int((v - 128) * 4 + 128))))


def land_blur(image: Image.Image, alpha: Image.Image, radius: float = COLOUR_RADIUS) -> Image.Image:
    """Gaussian blur weighted by the land mask: blur(rgb * a) / blur(a)."""
    blur = ImageFilter.GaussianBlur(radius)
    a = alpha.convert('F')
    weight = alpha.filter(blur).convert('F')
    channels = []
    for channel in image.split():
        weighted = ImageMath.lambda_eval(lambda x: x['c'] * x['a'] / 255, c=channel.convert('F'), a=a)
        # blur in 8 bits is lossy for dim coast; scale up before, back after
        spread = ImageMath.lambda_eval(lambda x: x['v'], v=weighted).convert('L').filter(blur).convert('F')
        channels.append(ImageMath.lambda_eval(lambda x: x['s'] * 255 / (x['w'] + (x['w'] < 1)), s=spread, w=weight).convert('L'))
    return Image.merge('RGB', channels)


def compose(args: argparse.Namespace) -> None:
    out = args.output
    spec = json.loads((out / 'samples.json').read_text())
    choices = json.loads((out / 'choices.json').read_text())
    (out / 'compose').mkdir(exist_ok=True)
    report = {}
    for name, surface in spec['surfaces'].items():
        source = Image.open(out / surface['source']).convert('RGBA')
        full = source.size
        W, H = full[0] * SCALE, full[1] * SCALE
        sums = [Image.new('F', (W, H)) for _ in range(3)]
        weight = Image.new('F', (W, H))
        fits = {}
        for sample in (s for s in spec['samples'] if s['surface'] == name):
            candidate = choices[sample['id']]['candidate']
            request = json.loads((out / 'runs' / f"{sample['id']}--{MODEL}--{candidate}" / 'request.json').read_text())
            box = tuple(sample['box'])
            output = Image.open(out / request['output'])
            fit = register(output, flat(source).crop(box))
            size = ((box[2] - box[0]) * SCALE, (box[3] - box[1]) * SCALE)
            piece = place(output, fit, size)
            mask = feather(size, box, full)
            area = (box[0] * SCALE, box[1] * SCALE, box[0] * SCALE + size[0], box[1] * SCALE + size[1])
            for c, channel in enumerate(piece.split()):
                sums[c].paste(ImageMath.lambda_eval(lambda a: a['s'] + a['p'] * a['m'], s=sums[c].crop(area),
                                                    p=channel.convert('F'), m=mask), area[:2])
            weight.paste(ImageMath.lambda_eval(lambda a: a['w'] + a['m'], w=weight.crop(area), m=mask), area[:2])
            fits[sample['id']] = {k: (v if k == 'windows' else [round(v[0], 5), round(v[1], 2)]) for k, v in fit.items()}
        acc = Image.merge('RGB', [ImageMath.lambda_eval(lambda a: a['s'] / (a['w'] + (a['w'] == 0)), s=sums[c], w=weight).convert('L')
                                  for c in range(3)])
        alpha = coast(source)
        if args.colour_lock:
            # Keep the source's low-frequency colour; both blurs are normalised over
            # land only, or the sea would tint the coast. Off for the painted style,
            # which changes the colours on purpose (the windows share one reference).
            smooth = land_blur(source.convert('RGB').resize((W, H), Image.Resampling.BICUBIC), alpha)
            detail = ImageChops.subtract(acc, land_blur(acc, alpha), 1, 128)
            locked = ImageChops.add(detail, smooth, 1, -128)
        else:
            locked = acc
        land = locked.copy()
        land.putalpha(alpha)
        sea = Image.new('RGBA', (W, H), OCEAN + (0,))
        result = Image.composite(land, sea, alpha.point(lambda v: 255 if v else 0))
        path = out / 'compose' / f'{name}.png'
        result.save(path)
        report[name] = {'file': str(path.relative_to(out)), 'size': [W, H], 'windows': fits}
        print(name, (W, H), {k: v['x'][0] for k, v in fits.items()})
    (out / 'compose' / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


def kit_weight(size: tuple[int, int], box: tuple, full: tuple[int, int]) -> Image.Image:
    """Blend weight of a kit window ("F"): (distance to its nearest edge that meets another
    window, in 64 source px)^4. Kit windows overlap by up to two thirds; averaging two
    paintings there smears them, so the window reaching deepest dominates and only the
    hand-over near the dividing line stays soft."""
    w, h = size
    far = float(max(full) * SCALE)

    def axis(length: int, inner_start: bool, inner_end: bool) -> list[float]:
        return [min(i + .5 if inner_start else far, length - i - .5 if inner_end else far) for i in range(length)]
    row = Image.new('F', (w, 1)); row.putdata(axis(w, box[0] > 0, box[2] < full[0]))
    col = Image.new('F', (1, h)); col.putdata(axis(h, box[1] > 0, box[3] < full[1]))
    unit = 64 * SCALE
    return ImageMath.lambda_eval(lambda a: (a['min'](a['r'], a['c']) / unit) ** 4,
                                 r=row.resize((w, h), Image.Resampling.NEAREST), c=col.resize((w, h), Image.Resampling.NEAREST))


def not_sea(image: Image.Image) -> Image.Image:
    """255 where a painted pixel does not look like sea (blue above red and green)."""
    r, g, b = image.convert('RGB').split()
    sea = ImageMath.lambda_eval(lambda a: (a['b'] > a['r'] + 30) & (a['b'] > a['g'] + 10), r=r, g=g, b=b).convert('L')
    return sea.point(lambda v: 0 if v else 255)


def near(land: Image.Image) -> Image.Image:
    """255 where painted land fills a quarter or more of the KIT_COLOUR_RADIUS neighbourhood."""
    return land.filter(ImageFilter.GaussianBlur(KIT_COLOUR_RADIUS)).point(lambda v: min(255, v * 4))


def imagegen(args: argparse.Namespace) -> None:
    """Compose the Codex image_gen kit (imagegen-kit/manifest.json) into a run folder `pack` reads.

    Per surface the whole-region picture is registered onto the full HD grid. The detail
    windows are registered and feathered, and their colour above KIT_COLOUR_RADIUS is the
    whole picture's, so neighbouring windows meet in one colour. Where no window reaches
    (mostly sea) the whole picture shows. The coastline comes from the source mask."""
    kit, out = args.kit, args.output
    manifest = json.loads((kit / 'manifest.json').read_text())
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    (out / 'compose').mkdir()
    surfaces, report = {}, {}
    rounded = lambda fit: {k: (v if k == 'windows' else [round(v[0], 5), round(v[1], 2)]) for k, v in fit.items()}
    for name, region in manifest['regions'].items():
        shutil.copyfile(kit / region['source'], out / 'inputs' / f'{name}-source.png')
        source = Image.open(kit / region['source']).convert('RGBA')
        full = source.size
        W, H = full[0] * SCALE, full[1] * SCALE
        surfaces[name] = {'resource': region['resource'], 'source': f'inputs/{name}-source.png', 'size': list(full)}
        whole = Image.open(kit / region['whole']['output'])
        fit = register(whole, flat(source))
        base = place(whole, fit, (W, H))
        fits = {Path(region['whole']['output']).stem: rounded(fit)}
        alpha = coast(source)
        # The pictures' own coasts are a little off the source's, and the source's land takes in
        # a shallow-water rim the pictures paint as sea: colours come from painted land only.
        base_land = ImageChops.multiply(alpha, not_sea(base))
        colour, colour_there = land_blur(base, base_land, KIT_COLOUR_RADIUS), near(base_land)
        # The whole picture carries a small weight everywhere, so it fills in smoothly
        # where the windows' feathered weights run out.
        sums = [ImageMath.lambda_eval(lambda a: a['c'] * KIT_BASE_WEIGHT, c=c.convert('F')) for c in base.split()]
        weight = Image.new('F', (W, H), KIT_BASE_WEIGHT)
        for window in region['windows']:
            box = tuple(window['box'])
            output = Image.open(kit / window['output'])
            fit = register(output, flat(source).crop(box))
            size = ((box[2] - box[0]) * SCALE, (box[3] - box[1]) * SCALE)
            area = (box[0] * SCALE, box[1] * SCALE, box[0] * SCALE + size[0], box[1] * SCALE + size[1])
            piece = place(output, fit, size)
            land_here = ImageChops.multiply(alpha.crop(area), not_sea(piece))
            # Own colour where the piece has painted land nearby, else the piece itself (so
            # painted sea keeps its look); the whole picture's colour wherever it has land.
            own = Image.composite(land_blur(piece, land_here, KIT_COLOUR_RADIUS), piece, near(land_here))
            target = Image.composite(colour.crop(area), own, colour_there.crop(area))
            piece = ImageChops.add(ImageChops.subtract(piece, own, 1, 128), target, 1, -128)
            mask = kit_weight(size, box, full)
            for c, channel in enumerate(piece.split()):
                sums[c].paste(ImageMath.lambda_eval(lambda a: a['s'] + a['p'] * a['m'], s=sums[c].crop(area),
                                                    p=channel.convert('F'), m=mask), area[:2])
            weight.paste(ImageMath.lambda_eval(lambda a: a['w'] + a['m'], w=weight.crop(area), m=mask), area[:2])
            fits[window['id']] = rounded(fit)
        land = Image.merge('RGB', [ImageMath.lambda_eval(lambda a: a['s'] / a['w'], s=sums[c], w=weight).convert('L') for c in range(3)])
        land.putalpha(alpha)
        sea = Image.new('RGBA', (W, H), OCEAN + (0,))
        path = out / 'compose' / f'{name}.png'
        Image.composite(land, sea, alpha.point(lambda v: 255 if v else 0)).save(path)
        report[name] = {'file': str(path.relative_to(out)), 'size': [W, H], 'windows': fits}
        print(name, (W, H), {k: v['x'][0] for k, v in fits.items()}, flush=True)
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': f'world-map surfaces drawn with Codex image_gen from {kit.name}', 'surfaces': surfaces, 'samples': []},
        ensure_ascii=False, indent=2) + '\n')
    (out / 'compose' / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


def pack(args: argparse.Namespace) -> None:
    """RT64 tiles for every surface, next to the pack's other (non-world-map) textures."""
    out = args.output
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    xxh = hasher()
    # Every run's surfaces, the first run's first.
    runs = [out] + list(args.extra_run or [])
    surfaces = [(run, name, surface, json.loads((run / 'compose' / 'report.json').read_text())[name])
                for run in runs for name, surface in json.loads((run / 'samples.json').read_text())['surfaces'].items()]
    whole = {s['resource'] for _, _, s, _ in surfaces if 'fill_only' not in s}
    # A run that redraws all of 5604 replaces the first-stage tiles and makes its fill window moot.
    approved = set() if APPROVED in whole else set(approved_tiles(table))
    surfaces = [row for row in surfaces if not ('fill_only' in row[2] and row[2]['resource'] in whole)]
    target = args.pack_output
    shutil.copytree(args.base_pack, target)
    database = json.loads((target / 'rt64.json').read_text())
    entries = {e['hashes']['rt64']: e for e in database['textures']}
    first_stage = set(approved_tiles(table)) - approved
    old = {h for h, e in entries.items() if (e['path'].startswith('map-') and h not in approved) or h in first_stage}
    for h in old:
        (target / entries[h]['path']).unlink(missing_ok=True)
        del entries[h]
    claims: dict[str, list] = {}
    for run, name, surface, composed in surfaces:
        hd = Image.open(run / composed['file']).convert('RGBA')
        for rid in [surface['resource']] + [r for r, same in SAME_TILES.items() if same == surface['resource']]:
            d = table.extract(rid)[0]
            _, tiles = assemble(d)
            fill = surface.get('fill_only')
            for t in tiles:
                pixels, palette = d[t['tex']:t['tex'] + 2048], d[t['pal']:t['pal'] + 32]
                if not any(pixels):
                    continue  # all index 0: sea only, left to the clear colour
                x, y = t['xy']
                if fill and (map_hash(pixels, palette, xxh) in approved or not (fill[0] <= x and x + 64 <= fill[2] and fill[1] <= y and y + 64 <= fill[3])):
                    continue
                tile = hd.crop((x * SCALE, y * SCALE, (x + 64) * SCALE, (y + 64) * SCALE))
                if t['flip'][0]: tile = tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                if t['flip'][1]: tile = tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                claims.setdefault(map_hash(pixels, palette, xxh), []).append((tile.tobytes(), tile, rid, x, y))
    conflicts, hashes = {}, []
    for digest, found in sorted(claims.items()):
        if len({b for b, *_ in found}) > 1 and len({(r, x, y) for _, _, r, x, y in found}) > 1:
            conflicts[digest] = [[r, x, y] for _, _, r, x, y in found]
            continue
        name = f'map-{digest}.png'
        found[0][1].save(target / name)
        entries[digest] = {'hashes': {'rt64': digest}, 'path': name}
        hashes.append(digest)
    database['textures'] = sorted(entries.values(), key=lambda e: e['hashes']['rt64'])
    (target / 'rt64.json').write_text(json.dumps(database, indent=2) + '\n')
    worldmap = {'schema': 'srw64.worldmap-hd.v1', 'resources': sorted({s['resource'] for _, _, s, _ in surfaces} | set(SAME_TILES) | ({APPROVED} if approved else set())),
                'source_tile_size': 64, 'replacement_tile_size': 64 * SCALE, 'hashes': sorted(set(hashes) | approved)}
    (target / 'pack-report.json').write_text(json.dumps({'runs': [str(run.resolve().relative_to(ROOT)) for run in runs], 'tiles': len(hashes),
                                                         'conflicts_kept_original': conflicts, 'removed_old_map_tiles': len(old)}, indent=2) + '\n')
    print({'tiles': len(hashes), 'conflicts': len(conflicts), 'removed_old': len(old)})
    if args.bind:
        path = ROOT / 'content/art/stage1-hd.json'
        manifest = json.loads(path.read_text())
        base = json.loads((target / 'rt64.json').read_text())
        paths = {e['hashes']['rt64']: e['path'] for e in base['textures']}
        # Space rows stay when the base pack already holds their tiles; on a base
        # without them, worldmap_space pack adds them again.
        rows = [r for r in manifest['textures'] if r['kind'] != 'worldmap' and (r['kind'] != 'space' or r['hash'] in paths)]
        for r in rows:
            if r['hash'] not in paths:
                raise ValueError(f"{r['hash']} missing from the new pack")
        rows += [{'hash': h, 'kind': 'worldmap', 'sha256': sha(target / paths[h])} for h in worldmap['hashes']]
        manifest['textures'] = rows
        manifest['source'] = {'path': str(target.resolve().relative_to(ROOT)), 'manifest_sha256': sha(target / 'rt64.json')}
        manifest['worldmap'] = worldmap
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', path.relative_to(ROOT), len(rows), 'textures')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'restyle', 'run', 'compose', 'imagegen', 'pack'):
        sub = commands.add_parser(name)
        sub.add_argument('--output', type=Path, required=True)
        if name == 'imagegen':
            sub.add_argument('--kit', type=Path, required=True, help='image_gen kit folder with manifest.json and outputs/')
        if name == 'restyle':
            sub.add_argument('--from', type=Path, required=True, help='composed run folder whose surface is redrawn')
            sub.add_argument('--surface', required=True)
        if name == 'run':
            sub.add_argument('--env-file', type=Path, required=True)
        if name == 'compose':
            sub.add_argument('--colour-lock', action='store_true')
        if name == 'pack':
            sub.add_argument('--base-pack', type=Path, required=True)
            sub.add_argument('--pack-output', type=Path, required=True)
            sub.add_argument('--extra-run', type=Path, action='append', help='another run folder whose surfaces join the pack')
            sub.add_argument('--bind', action='store_true')
    args = parser.parse_args()
    {'prepare': prepare, 'restyle': restyle, 'run': run, 'compose': compose, 'imagegen': imagegen, 'pack': pack}[args.command](args)


if __name__ == '__main__':
    main()
