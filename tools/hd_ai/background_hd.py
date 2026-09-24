"""HD intermission backgrounds: eight 320x240 CI8 CG pictures, bright and dark palettes.

`prepare` freezes one request per picture and model (the bright palette, nearest
upscaled), `run` sends them through aliyun.run_one, `compose` registers each
output to its source, resamples it to SIZE and renders a comparison sheet, and
`build` derives the dark version from the two ROM palettes and writes the
whole-image set the host draws (native_background.cpp).

The dark palette is not a uniform dimming (per-colour ratios 0.5-0.95), so the dark
image is mapped from the bright HD one through a colour table fitted to the 256
palette pairs. Index 0 is transparent black; the game clears to black behind the
picture, so the HD images are opaque.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import time

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

from srw64_rom.resources import ResourceTable
from tools.hd_ai.aliyun import PRICES, ROOT, load_env, run_one

IMAGES = range(0x155E, 0x1566)            # 5470-5477; D_800C59AC lists them per lead unit
BRIGHT, DARK = 0x1566 - 0x155E, 0x156E - 0x155E  # palette = image + offset
SOURCE = (320, 240)
SIZE = (1920, 1440)                       # 6x: the 1440p window 1:1, mipmapped below
INPUT_SCALE = 6
OUTPUT_SIZE = '2048*1536'
MODELS = ('qwen-image-3.0-pro', 'qwen-image-3.0')
CANDIDATE = 1
LUT = 17                                  # colour table resolution per channel
PROMPT = ('忠实高清修复图1这张1999年游戏的三维CG插画。严格保留构图、机体的造型与每个部件的形状和位置、配色、光影、'
          '背景内容和画面裁切，只把低分辨率像素恢复为清晰细腻的高清CG渲染，去除抖动噪点和色带。'
          '右上角的标志文字必须原样保留：大字“SRW64”和下面一行小字“super robot wars 64”，'
          '字母、字体、倾斜、位置和大小都不变。不要增加新的物体、文字或细节，不要改变画幅。')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def colors(table: ResourceTable, palette: int) -> list[tuple[int, int, int, int]]:
    raw = table.extract(palette)[0][8:8 + 512]
    return [tuple(round(((v >> s) & 31) * 255 / 31) for s in (11, 6, 1)) + (255 * (v & 1),)
            for (v,) in struct.iter_unpack('>H', raw)]


def decode(table: ResourceTable, image: int, palette: int) -> Image.Image:
    data = table.extract(image)[0]
    kind, width, height, _ = struct.unpack_from('>4H', data)
    if (kind, width, height) != (8, *SOURCE):
        raise ValueError(f'resource {image} is not a 320x240 CI8 background')
    pal = colors(table, palette)
    rgb = bytes(c for index in data[8:8 + width * height] for c in pal[index][:3])
    return Image.frombytes('RGB', SOURCE, rgb)  # index 0 is transparent black: opaque black


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    samples = []
    for image in IMAGES:
        source = decode(table, image, image + BRIGHT)
        path = out / 'inputs' / f'background-{image}-source.png'
        source.save(path)
        request = out / 'inputs' / f'background-{image}.png'
        source.resize((SOURCE[0] * INPUT_SCALE, SOURCE[1] * INPUT_SCALE), Image.Resampling.NEAREST).save(request)
        samples.append({'id': f'background-{image}', 'image': image, 'bright': image + BRIGHT, 'dark': image + DARK,
                        'source': str(path.relative_to(out)), 'input': str(request.relative_to(out)),
                        'input_sha256': sha(request), 'output_size': OUTPUT_SIZE, 'prompt': PROMPT})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': 'intermission backgrounds, bright palette; one candidate per model', 'models': list(MODELS),
        'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({'backgrounds': len(samples), 'estimated_cny': round(len(samples) * sum(PRICES[m] for m in MODELS), 2)})


def run(args: argparse.Namespace) -> None:
    out = args.output
    config = load_env(args.env_file)
    samples = json.loads((out / 'samples.json').read_text())['samples']
    for model in MODELS:
        for sample in samples:
            folder = out / 'runs' / f"{sample['id']}--{model}--{CANDIDATE}"
            for _ in range(3):
                report = run_one(out, sample, model, CANDIDATE, config)
                print(json.dumps({k: report.get(k) for k in ('sample_id', 'model', 'status', 'http_status', 'error_code', 'elapsed_seconds')}), flush=True)
                if report['status'] == 'completed' or not (report.get('http_status') == 400 and report.get('error_code') == 'InvalidParameter'):
                    break
                (out / 'rejected').mkdir(exist_ok=True)
                shutil.move(folder, out / 'rejected' / f'{folder.name}--400-{int(time.time())}')
                time.sleep(5)
            if report['status'] != 'completed':
                raise SystemExit(f"stopped at {sample['id']} / {model}: {report['status']}")
            time.sleep(4 if model == 'qwen-image-3.0' else 13)


def _best_shift(ref: Image.Image, cand: Image.Image, box: tuple, radius: int) -> tuple[int, int]:
    base = ref.crop(box)
    best = None
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            error = ImageStat.Stat(ImageChops.difference(base, cand.crop((box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy)))).mean[0]
            if best is None or error < best[0]:
                best = (error, dx, dy)
    return best[1], best[2]


def _line(points: list[tuple[float, float]]) -> tuple[float, float]:
    n = len(points)
    mp = sum(p for p, _ in points) / n
    mq = sum(q for _, q in points) / n
    spread = sum((p - mp) ** 2 for p, _ in points)
    slope = sum((p - mp) * (q - mq) for p, q in points) / spread if spread else 0.0
    return slope, mq - slope * mp


def register(generated: Image.Image, source: Image.Image) -> dict:
    """Per-axis scale and offset from source to output, in 2x source pixels."""
    k = 2
    size = (source.width * k, source.height * k)
    blur = ImageFilter.GaussianBlur(1.5)
    ref = source.resize(size, Image.Resampling.BICUBIC).convert('L').filter(blur)
    cand = generated.convert('RGB').resize(size, Image.Resampling.LANCZOS).convert('L').filter(blur)
    samples = []
    for cy in range(40, size[1] - 40 + 1, 40):
        for cx in range(40, size[0] - 40 + 1, 40):
            box = (cx - 24, cy - 24, cx + 24, cy + 24)
            if ImageStat.Stat(ref.crop(box)).stddev[0] < 10:
                continue
            dx, dy = _best_shift(ref, cand, box, 8)
            samples.append((cx, cy, dx, dy))
    sx, ox = _line([(cx, dx) for cx, _, dx, _ in samples])
    sy, oy = _line([(cy, dy) for _, cy, _, dy in samples])
    residual = sum(abs(dx - sx * cx - ox) + abs(dy - sy * cy - oy) for cx, cy, dx, dy in samples) / len(samples)
    return {'x': [1 + sx, ox], 'y': [1 + sy, oy], 'windows': len(samples), 'mean_residual': round(residual, 2), 'units': '2x source px'}


def align(generated: Image.Image, fit: dict) -> Image.Image:
    """Resample the output onto the source grid at SIZE (edges replicated)."""
    (ax, bx), (ay, by) = fit['x'], fit['y']
    k = 2
    w, h = generated.size
    out_per_2x = (w / (SOURCE[0] * k), h / (SOURCE[1] * k))
    step = (SOURCE[0] * k / SIZE[0], SOURCE[1] * k / SIZE[1])  # 2x px per output px
    pad = 64
    padded = Image.new('RGB', (w + 2 * pad, h + 2 * pad))
    padded.paste(generated.convert('RGB'), (pad, pad))
    padded.paste(padded.crop((pad, pad, w + pad, pad + 1)).resize((w, pad)), (pad, 0))
    padded.paste(padded.crop((pad, h + pad - 1, w + pad, h + pad)).resize((w, pad)), (pad, h + pad))
    padded.paste(padded.crop((pad, 0, pad + 1, h + 2 * pad)).resize((pad, h + 2 * pad)), (0, 0))
    padded.paste(padded.crop((w + pad - 1, 0, w + pad, h + 2 * pad)).resize((pad, h + 2 * pad)), (w + pad, 0))
    # output px (u, v) -> 2x px x = (u + .5) * step - .5 -> generated px = (ax*x + bx) * out_per_2x
    return padded.transform(SIZE, Image.Transform.AFFINE,
                             (ax * step[0] * out_per_2x[0], 0, (bx + ax * (step[0] / 2 - .5) + .5) * out_per_2x[0] - .5 + pad,
                              0, ay * step[1] * out_per_2x[1], (by + ay * (step[1] / 2 - .5) + .5) * out_per_2x[1] - .5 + pad),
                             resample=Image.Resampling.BICUBIC)


def dark_filter(bright: list, dark: list) -> ImageFilter.Color3DLUT:
    """LUT^3 table from bright to dark colours: inverse-distance weights over the six
    nearest palette pairs, each carried by its per-channel ratio, so colours between
    palette entries follow their neighbours. Applied trilinearly by Pillow."""
    pairs = [(b[:3], d[:3]) for b, d in zip(bright, dark) if b[3]]

    def mapped(r: float, g: float, b: float) -> tuple[float, float, float]:
        c = (r * 255, g * 255, b * 255)
        near = sorted(pairs, key=lambda p: sum((p[0][i] - c[i]) ** 2 for i in range(3)))[:6]
        total, weight = [0.0, 0.0, 0.0], 0.0
        for src, dst in near:
            w = 1 / (math.sqrt(sum((src[i] - c[i]) ** 2 for i in range(3))) + 8) ** 2
            for i in range(3):
                total[i] += w * (dst[i] * c[i] / src[i] if src[i] > 16 else dst[i] + (c[i] - src[i]) * .75)
            weight += w
        return tuple(max(0.0, min(1.0, v / weight / 255)) for v in total)
    return ImageFilter.Color3DLUT.generate(LUT, mapped)


def fidelity(high: Image.Image, source: Image.Image) -> float:
    small = high.convert('L').resize(SOURCE, Image.Resampling.BOX)
    return round(ImageStat.Stat(ImageChops.difference(small, source.convert('L'))).mean[0], 2)


def compose(args: argparse.Namespace) -> None:
    out = args.output
    samples = json.loads((out / 'samples.json').read_text())['samples']
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    (out / 'compose').mkdir(exist_ok=True)
    report = {}
    for sample in samples:
        source = Image.open(out / sample['source']).convert('RGB')
        rows = {}
        for model in MODELS:
            run = out / 'runs' / f"{sample['id']}--{model}--{CANDIDATE}" / 'request.json'
            if not run.exists() or json.loads(run.read_text())['status'] != 'completed':
                continue
            request = json.loads(run.read_text())
            generated = Image.open(out / request['output'])
            fit = register(generated, source)
            high = align(generated, fit)
            path = out / 'compose' / f"{sample['id']}-{model}.png"
            high.save(path)
            rows[model] = {'file': str(path.relative_to(out)), 'registration': fit, 'fidelity': fidelity(high, source)}
        report[sample['id']] = rows
        print(sample['id'], {m: (r['fidelity'], r['registration']['mean_residual'], round(r['registration']['x'][0], 4)) for m, r in rows.items()})
    (out / 'compose' / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    sheet(out, samples, report)


def sheet(out: Path, samples: list, report: dict) -> None:
    w, h, gap = 480, 360, 8
    columns = ['original'] + list(MODELS)
    image = Image.new('RGB', (len(columns) * (w + gap) + gap, len(samples) * (h + gap + 20) + gap), (236, 236, 236))
    draw = ImageDraw.Draw(image)
    for j, sample in enumerate(samples):
        y = gap + j * (h + gap + 20)
        tiles = [Image.open(out / sample['source']).convert('RGB').resize((w, h), Image.Resampling.NEAREST)]
        tiles += [Image.open(out / report[sample['id']][m]['file']).resize((w, h), Image.Resampling.LANCZOS)
                  if m in report[sample['id']] else Image.new('RGB', (w, h)) for m in MODELS]
        for i, tile in enumerate(tiles):
            image.paste(tile, (gap + i * (w + gap), y + 20))
            draw.text((gap + i * (w + gap), y + 4), f"{sample['id']} · {columns[i]}", fill=(20, 20, 20))
    image.save(out / 'compose' / 'sheet.png')


def build(args: argparse.Namespace) -> None:
    """Whole images for the host: bright from the chosen model's output, dark by table."""
    out = args.output
    target = args.images
    target.mkdir(parents=True, exist_ok=False)
    samples = json.loads((out / 'samples.json').read_text())['samples']
    report = json.loads((out / 'compose' / 'report.json').read_text())
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    rows = []
    for sample in samples:
        model = args.choice.get(sample['id'], args.model)
        bright = Image.open(out / report[sample['id']][model]['file']).convert('RGB')
        dark = bright.filter(dark_filter(colors(table, sample['bright']), colors(table, sample['dark'])))
        for palette, picture in ((sample['bright'], bright), (sample['dark'], dark)):
            name = f"background-{sample['image']}-{palette}.png"
            picture.save(target / name)
            rows.append({'image': sample['image'], 'palette': palette, 'file': name, 'sha256': sha(target / name),
                         'model': model})
        print(sample['id'], model)
    index = {'schema': 'srw64.background-images.v1', 'size': list(SIZE), 'source_size': list(SOURCE), 'images': rows}
    (target / 'backgrounds.json').write_text(json.dumps(index, indent=2) + '\n')
    if args.bind:
        path = ROOT / 'content/art/stage1-hd.json'
        manifest = json.loads(path.read_text())
        manifest['backgrounds'] = {'path': str(target.resolve().relative_to(ROOT)), 'manifest_sha256': sha(target / 'backgrounds.json')}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', path.relative_to(ROOT))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'run', 'compose', 'build'):
        sub = commands.add_parser(name)
        sub.add_argument('--output', type=Path, required=True)
        if name == 'run':
            sub.add_argument('--env-file', type=Path, required=True)
        if name == 'build':
            sub.add_argument('--images', type=Path, required=True, help='new folder for the whole images')
            sub.add_argument('--model', default=MODELS[0])
            sub.add_argument('--choice', type=json.loads, default={}, help='{"background-5470": "qwen-image-3.0"}')
            sub.add_argument('--bind', action='store_true')
    args = parser.parse_args()
    {'prepare': prepare, 'run': run, 'compose': compose, 'build': build}[args.command](args)


if __name__ == '__main__':
    main()
