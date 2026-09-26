"""HD redraws of the units' battle poses (the 機体 pictures the native pages show).

The battle page, the upgrade, ability and swap pages all draw the unit's basic
battle pose from `battle_assets.units` (one PNG per unique scene/atlas/palette
triplet, 330 for 363 units, mostly 96x96 or 128x128), magnified up to six times.
This tool redraws those poses one request each, like the reviewed portrait
route: grey backdrop, nearest-neighbour pre-scale, then registration and a
matte against the original alpha (portrait_matte), so the outline the pages
crop by (`rect`) stays the original's.

    .venv/bin/python -m tools.hd_ai.unit_pose_hd scale
    .venv/bin/python -m tools.hd_ai.unit_pose_hd prepare --output DIR --units 3 177
    .venv/bin/python -m tools.hd_ai.unit_pose_hd run --output DIR --env-file /path/.env
    .venv/bin/python -m tools.hd_ai.unit_pose_hd compose --output DIR

Outputs under DIR: inputs/ (frozen sources and requests), runs/ (aliyun
records), matte/ (masters at 8x with QA), hd/ (whole images named like the
battle assets: unit-<scene>-<atlas>-<palette>.png), review/ (side by side).
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import shutil
import time

from PIL import Image, ImageDraw

from srw64_rom.resources import ResourceTable
from srw64_native.battle_graphics import decode_atlas, parse_scene, read_triplets, render_scene
from tools.hd_ai.aliyun import PRICES, ROOT, load_env, run_one
from tools.hd_ai.portrait_matte import WORK, filter_fringe, flatten, matte_portrait, sha

GREY = (100, 100, 112)
INPUT_SCALE = 6          # nearest-neighbour pre-scale of the request image, as the portraits
MAX_SIDE = 2048          # qwen-image output limit; 1K and 2K cost the same
MODEL = 'qwen-image-3.0-pro'
CANDIDATE = 1
UNITS_CSV = ROOT / 'assets/original-graphics/units.csv'   # names, when export_graphics.py has run


def unit_names() -> dict[int, str]:
    if not UNITS_CSV.exists():
        return {}
    with UNITS_CSV.open(encoding='utf-8-sig') as file:
        return {int(row['id']): row['name'] for row in csv.DictReader(file)}


def poses(rom: bytes) -> list[dict]:
    """Every unique pose triplet with the units that use it, in unit order."""
    rows, by_key = [], {}
    for uid, key in enumerate(read_triplets(rom, 'unit_poses')):
        if not key[0]:
            continue
        if key not in by_key:
            by_key[key] = {'scene': key[0], 'atlas': key[1], 'palette': key[2], 'units': []}
            rows.append(by_key[key])
        by_key[key]['units'].append(uid)
    return rows


def render_pose(table: ResourceTable, row: dict) -> Image.Image:
    scene = parse_scene(table.extract(row['scene'])[0])
    atlas, _ = decode_atlas(table.extract(row['atlas'])[0], table.extract(row['palette'])[0])
    frames, _ = render_scene(scene, atlas)
    frame = next((f for f, _ in scene.steps if f != 0xFF), 0)
    return frames[frame].convert('RGBA')


def output_size(size: tuple[int, int]) -> str:
    """Largest output within MAX_SIDE keeping the source aspect, sides multiples of 32."""
    w, h = size
    k = MAX_SIDE / max(w, h)
    return f'{round(w * k / 32) * 32}*{round(h * k / 32) * 32}'


PROMPTS = {
    # Round 1 (test-1): qwen-image-3.0-pro kept the pixel staircases, only tidying lines.
    'faithful': ('忠实高清修复图1这张1999年日式机器人动画游戏的机体像素画立绘。严格保留同一台机体的造型、比例、姿势、朝向、'
                 '每一个部件、所有轮廓、位置、配色、明暗分布和原画风。仅将已有像素恢复为清楚细腻的二维手绘线条和赛璐璐上色，'
                 '让装甲边缘、关节和武器的形状清晰。不要改成写实照片或三维渲染，不要增加原图没有的装备、纹样或细节，'
                 '不要改变各部分的大小。画幅、机体占比、姿势、裁切和纯灰色背景必须与输入完全相同，不添加文字、光效或物体。'),
    # Round 2: names the pixel blocks as the defect to remove, and the target as a cel illustration.
    'smooth': ('图1是一张1999年日式机器人动画游戏里超级变形（SD、二头身）机体的低分辨率图，放大后满是马赛克方块和阶梯状锯齿。'
               '请把它重画成同一台机体的高清二维动画赛璐璐插画：所有阶梯状的边缘都画成平滑连续的手绘线条，色块之间的锯齿全部消除，'
               '装甲面用干净的赛璐璐平涂加原有的明暗分区，输出里不能再有任何像素方块。'
               '严格保留机体的造型、二头身比例、姿势、朝向、每一个部件的形状与位置、配色和明暗分布；'
               '不要改成写实照片或三维渲染，不要增加原图没有的装备、纹样、高光或细节，不要改变各部分的大小。'
               '画幅、机体占比、姿势、裁切和纯灰色背景必须与输入完全相同，不添加文字、光效或物体。'),
    # 'smooth' without the SD/二头身 naming: 真・ゲッター1 came back 400 IPInfringementSuspect under it
    # (twice), after passing under 'faithful' with the same picture.
    'smooth-plain': ('图1是一张1999年日式机器人游戏里一台Q版机器人的低分辨率图，放大后满是马赛克方块和阶梯状锯齿。'
                     '请把它重画成同一台机器人的高清二维赛璐璐插画：所有阶梯状的边缘都画成平滑连续的手绘线条，色块之间的锯齿全部消除，'
                     '装甲面用干净的赛璐璐平涂加原有的明暗分区，输出里不能再有任何像素方块。'
                     '严格保留机器人的造型、头身比例、姿势、朝向、每一个部件的形状与位置、配色和明暗分布；'
                     '不要改成写实照片或三维渲染，不要增加原图没有的装备、纹样、高光或细节，不要改变各部分的大小。'
                     '画幅、机器人占比、姿势、裁切和纯灰色背景必须与输入完全相同，不添加文字、光效或物体。'),
}
PRESCALE = {'nearest': Image.Resampling.NEAREST, 'bicubic': Image.Resampling.BICUBIC}


def prompt(style: str = 'faithful') -> str:
    return PROMPTS[style]


def scale(args: argparse.Namespace) -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    rows = poses(rom)
    sizes = Counter(render_pose(table, row).size for row in rows)
    units = sum(len(r['units']) for r in rows)
    print(f'units {units}, unique poses {len(rows)}')
    for size, count in sizes.most_common():
        print(f'  {size[0]}x{size[1]}: {count}')
    single = len(rows) * PRICES[MODEL]
    print(f'one request each, {MODEL}: {len(rows)} requests, about {single:.0f} CNY per candidate')
    print(f'same, qwen-image-3.0: about {len(rows) * PRICES["qwen-image-3.0"]:.0f} CNY per candidate')
    for columns in (2, 3):
        requests = -(-len(rows) // columns ** 2)
        print(f'{columns}x{columns} grids (untested for poses), {MODEL}: {requests} requests, about {requests * PRICES[MODEL]:.0f} CNY')


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    names = unit_names()
    rows = [r for r in poses(rom) if args.all or any(u in args.units for u in r['units'])]
    if not rows:
        raise SystemExit('no poses selected')
    samples = []
    for row in rows:
        source = render_pose(table, row)
        key = f"unit-{row['scene']}-{row['atlas']}-{row['palette']}"
        source_path = out / 'inputs' / f'{key}-source.png'
        source.save(source_path)
        request = flatten(source, GREY).resize((source.width * INPUT_SCALE, source.height * INPUT_SCALE), PRESCALE[args.prescale])
        request_path = out / 'inputs' / f'{key}-request.png'
        request.save(request_path)
        samples.append({'id': key, **{k: row[k] for k in ('scene', 'atlas', 'palette', 'units')},
                        'names': [names.get(u, '') for u in row['units']],
                        'source': str(source_path.relative_to(out)), 'source_sha256': sha(source_path),
                        'source_size': list(source.size),
                        'input': str(request_path.relative_to(out)), 'input_sha256': sha(request_path),
                        'output_size': output_size(request.size), 'backdrop': list(GREY), 'prescale': args.prescale,
                        'prompt_style': args.prompt_style, 'prompt': prompt(args.prompt_style)})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': f'unit battle poses, one request each on grey, {args.prescale} x{INPUT_SCALE}, prompt {args.prompt_style}; {MODEL}, candidate {CANDIDATE}',
        'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({'poses': len(samples), 'estimated_cny': round(len(samples) * PRICES[MODEL], 2),
           'units': [f"{u} {n}".strip() for s in samples for u, n in zip(s['units'], s['names'])]})


def run(args: argparse.Namespace) -> None:
    """Send every frozen sample once, in order; a 400 InvalidParameter (returned
    before inference) is set aside and the same request resent up to twice."""
    out = args.output
    config = load_env(args.env_file)
    samples = json.loads((out / 'samples.json').read_text())['samples']
    for sample in samples:
        folder = out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}"
        for attempt in range(3):
            report = run_one(out, sample, MODEL, CANDIDATE, config)
            print(json.dumps({k: report.get(k) for k in ('sample_id', 'status', 'http_status', 'error_code', 'dimensions', 'elapsed_seconds')}), flush=True)
            if report['status'] == 'completed' or not (report.get('http_status') == 400 and report.get('error_code') == 'InvalidParameter'):
                break
            (out / 'rejected').mkdir(exist_ok=True)
            shutil.move(folder, out / 'rejected' / f'{folder.name}--400-{int(time.time())}')
            time.sleep(5)
        if report['status'] == 'http_error' and report.get('error_code') == 'IPInfringementSuspect':
            print(f"skipped {sample['id']}: refused by the IP filter (not charged); try another prompt style", flush=True)
            continue
        if report['status'] != 'completed':
            raise SystemExit(f"stopped at {sample['id']}: {report['status']}")
        time.sleep(2)


def review_page(out: Path, sample: dict, source: Image.Image, generated: Image.Image, hd: Image.Image | None, label: str) -> None:
    """Original (nearest), raw model output and the matted HD at the battle
    page's 6x on the page's dark blue, plus a 2x crop of the centre."""
    (out / 'review').mkdir(exist_ok=True)
    zoom = 6
    w, h = source.width * zoom, source.height * zoom
    blue = (12, 20, 48, 255)
    columns = [source.resize((w, h), Image.Resampling.NEAREST), generated.convert('RGBA').resize((w, h), Image.Resampling.LANCZOS)]
    if hd is not None:
        columns.append(hd.resize((w, h), Image.Resampling.LANCZOS))
    gap = 12
    page = Image.new('RGB', (len(columns) * (w + gap) + gap, h + 2 * gap + 24 + h), (236, 236, 236))
    for i, picture in enumerate(columns):
        tile = Image.new('RGBA', (w, h), blue)
        tile.alpha_composite(picture)
        page.paste(tile.convert('RGB'), (gap + i * (w + gap), gap))
        # Centre crop at 2x for the line work.
        crop = tile.crop((w // 4, h // 4, w * 3 // 4, h * 3 // 4)).resize((w, h), Image.Resampling.NEAREST if i == 0 else Image.Resampling.LANCZOS)
        page.paste(crop.convert('RGB'), (gap + i * (w + gap), h + 2 * gap + 24))
    ImageDraw.Draw(page).text((gap, h + gap + 6), label, fill=(20, 20, 20))
    page.save(out / 'review' / f"{sample['id']}.png")


def compose(args: argparse.Namespace) -> None:
    out = args.output
    samples = json.loads((out / 'samples.json').read_text())['samples']
    (out / 'matte').mkdir(exist_ok=True)
    (out / 'hd').mkdir(exist_ok=True)
    results = {}
    for sample in samples:
        run_dir = out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}"
        if not (run_dir / 'request.json').exists():
            results[sample['id']] = {'status': 'not_run'}
            continue
        request = json.loads((run_dir / 'request.json').read_text())
        if request['status'] != 'completed':
            results[sample['id']] = {'status': request['status']}
            continue
        generated = Image.open(out / request['output'])
        assert sha(out / request['output']) == request['output_sha256']
        source = Image.open(out / sample['source']).convert('RGBA')
        label = f"{sample['id']}  {' / '.join(n or str(u) for u, n in zip(sample['units'], sample['names']))}"
        try:
            master, report = matte_portrait(generated, source, tuple(sample['backdrop']))
        except RuntimeError as error:
            results[sample['id']] = {'status': 'matte_failed', 'error': str(error)}
            review_page(out, sample, source, generated, None, label + '  MATTE FAILED')
            print(sample['id'], 'matte failed:', error)
            continue
        master_path = out / 'matte' / f"{sample['id']}-master.png"
        master.save(master_path)
        hd_path = out / 'hd' / f"{sample['id']}.png"
        master.save(hd_path)   # whole image at WORK x, premultiplied at draw time by the pages
        fringe = filter_fringe(master, (12, 20, 48))
        results[sample['id']] = {'status': 'completed', 'master': str(master_path.relative_to(out)),
                                 'hd': str(hd_path.relative_to(out)), 'hd_size': list(master.size), 'hd_sha256': sha(hd_path),
                                 'matte': report, 'fringe': fringe}
        fit = report['registration']
        review_page(out, sample, source, generated, master,
                    label + f"  fit x{fit['x']} y{fit['y']}  iou {report['silhouette_iou_against_original']:.3f}")
        print(sample['id'], master.size, {k: report[k] for k in ('registration', 'silhouette_iou_against_original',
                                                                'foreground_area_ratio_against_original', 'warnings')})
    (out / 'report.json').write_text(json.dumps({'schema': 'srw64.unit-pose-hd-report.v1', 'model': MODEL,
        'work_scale': WORK, 'results': results}, ensure_ascii=False, indent=2) + '\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('scale', help='count the poses and estimate the cost').set_defaults(func=scale)
    p = commands.add_parser('prepare', help='freeze sources, requests and prompts')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--units', type=int, nargs='*', default=[], help='unit ids whose poses to prepare')
    p.add_argument('--all', action='store_true')
    p.add_argument('--prompt-style', choices=PROMPTS, default='smooth')
    p.add_argument('--prescale', choices=PRESCALE, default='nearest')
    p.set_defaults(func=prepare)
    p = commands.add_parser('run', help='send the frozen requests once each')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--env-file', type=Path, required=True)
    p.set_defaults(func=run)
    p = commands.add_parser('compose', help='register, matte and build review pages')
    p.add_argument('--output', type=Path, required=True)
    p.set_defaults(func=compose)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
