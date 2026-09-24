"""Grid-batched portrait redraws: several portraits per qwen-image request.

qwen-image-3.0 bills per output image and 1K and 2K cost the same, so an N x N
grid of portraits in one 2048 px request costs 1/N^2 per portrait. Each cell
keeps a backdrop gutter so frame-cut clothes do not run into a neighbour.
`prepare` freezes every portrait's grid from the ROM, `run` sends them in order
through run_benchmark.run_one, and `compose` fits each cell's own shift and
scale, resamples it from the whole output and mattes it with portrait_matte.

2026-09-24: on the grey backdrop cells move up to 7 source px and scale by up
to 6%, each on its own (drawing out into the gutter). On pure green the model
zoomed every portrait and filled the gutters, so key-colour backdrops are not
used.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

from srw64_rom.resources import ResourceTable
from tools.hd_ai.portrait_matte import RUNTIME, WORK, filter_fringe, flatten, matte_portrait, runtime_image
from tools.hd_ai.run_benchmark import ROOT, run_one

GUTTER = 16        # source px of backdrop around every cell
INPUT_SCALE = 6    # nearest-neighbour pre-scale of the request image
OUTPUT_SIZE = '2048*2048'
MODEL = 'qwen-image-3.0'
CANDIDATE = 2      # seed 640903, as the reviewed single-portrait outputs
GREY = (100, 100, 112)
FACE_TABLE = 0x84220  # actor -> (image, palette) resource ids, big-endian u16 pairs
ACTORS = 361
SILHOUETTE = 609      # one dark grey: derived from the HD matte, never generated
PROTAGONISTS = [(1308 + k, 1312 + k) for k in range(4)]  # save_page.cpp D_801DC680
PAGE = 20             # portraits per review page


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def key_color(sources: list[Image.Image]) -> tuple[int, int, int]:
    """The 17-step RGB colour farthest from every colour the portraits use."""
    used = {p[:3] for image in sources for p in image.convert('RGBA').get_flattened_data() if p[3]}
    candidates = [(r, g, b) for r in range(0, 256, 17) for g in range(0, 256, 17) for b in range(0, 256, 17)]
    return max(candidates, key=lambda c: min(sum((c[k] - u[k]) ** 2 for k in range(3)) for u in used))


def layout(sizes: list[tuple[int, int]], columns: int) -> tuple[tuple[int, int], list[tuple[int, int]]]:
    cell = max(max(size) for size in sizes)
    rows = -(-len(sizes) // columns)
    canvas = (columns * cell + (columns + 1) * GUTTER, rows * cell + (rows + 1) * GUTTER)
    origins = [(GUTTER + (i % columns) * (cell + GUTTER), GUTTER + (i // columns) * (cell + GUTTER))
               for i in range(len(sizes))]
    return canvas, origins


def grid_source(sources: list[Image.Image], columns: int) -> tuple[Image.Image, list[tuple[int, int]]]:
    canvas, origins = layout([s.size for s in sources], columns)
    grid = Image.new('RGBA', canvas, (0, 0, 0, 0))
    for source, origin in zip(sources, origins):
        grid.paste(source.convert('RGBA'), origin)
    return grid, origins


def prompt(count: int, columns: int, backdrop_name: str, note: str = '') -> str:
    if count == 1:
        return ('忠实高清修复图1这张1999年日式二维赛璐璐游戏人物头像。严格保留同一个人物的脸型、五官、年龄、发型、眼神、'
                '原有表情、朝向、服装、所有轮廓、位置、配色和原画风。仅将已有像素线条恢复为清楚细腻的二维手绘线条。'
                '不要美化变年轻，不要增加服饰细节，不要改成写实照片或三维。'
                f'画幅、人物占比、姿势、裁切和{backdrop_name}背景必须与输入相同，不添加文字或物体。' + note)
    rows = -(-count // columns)
    return (f'图1是按{rows}行{columns}列网格排列的{count}张互不相关的1999年日式二维赛璐璐游戏人物头像，'
            f'格子之间是{backdrop_name}背景。逐格忠实高清修复：每一格严格保留该人物的脸型、五官、年龄、发型、眼神、'
            '原有表情、朝向、服装、所有轮廓、位置、配色和原画风，仅将已有像素线条恢复为清楚细腻的二维手绘线条。'
            '不要美化变年轻，不要增加服饰细节，不要改成写实照片或三维。各格人物互不影响，不要合并，不要越过格子。'
            f'每格的位置、大小、人物占比、裁切以及{backdrop_name}背景都必须与输入完全相同，不添加文字、边框或物体。' + note)


def make_grid(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    approved = json.loads((args.portraits / 'samples.json').read_text())['samples']
    cells, sources = [], []
    for sample in approved:
        source = Image.open(args.portraits / sample['source']).convert('RGBA')
        path = out / 'inputs' / f"portrait-{sample['resource_id']}-source.png"
        source.save(path)
        sources.append(source)
        cells.append({'resource_id': sample['resource_id'], 'character': sample['character'],
                      'source': str(path.relative_to(out)), 'source_sha256': sha(path)})
    grid, origins = grid_source(sources, args.columns)
    for cell, origin in zip(cells, origins):
        cell['origin'] = list(origin)
    samples = []
    for name, color, color_name in (('gray', (100, 100, 112), '纯灰色'), ('key', key_color(sources), '纯绿色')):
        request = flatten(grid, color).resize((grid.width * INPUT_SCALE, grid.height * INPUT_SCALE), Image.Resampling.NEAREST)
        path = out / 'inputs' / f'grid-{name}.png'
        request.save(path)
        samples.append({'id': f'grid-{args.columns}x{args.columns}-{name}', 'input': str(path.relative_to(out)),
                        'input_sha256': sha(path), 'output_size': OUTPUT_SIZE, 'backdrop': list(color),
                        'grid_size': list(grid.size), 'columns': args.columns, 'cells': cells,
                        'prompt': prompt(len(cells), args.columns, color_name)})
    if samples[1]['backdrop'] != [0, 255, 0]:
        raise ValueError(f"key colour {samples[1]['backdrop']} is not the green the prompt names")
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': 'grid batching test on the four reviewed portraits; qwen-image-3.0, seed of candidate 2',
        'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({s['id']: s['backdrop'] for s in samples})


def decode(table: ResourceTable, image_id: int, palette_id: int) -> Image.Image:
    image, palette = table.extract(image_id)[0], table.extract(palette_id)[0]
    kind, width, height, zero = struct.unpack_from('>4H', image)
    if kind not in (6, 15) or width != height or width not in (96, 97) or zero or len(image) != 8 + width * height:
        raise ValueError(f'resource {image_id} is not a portrait')
    colors = [tuple(round(((v >> shift) & 31) * 255 / 31) for shift in (11, 6, 1)) + (255 * (v & 1),)
              for (v,) in struct.iter_unpack('>H', palette[8:])]
    return Image.frombytes('RGBA', (width, height), bytes(c for index in image[8:] for c in colors[index]))


def portraits(rom: bytes) -> list[dict]:
    """Every distinct portrait image with the palette it is redrawn under.

    The base palette is image + 300 where the face table uses it (294 of 300);
    images 259-264 each have one crossed palette. Other palettes of an image
    (the silhouette 609 on 34 images, four borrowed palettes on generic face
    134) are variants to derive from the HD result, not to generate.
    """
    palettes, actors = defaultdict(Counter), defaultdict(list)
    for actor in range(ACTORS):
        image, palette = struct.unpack_from('>2H', rom, FACE_TABLE + 4 * actor)
        palettes[image][palette] += 1
        actors[image].append(actor)
    rows = []
    for image in sorted(palettes):
        usable = [p for p in palettes[image] if p != SILHOUETTE]
        base = image + 300 if image + 300 in usable else min(usable)
        rows.append({'resource_id': image, 'palette_id': base, 'actors': actors[image],
                     'variants': sorted(p for p in palettes[image] if p != base)})
    rows += [{'resource_id': image, 'palette_id': palette, 'actors': [], 'variants': [], 'protagonist': True}
             for image, palette in PROTAGONISTS]
    return rows


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    rows = [r for r in portraits(rom) if r['resource_id'] not in args.skip and (not args.ids or r['resource_id'] in args.ids)]
    for row in rows:
        source = decode(table, row['resource_id'], row['palette_id'])
        path = out / 'inputs' / f"portrait-{row['resource_id']}-source.png"
        source.save(path)
        row.update(size=source.width, source=str(path.relative_to(out)), source_sha256=sha(path))
    # 97 px portraits together so no grid mixes cell sizes; id order keeps series together.
    rows.sort(key=lambda r: (r['size'], r['resource_id']))
    samples = []
    for start in range(0, len(rows), args.columns ** 2):
        cells = rows[start:start + args.columns ** 2]
        sources = [Image.open(out / c['source']).convert('RGBA') for c in cells]
        grid, origins = grid_source(sources, args.columns)
        for cell, origin in zip(cells, origins):
            cell['origin'] = list(origin)
        name = f'grid-{len(samples) + 1:03d}'
        path = out / 'inputs' / f'{name}.png'
        flatten(grid, GREY).resize((grid.width * INPUT_SCALE, grid.height * INPUT_SCALE), Image.Resampling.NEAREST).save(path)
        samples.append({'id': name, 'input': str(path.relative_to(out)), 'input_sha256': sha(path),
                        'output_size': OUTPUT_SIZE, 'backdrop': list(GREY), 'grid_size': list(grid.size),
                        'columns': args.columns, 'cells': cells, 'prompt': prompt(len(cells), args.columns, '纯灰色', args.note)})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': f'all portraits, {args.columns}x{args.columns} grids on grey; {MODEL}, seed of candidate {CANDIDATE}',
        'skipped_reviewed': sorted(args.skip), 'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({'portraits': len(rows), 'requests': len(samples), 'estimated_cny': round(len(samples) * 0.2, 2)})


def load_env(path: Path) -> dict:
    config = {}
    for line in path.read_text().splitlines():
        line = line.strip().removeprefix('export ')
        if line and not line.startswith('#') and '=' in line:
            key, value = line.split('=', 1)
            if key.strip() in ('DASHSCOPE_API_KEY', 'DASHSCOPE_BASE_URL'):
                config[key.strip()] = value.strip().strip('"\'')
    return config


def run(args: argparse.Namespace) -> None:
    """Send every frozen grid once, in order. A 400 InvalidParameter is returned
    before inference (seen twice on the test, then accepted unchanged), so its
    record is set aside and the same request resent up to twice; any other
    failure stops the run for inspection."""
    out = args.output
    config = load_env(args.env_file)
    samples = json.loads((out / 'samples.json').read_text())['samples']
    for sample in samples:
        folder = out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}"
        for attempt in range(3):
            report = run_one(out, sample, MODEL, CANDIDATE, config)
            print(json.dumps({k: report.get(k) for k in ('sample_id', 'status', 'http_status', 'error_code', 'elapsed_seconds')}), flush=True)
            if report['status'] == 'completed' or not (report.get('http_status') == 400 and report.get('error_code') == 'InvalidParameter'):
                break
            (out / 'rejected').mkdir(exist_ok=True)
            shutil.move(folder, out / 'rejected' / f'{folder.name}--400-{int(time.time())}')
            time.sleep(5)
        if report['status'] != 'completed':
            raise SystemExit(f"stopped at {sample['id']}: {report['status']}")
        time.sleep(4)


def review_pages(out: Path, samples: list, results: dict) -> None:
    """Original beside redraw on the dialogue-window blue, PAGE portraits a page."""
    rows = [(cell, row) for s in samples if results[s['id']]['status'] == 'completed'
            for cell, row in zip(s['cells'], results[s['id']]['cells'])]
    (out / 'review').mkdir(exist_ok=True)
    size, gap, per_row = 224, 10, 4
    blue = (28, 36, 84, 255)
    for page, start in enumerate(range(0, len(rows), PAGE), 1):
        chunk = rows[start:start + PAGE]
        lines = -(-len(chunk) // per_row)
        image = Image.new('RGB', (per_row * (2 * size + 3 * gap), lines * (size + 30 + gap) + gap), (236, 236, 236))
        draw = ImageDraw.Draw(image)
        for i, (cell, row) in enumerate(chunk):
            x, y = (i % per_row) * (2 * size + 3 * gap) + gap, (i // per_row) * (size + 30 + gap) + gap
            original = Image.open(out / cell['source']).convert('RGBA').resize((size, size), Image.Resampling.NEAREST)
            pictures = [original]
            if row['status'] == 'completed':
                pictures.append(Image.open(out / row['runtime']).convert('RGBA').resize((size, size), Image.Resampling.LANCZOS))
            for k, picture in enumerate(pictures):
                tile = Image.new('RGBA', (size, size), blue)
                tile.alpha_composite(picture)
                image.paste(tile.convert('RGB'), (x + k * (size + gap), y))
            warnings = row.get('matte', {}).get('warnings', [])
            label = f"{cell['resource_id']}  {row.get('status')}  err {row.get('fidelity', '-')}" + (f"  ! {', '.join(warnings)}" if warnings else '')
            draw.text((x, y + size + 6), label, fill=(20, 20, 20))
        image.save(out / 'review' / f'page-{page:02d}.png')


def _error(ref: Image.Image, cand: Image.Image, box: tuple, dx: int, dy: int) -> float:
    moved = cand.crop((box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy))
    return ImageStat.Stat(ImageChops.difference(ref.crop(box), moved)).mean[0]


def _plane(points: list[tuple[float, float, float]]) -> tuple[float, float, float]:
    """Least-squares a, b, c for q = a*x + b*y + c (normal equations, Cramer's rule)."""
    n = len(points)
    sx = sum(x for x, _, _ in points); sy = sum(y for _, y, _ in points); sq = sum(q for _, _, q in points)
    sxx = sum(x * x for x, _, _ in points); syy = sum(y * y for _, y, _ in points); sxy = sum(x * y for x, y, _ in points)
    sxq = sum(x * q for x, _, q in points); syq = sum(y * q for _, y, q in points)
    m = ((sxx, sxy, sx), (sxy, syy, sy), (sx, sy, n))
    r = (sxq, syq, sq)
    def det(a):
        return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    d = det(m)
    if not d:
        return 0.0, 0.0, sq / n
    return tuple(det(tuple(tuple(r[i] if j == k else m[i][j] for j in range(3)) for i in range(3))) / d for k in range(3))


def cell_fit(source: Image.Image, backdrop: tuple, cand: Image.Image, origin: tuple[int, int]) -> dict:
    """Fit one cell of a grid output: output x = origin + ax*x + bx (runtime px).

    `cand` is the whole output, grey-level and blurred, at RUNTIME px per
    source px. Works at half resolution: a whole-cell shift first, then 32 px
    windows around it for per-axis scale and offset.
    """
    ref = flatten(source, backdrop).resize((source.width * RUNTIME, source.height * RUNTIME), Image.Resampling.BICUBIC)
    ref = ref.convert('L').filter(ImageFilter.GaussianBlur(2))
    small = cand.resize((cand.width // 2, cand.height // 2), Image.Resampling.BOX)
    canvas = Image.new('L', small.size, flatten(Image.new('RGBA', (1, 1)), backdrop).convert('L').getpixel((0, 0)))
    ox, oy = origin[0] // 2, origin[1] // 2
    canvas.paste(ref.resize((ref.width // 2, ref.height // 2), Image.Resampling.BOX), (ox, oy))
    w, h = ref.width // 2, ref.height // 2
    cell = (ox, oy, ox + w, oy + h)
    reach = 24  # half-res px = 12 source px, three quarters of a gutter
    shifts = {(dx, dy): _error(canvas, small, cell, dx, dy) for dy in range(-reach, reach + 1, 2) for dx in range(-reach, reach + 1, 2)}
    sx0, sy0 = min(shifts, key=shifts.get)
    samples = []
    for cy in range(24, h - 24 + 1, 24):
        for cx in range(24, w - 24 + 1, 24):
            box = (ox + cx - 16, oy + cy - 16, ox + cx + 16, oy + cy + 16)
            if ImageStat.Stat(canvas.crop(box)).stddev[0] < 8:
                continue
            errors = {(dx, dy): _error(canvas, small, box, dx, dy)
                      for dy in range(sy0 - 10, sy0 + 11) for dx in range(sx0 - 10, sx0 + 11)}
            dx, dy = min(errors, key=errors.get)
            samples.append((cx, cy, dx, dy))
    if len(samples) < 6:
        raise RuntimeError('cell has too little detail to register')
    # Full affine: the model also shears and rotates a cell (grey 14: top
    # 5 source px left of the bottom), so d = a*x + b*y + c per axis.
    fx = _plane([(cx, cy, dx) for cx, cy, dx, _ in samples])
    fy = _plane([(cx, cy, dy) for cx, cy, _, dy in samples])
    residual = sum(abs(dx - fx[0] * cx - fx[1] * cy - fx[2]) + abs(dy - fy[0] * cx - fy[1] * cy - fy[2])
                   for cx, cy, dx, dy in samples) / len(samples)
    # half-res d(c) = a*cx + b*cy + c  ->  runtime: x' = (1 + a)*x + b*y + 2c
    fit = {'x': [round(1 + fx[0], 5), round(fx[1], 5), round(2 * fx[2], 2)],
           'y': [round(fy[0], 5), round(1 + fy[1], 5), round(2 * fy[2], 2)],
           'windows': len(samples), 'mean_residual_half_px': round(residual, 2), 'units': 'runtime px'}
    # Small figures (the protagonists 1308-1311) come back up to 10% larger.
    # Redrawn hair spikes (1310) leave up to ~1.5 source px of local residual.
    if max(abs(fx[0]), abs(fx[1]), abs(fy[0]), abs(fy[1])) > 0.15 or residual > 3:
        raise RuntimeError(f'cell registration needs inspection: {fit}')
    return fit


def compose(args: argparse.Namespace) -> None:
    out = args.output
    samples = json.loads((out / 'samples.json').read_text())['samples']
    (out / 'matte').mkdir(exist_ok=True)
    results = {}
    previous = json.loads((out / 'report.json').read_text())['results'] if args.only and (out / 'report.json').exists() else {}
    for sample in samples:
        if args.only and sample['id'] not in args.only and sample['id'] in previous:
            results[sample['id']] = previous[sample['id']]
            continue
        run = out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}"
        if not (run / 'request.json').exists():
            results[sample['id']] = {'status': 'not_run'}
            continue
        request = json.loads((run / 'request.json').read_text())
        if request['status'] != 'completed':
            results[sample['id']] = {'status': request['status']}
            continue
        generated = Image.open(out / request['output'])
        assert sha(out / request['output']) == request['output_sha256']
        sources = [Image.open(out / c['source']).convert('RGBA') for c in sample['cells']]
        grid, origins = grid_source(sources, sample['columns'])
        backdrop = tuple(sample['backdrop'])
        # Every cell moves and scales on its own (grey: up to 7 source px and
        # 6%, drawn out into the gutter), so each is fitted separately and
        # resampled from the whole output; portrait_matte then sees ~identity.
        cand = generated.convert('RGB').resize((grid.width * RUNTIME, grid.height * RUNTIME), Image.Resampling.LANCZOS)
        cand = cand.convert('L').filter(ImageFilter.GaussianBlur(2))
        cells = []
        for cell, source, (x, y) in zip(sample['cells'], sources, origins):
            try:
                fit = cell_fit(source, backdrop, cand, (x * RUNTIME, y * RUNTIME))
                (a, b, c), (d, e, f) = fit['x'], fit['y']
                k = generated.width / cand.width
                step = k * RUNTIME / WORK  # master px -> output px
                crop = generated.convert('RGB').transform(
                    (source.width * WORK, source.height * WORK), Image.Transform.AFFINE,
                    (a * step, b * step, (x * RUNTIME + c) * k, d * step, e * step, (y * RUNTIME + f) * k),
                    resample=Image.Resampling.BICUBIC)
                master, report = matte_portrait(crop, source, backdrop=backdrop)
            except RuntimeError as error:
                cells.append({'resource_id': cell['resource_id'], 'status': 'needs_inspection', 'error': str(error)})
                continue
            report['cell_registration'] = fit
            high = runtime_image(master)
            path = out / 'matte' / f"{sample['id']}-{cell['resource_id']}-{high.width}.png"
            high.save(path)
            master_path = out / 'matte' / f"{sample['id']}-{cell['resource_id']}-master.png"
            master.save(master_path)
            report['filter_fringe'] = filter_fringe(high, (255, 255, 255))
            cells.append({'resource_id': cell['resource_id'], 'status': 'completed', 'runtime': str(path.relative_to(out)),
                          'runtime_sha256': sha(path), 'master': str(master_path.relative_to(out)),
                          'master_sha256': sha(master_path), 'fidelity': fidelity(high, source), 'matte': report})
        results[sample['id']] = {'status': 'completed', 'cells': cells}
    reference = {}
    if args.reference:
        for row in json.loads((args.reference / 'report.json').read_text())['portraits']:
            high = Image.open(args.reference / row['runtime']).convert('RGBA')
            source = Image.open(out / 'inputs' / f"portrait-{row['resource_id']}-source.png").convert('RGBA')
            reference[row['resource_id']] = {'runtime': str(args.reference / row['runtime']),
                                             'fidelity': fidelity(high, source)}
    (out / 'report.json').write_text(json.dumps({'schema': 'srw64.portrait-grid-test.v1', 'results': results,
                                                 'single_reference': reference}, ensure_ascii=False, indent=2) + '\n')
    if reference:
        sheet(out, samples, results, reference)
    else:
        review_pages(out, samples, results)
    for sid, result in results.items():
        print(sid, result['status'], [(c['resource_id'], c['fidelity'], round(c['matte']['silhouette_iou_against_original'], 4))
                                      if c['status'] == 'completed' else (c['resource_id'], c['status'])
                                      for c in result.get('cells', [])])
    if reference:
        print('single', {k: v['fidelity'] for k, v in reference.items()})


def fidelity(high: Image.Image, source: Image.Image) -> float:
    """Mean luma error of the redraw box-reduced to the source size, over opaque source pixels."""
    small = Image.new('RGBA', source.size, (100, 100, 112, 255))
    small.alpha_composite(high.resize(source.size, Image.Resampling.BOX))
    original = flatten(source)
    a = small.convert('L').get_flattened_data()
    b = original.convert('L').get_flattened_data()
    mask = [p[3] > 0 for p in source.get_flattened_data()]
    diffs = [abs(x - y) for x, y, m in zip(a, b, mask) if m]
    return round(sum(diffs) / len(diffs), 2)


def sheet(out: Path, samples: list, results: dict, reference: dict) -> None:
    columns = [('原图', None)] + ([('单张（已审）', 'single')] if reference else []) + \
              [(s['id'], s['id']) for s in samples if results[s['id']]['status'] == 'completed']
    cell, gap, head = 320, 8, 30
    ids = [c['resource_id'] for c in samples[0]['cells']]
    image = Image.new('RGB', (len(columns) * (cell + gap) + gap, head + len(ids) * (cell + gap) + gap), (236, 236, 236))
    draw = ImageDraw.Draw(image)
    blue = (28, 36, 84, 255)
    for i, (label, _) in enumerate(columns):
        draw.text((gap + i * (cell + gap) + 4, 10), label, fill=(20, 20, 20))
    for j, rid in enumerate(ids):
        for i, (_, key) in enumerate(columns):
            if key is None:
                picture = Image.open(out / 'inputs' / f'portrait-{rid}-source.png').convert('RGBA').resize((cell, cell), Image.Resampling.NEAREST)
            elif key == 'single':
                picture = Image.open(reference[rid]['runtime']).convert('RGBA').resize((cell, cell), Image.Resampling.LANCZOS)
            else:
                row = next(c for c in results[key]['cells'] if c['resource_id'] == rid)
                if row['status'] != 'completed':
                    continue
                picture = Image.open(out / row['runtime']).convert('RGBA').resize((cell, cell), Image.Resampling.LANCZOS)
            tile = Image.new('RGBA', (cell, cell), blue)
            tile.alpha_composite(picture)
            image.paste(tile.convert('RGB'), (gap + i * (cell + gap), head + j * (cell + gap)))
    image.save(out / 'comparison.png')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    grid = commands.add_parser('grid', help='freeze a grid test of the reviewed portraits')
    grid.add_argument('--portraits', type=Path, required=True, help='folder with the reviewed samples.json')
    grid.add_argument('--output', type=Path, required=True)
    grid.add_argument('--columns', type=int, default=2)
    done = commands.add_parser('compose', help='cut and matte completed grid outputs')
    done.add_argument('--output', type=Path, required=True)
    done.add_argument('--reference', type=Path, help='portrait_matte output of the reviewed single redraws')
    done.add_argument('--only', nargs='*', default=[], help='grid ids to redo; the rest are kept from report.json')
    batch = commands.add_parser('prepare', help='freeze grids of every portrait from the ROM')
    batch.add_argument('--output', type=Path, required=True)
    batch.add_argument('--columns', type=int, default=2)
    batch.add_argument('--skip', type=int, nargs='*', default=[], help='image ids already redrawn and reviewed')
    batch.add_argument('--ids', type=int, nargs='*', default=[], help='only these image ids (redraws)')
    batch.add_argument('--note', default='', help='text appended to the prompt, e.g. what a redraw must keep')
    send = commands.add_parser('run', help='send the frozen grids in order')
    send.add_argument('--output', type=Path, required=True)
    send.add_argument('--env-file', type=Path, required=True)
    args = parser.parse_args()
    {'grid': make_grid, 'prepare': prepare, 'run': run, 'compose': compose}[args.command](args)


if __name__ == '__main__':
    main()
