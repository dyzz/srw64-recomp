"""Local ESRGAN upscales of the unit battle poses, for comparison with the AI redraws.

Runs community 4x ESRGAN models (spandrel) on the poses that unit_pose_hd.py
prepared. Colour and alpha go through the model separately: the colour image
has its transparent pixels filled with the nearest solid colour first (no grey
halo), and the alpha mask is upscaled as a grey image, which gives a smooth
anti-aliased edge. Two passes (16x, then Lanczos down to 8x) match the 8x
masters of the AI route.

    build/esrgan-venv/bin/python tools/hd_ai/esrgan_pose.py --samples DIR --models build/esrgan-models --output DIR2
        [--only MODEL ...] [--pro DIR3] [--blend 4x-UltraSharpV2 4x-PixelPerfectV4]

Settled recipe (2026-09-26, after comparing 14 models on three poses): the
50/50 blend of 4x-UltraSharpV2 (DAT, crisp and detailed but a touch too
sharp) and 4x-PixelPerfectV4 (softer). `--blend` writes it as hd/<id>.png,
named like the battle asset, for the HD package.

The venv is separate from .venv (torch): python3.14 -m venv build/esrgan-venv;
pip install torch spandrel pillow numpy. Models: 4x-PixelPerfectV4 (WTFPL,
OpenModelDB), 4x-AnimeSharp (CC-BY-NC-SA 4.0, Kim2091). Neither model nor its
output is part of the public package: the poses are ROM-derived.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image, ImageDraw
import spandrel
import torch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hd_ai.build_unit_images import clean_alpha  # noqa: E402  (PIL only; shared with the packer)

WORK = 8   # output pixels per source pixel, as unit_pose_hd masters


def fill_transparent(image: Image.Image) -> Image.Image:
    """RGB with every transparent pixel set to the mean of its nearest solid neighbours."""
    rgba = np.asarray(image.convert('RGBA')).astype(np.float32)
    rgb, alpha = rgba[..., :3], rgba[..., 3] > 0
    known = alpha.copy()
    while not known.all():
        padded = np.pad(rgb, ((1, 1), (1, 1), (0, 0)))
        weight = np.pad(known.astype(np.float32), 1)
        total = np.zeros_like(rgb)
        count = np.zeros(known.shape, np.float32)
        for dy in (0, 1, 2):
            for dx in (0, 1, 2):
                w = weight[dy:dy + known.shape[0], dx:dx + known.shape[1]]
                total += padded[dy:dy + known.shape[0], dx:dx + known.shape[1]] * w[..., None]
                count += w
        fill = ~known & (count > 0)
        rgb[fill] = total[fill] / count[fill][:, None]
        known |= fill
    return Image.fromarray(rgb.round().astype(np.uint8), 'RGB')


def upscale(model, image: Image.Image, device) -> Image.Image:
    array = np.asarray(image.convert('RGB')).astype(np.float32) / 255
    tensor = torch.from_numpy(array).permute(2, 0, 1).unsqueeze(0).to(device)
    with torch.no_grad():
        out = model(tensor)
    out = out.squeeze(0).permute(1, 2, 0).clamp(0, 1).cpu().numpy()
    return Image.fromarray((out * 255).round().astype(np.uint8), 'RGB')


def run_pose(model, source: Image.Image, device, passes: int | None = None) -> Image.Image:
    """Apply the model until the image is at least WORK x (4x models twice, 2x
    three times, 8x once), or exactly `passes` times, then Lanczos to WORK x."""
    colour, alpha = fill_transparent(source), source.getchannel('A').convert('RGB')
    done = 0
    while (passes is None and colour.width < source.width * WORK) or (passes is not None and done < passes):
        colour, alpha = upscale(model, colour, device), upscale(model, alpha, device)
        done += 1
    size = (source.width * WORK, source.height * WORK)
    if colour.size != size:
        colour, alpha = colour.resize(size, Image.Resampling.LANCZOS), alpha.resize(size, Image.Resampling.LANCZOS)
    result = colour.copy()
    result.putalpha(alpha.convert('L'))
    return clean_alpha(result, source)


def review(out: Path, sample: dict, columns: list[tuple[str, Image.Image]], per_row: int = 3) -> None:
    """A grid of tiles, one per model: the whole pose at 4x beside a 2x crop of its centre."""
    zoom, gap, blue = 4, 12, (12, 20, 48, 255)
    w, h = sample['source_size'][0] * zoom, sample['source_size'][1] * zoom
    tile_w, tile_h = 2 * w + gap, h + 24
    rows = -(-len(columns) // per_row)
    page = Image.new('RGB', (per_row * (tile_w + gap) + gap, rows * (tile_h + gap) + gap), (236, 236, 236))
    draw = ImageDraw.Draw(page)
    for i, (name, picture) in enumerate(columns):
        resample = Image.Resampling.NEAREST if i == 0 else Image.Resampling.LANCZOS
        tile = Image.new('RGBA', (w, h), blue)
        tile.alpha_composite(picture.resize((w, h), resample))
        x, y = gap + (i % per_row) * (tile_w + gap), gap + (i // per_row) * (tile_h + gap)
        page.paste(tile.convert('RGB'), (x, y + 24))
        crop = tile.crop((w // 4, h // 4, w * 3 // 4, h * 3 // 4)).resize((w, h), resample)
        page.paste(crop.convert('RGB'), (x + w + gap, y + 24))
        draw.text((x, y + 6), name, fill=(20, 20, 20))
    (out / 'review').mkdir(exist_ok=True, parents=True)
    page.save(out / 'review' / f"{sample['id']}.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--samples', type=Path, required=True, help='a unit_pose_hd prepare output (samples.json, inputs/)')
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pro', type=Path, help='unit_pose_hd output with hd/<id>.png to show in the last column')
    parser.add_argument('--only', nargs='*', default=[], help='model file stems to run (default: every model in --models)')
    parser.add_argument('--blend', nargs=2, metavar=('A', 'B'), help='the settled recipe: run A and B and write their 50/50 blend as hd/<id>.png')
    parser.add_argument('--per-row', type=int, default=3)
    args = parser.parse_args()
    device = torch.device('mps' if torch.backends.mps.is_available() else 'cpu')
    models = {}
    if args.blend:
        args.only = list(args.blend)
    for path in sorted(list(args.models.glob('*.pth')) + list(args.models.glob('*.safetensors'))):
        if args.only and path.stem not in args.only:
            continue
        try:
            models[path.stem] = spandrel.ModelLoader().load_from_file(str(path)).to(device).eval()
        except Exception as error:  # unsupported architecture or a broken download
            print(f'skip {path.name}: {type(error).__name__}: {error}', flush=True)
    samples = json.loads((args.samples / 'samples.json').read_text())['samples']
    (args.output / 'hd').mkdir(parents=True, exist_ok=True)
    report = {}
    for sample in samples:
        source = Image.open(args.samples / sample['source']).convert('RGBA')
        columns = [('original', source)]
        for name, model in models.items():
            start = time.monotonic()
            try:
                result = run_pose(model, source, device)
            except Exception as error:
                print(f'{name} failed on {sample["id"]}: {type(error).__name__}: {error}', flush=True)
                continue
            path = args.output / 'hd' / f"{sample['id']}--{name}.png"
            result.save(path)
            report[f"{sample['id']}--{name}"] = {'path': str(path), 'seconds': round(time.monotonic() - start, 2), 'arch': model.architecture.name, 'scale': model.scale}
            columns.append((name, result))
        if args.blend:
            # 2026-09-26 decision: UltraSharpV2 alone is a touch too sharp; half PixelPerfectV4 settles it.
            a, b = (Image.open(args.output / 'hd' / f"{sample['id']}--{m}.png").convert('RGBA') for m in args.blend)
            blend = Image.blend(a, b, .5)
            blend.save(args.output / 'hd' / f"{sample['id']}.png")
            report[sample['id']] = {'path': str(args.output / 'hd' / f"{sample['id']}.png"), 'recipe': f'50% {args.blend[0]} + 50% {args.blend[1]}',
                                    'size': list(blend.size), 'scene': sample['scene'], 'atlas': sample['atlas'], 'palette': sample['palette'], 'units': sample['units']}
            columns.append(('blend', blend))
        if args.pro and (args.pro / 'hd' / f"{sample['id']}.png").exists():
            columns.append(('qwen-image-3.0-pro', Image.open(args.pro / 'hd' / f"{sample['id']}.png").convert('RGBA')))
        review(args.output, sample, columns, args.per_row)
        print(sample['id'], [f'{c[0]} {c[1].size}' for c in columns], flush=True)
    (args.output / 'report.json').write_text(json.dumps({'device': str(device), 'work_scale': WORK, 'blend': args.blend, 'results': report}, indent=2) + '\n')


if __name__ == '__main__':
    main()
