"""Bring the image_gen redraws of the units' extra battle images into the derived set.

The kit (assets/hd-ai/unit-extra-imagegen, selected-manifest.json) holds 62 pictures
drawn with image_gen (docs/design/battle-animation-rendering.md §13): 01-15 whole new
pictures, 16-62 the standing pose with a part redrawn (marked red in items/*-changed.png,
grey where it equals the pose). Each input is the target frame on a known canvas: the
scene's bounds, or for a local edit `bounds_world` (the frame and the pose together).

compose, per picture:
  1. register and matte the output against the original frame (portrait_matte: the
     model may move the outline 1.5 source px at most), giving an 8x master;
  2. local edits: keep the HD standing pose wherever the frame equals it and take the
     redraw only in the red area, feathered across the seam;
  3. the frame's other animation frames (and `also` scenes) come from the drawn one
     through derive_frame, as the derived extras do;
  4. write them into assets/hd-ai/unit-extras-derived with method `image_gen` and add
     them to its manifest, so `unit_extra_derive.py --pack` ships them.

    build/esrgan-venv/bin/python tools/hd_ai/unit_extra_imagegen.py compose [--only ID ...]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/hd_ai'), str(ROOT / 'src'), str(ROOT)]
from unit_extra_derive import HD, K, OUT, Rom, derive_frame, place  # noqa: E402
from tools.hd_ai.portrait_matte import matte_portrait  # noqa: E402

KIT = ROOT / 'assets/hd-ai/unit-extra-imagegen'
GREY = (100, 100, 112)
FEATHER = 1.0          # source px of soft seam between the HD pose and the redraw
GROW = 1               # source px the red area is widened before feathering
FONT = '/System/Library/Fonts/Hiragino Sans GB.ttc'
RELAXED = (0.09, 20, 9)  # registration limits for the second try (scale, offset, residual)


def rgba(array: np.ndarray) -> Image.Image:
    return Image.fromarray(np.clip(array, 0, 255).round().astype(np.uint8), 'RGBA')


def changed_mask(path: Path, size: tuple[int, int]) -> np.ndarray:
    """The kit's red area (changed from the pose), widened and feathered, at `size` (0-1)."""
    marks = np.asarray(Image.open(path).convert('RGB')).astype(int)
    red = (marks[..., 0] > 200) & (marks[..., 1] < 60) & (marks[..., 2] < 60)
    mask = Image.fromarray((red * 255).astype(np.uint8)).resize(size, Image.Resampling.NEAREST)
    if GROW:
        mask = mask.filter(ImageFilter.MaxFilter(2 * GROW * K + 1))
    mask = mask.filter(ImageFilter.GaussianBlur(FEATHER * K))
    return np.asarray(mask).astype(np.float32) / 255


def compose_item(rom: Rom, item: dict) -> tuple[list[dict], dict]:
    scene, palette = item['scene'], item['palette']
    atlas = rom.atlas_of.get(scene, item['atlas'])
    frames, bounds = rom.frames(scene, atlas, palette)
    by_frame = dict(frames)
    drawn = item.get('drawn_frame', frames[0][0])
    target = by_frame[drawn]
    world = item.get('bounds_world') or list(bounds)
    source = rgba(place(target, bounds, world))
    generated = Image.open(KIT / item['output'])
    try:
        master, report = matte_portrait(generated, source, GREY)
    except RuntimeError:
        # whole new pictures drawn a few per cent large or small, and local edits whose
        # redrawn arm sits apart from the original's, pass here and are looked at by hand
        master, report = matte_portrait(generated, source, GREY, RELAXED)
        report['relaxed'] = True
    picture = np.asarray(master.convert('RGBA')).astype(np.float32)
    if item.get('changed_map'):
        pose_triplet = tuple(item['reference_pose'])
        hd = np.asarray(Image.open(HD / 'unit-{}-{}-{}.png'.format(*pose_triplet)).convert('RGBA')).astype(np.float32)
        pose_bounds = rom.frames(*pose_triplet)[1]
        pose = place(hd, pose_bounds, world, K)
        m = changed_mask(KIT / item['changed_map'], (picture.shape[1], picture.shape[0]))
        keep = (pose[..., 3] > 0) & (picture[..., 3] > 0)
        m = np.where(keep, m, 1.0)[..., None]
        picture[..., :3] = pose[..., :3] * (1 - m) + picture[..., :3] * m
        report['changed_share'] = round(float((m[..., 0] > .5).mean()), 3)
    # back onto the scene's own canvas
    y, x = (bounds[1] - world[1]) * K, (bounds[0] - world[0]) * K
    h, w = (bounds[3] - bounds[1]) * K, (bounds[2] - bounds[0]) * K
    hd_frame = picture[y:y + h, x:x + w]
    rows = []

    def save(sc: int, pal: int, frame: int, image: np.ndarray, how: str) -> None:
        name = f'{sc}-{pal}-f{frame}.png'
        rgba(image).save(OUT / name)
        rows.append({'scene': sc, 'palette': pal, 'frame': frame, 'file': name, 'how': how})

    save(scene, palette, drawn, hd_frame, 'image_gen')
    for f, other in frames:
        if f != drawn:
            image, _ = derive_frame(other, bounds, target, bounds, hd_frame, False)
            save(scene, palette, f, image, f'derived from frame {drawn}')
    for also in item.get('also', []):
        a_atlas = rom.atlas_of.get(also, atlas)
        a_frames, a_bounds = rom.frames(also, a_atlas, palette)
        for f, other in a_frames:
            image, _ = derive_frame(other, a_bounds, target, bounds, hd_frame, False)
            save(also, palette, f, image, f'derived from scene {scene} frame {drawn}')
    return rows, report


def compose(only: list[str]) -> None:
    rom = Rom()
    items = json.loads((KIT / 'selected-manifest.json').read_text())['items']
    if only:
        items = [i for i in items if i['id'] in only]
    manifest = json.loads((OUT / 'manifest.json').read_text())
    entries = manifest['items']
    reports, previews = {}, []
    for item in items:
        try:
            rows, report = compose_item(rom, item)
        except RuntimeError as error:   # registration out of bounds: needs a look
            reports[item['id']] = {'status': 'failed', 'error': str(error)}
            print(item['id'], 'FAILED', error, flush=True)
            continue
        reports[item['id']] = {'status': 'composed', **report}
        for scene in {r['scene'] for r in rows}:
            atlas = rom.atlas_of.get(scene, item['atlas'])
            bounds = rom.frames(scene, atlas, item['palette'])[1]
            entries = [e for e in entries if (e['scene'], e['palette']) != (scene, item['palette'])]
            entries.append({'scene': scene, 'atlas': atlas, 'palette': item['palette'], 'unit': item['unit'],
                            'method': 'image_gen', 'kit_id': item['id'], 'kit_output': item['output'],
                            'hd_pose': item.get('reference_pose'), 'bounds': list(bounds), 'scale': K,
                            'frames': [{'frame': r['frame'], 'file': r['file'], 'how': r['how']} for r in rows if r['scene'] == scene]})
        fit = report['registration']
        print(item['id'], 'RELAXED' if report.get('relaxed') else '', len(rows), 'frames', f"fit x{fit['x']} y{fit['y']} iou {report['silhouette_iou_against_original']:.3f}",
              report.get('warnings', ''), flush=True)
        previews.append((item, rows[0]['file']))
    manifest['items'] = entries
    (OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=1))
    report_path = KIT / 'compose-report.json'
    if only and report_path.exists():
        reports = {**json.loads(report_path.read_text()), **reports}
    report_path.write_text(json.dumps(reports, ensure_ascii=False, indent=1) + '\n')
    if not only:
        review(rom, previews)


def review(rom: Rom, previews: list) -> None:
    """Original frame beside its HD frame, two pairs per row, sheets of 16."""
    font = ImageFont.truetype(FONT, 18)
    T = 300
    for page in range(0, len(previews), 16):
        chunk = previews[page:page + 16]
        sheet = Image.new('RGB', (2 * (2 * T + 24), -(-len(chunk) // 2) * (T + 32) + 8), (28, 30, 36))
        draw = ImageDraw.Draw(sheet)
        for i, (item, file) in enumerate(chunk):
            x, y = 8 + (i % 2) * (2 * T + 24), 6 + (i // 2) * (T + 32)
            draw.text((x, y), f"{item['id']}  {item['unit']}", fill='white', font=font)
            frames, _ = rom.frames(item['scene'], rom.atlas_of.get(item['scene'], item['atlas']), item['palette'])
            original = rgba(dict(frames)[item.get('drawn_frame', frames[0][0])])
            for j, image in enumerate((original, Image.open(OUT / file).convert('RGBA'))):
                k = min(T / image.width, T / image.height)
                image = image.resize((max(1, round(image.width * k)), max(1, round(image.height * k))),
                                     Image.Resampling.NEAREST if j == 0 else Image.Resampling.LANCZOS)
                tile = Image.new('RGBA', (T, T), GREY + (255,))
                tile.alpha_composite(image, ((T - image.width) // 2, (T - image.height) // 2))
                sheet.paste(tile.convert('RGB'), (x + j * (T + 8), y + 24))
        sheet.save(KIT / f'composed-{page // 16 + 1}.jpg', quality=88)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('compose',))
    parser.add_argument('--only', nargs='*', default=[])
    args = parser.parse_args()
    compose(args.only)


if __name__ == '__main__':
    main()
