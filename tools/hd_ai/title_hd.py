"""HD title screen art: the logo and the flame animation, as whole scene frames.

The title draws both as scene sprites (docs/native/native-title-and-story-images.md):
the logo is scene 683 (64 parts of 16x16 from atlas 681), the flames scene
686 (16 frames of 40 parts of 32x32 from atlas 684). Drawn part by part, the
bilinear filter leaves a seam at every part edge. The host draws each frame as
one image instead, so the masters here are whole frames.

Each flame frame is one 128x128 tile of the atlas repeated across the screen
(tile columns 3, 0-3, 0-3, 0). A tile is sent with 64 px of its own wrap on
either side, so the model draws across the seam; `compose` keeps the middle
and cross-fades its left edge into the model's continuation of the right
edge, which makes the tile wrap exactly.

`prepare` freezes the requests from the ROM, `run` sends them through
aliyun.run_one, `compose` registers, colour-locks and mattes the
outputs against the originals, and `build` writes the runtime set that
src/srw64_native/assets.py copies into the art pack.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import time

from PIL import Image, ImageChops, ImageFilter, ImageStat

from srw64_rom.resources import ResourceTable
from srw64_native.battle_graphics import parse_scene, render_scene
from srw64_native.original_images import decode_indexed
from tools.hd_ai.aliyun import ROOT, load_env, run_one

MODEL = 'qwen-image-3.0'
CANDIDATE = 2                   # seed 640903, as the portraits
LOGO = {'scene': 683, 'atlas': 681, 'palette': 682}
FLAME = {'scene': 686, 'atlas': 684, 'palette': 685}
LOGO_MARGIN = 16                # source px of black around the logo in the request
LOGO_SCALE = 7                  # 288x96 -> 2016x672
FLAME_SCALE = 4                 # 512x304 -> 2048x1216
TILE = 128                      # flame tile, source px
TOP = 16                        # source px above the tile for the frame-5 flame tips
HEIGHT = TOP + TILE
WRAP = 64                       # source px of the tile's own wrap on either side
GAP = 16                        # source px of black between request rows
FADE = 8                        # source px the left edge cross-fades into the wrap
LOCK_RADIUS = 2.0               # source px of low-frequency colour kept from the original


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scene_frame(table: ResourceTable, ids: dict, frame: int) -> Image.Image:
    data = lambda i: table.extract(i)[0]
    scene = parse_scene(data(ids['scene']))
    images, clipped = render_scene(scene, decode_indexed(data(ids['atlas']), data(ids['palette'])))
    if clipped:
        raise ValueError(f"scene {ids['scene']} reads outside its atlas")
    # render_scene draws every frame on the whole scene's bounds; the frames here share them.
    return images[frame]


def flame_tiles(table: ResourceTable) -> list[Image.Image]:
    """The 16 flame tiles as the game draws them, each TILE wide and TOP + TILE tall.

    Every frame repeats one 128x128 atlas tile at x -160..160, y -8..120.
    Frames 4 and 10 leave two 32x32 cells of the tile undrawn (the atlas holds
    unrelated yellow there), and frame 5 adds two 16x16 flame tips above the
    tile at y -24. So each tile is rebuilt from the parts actually drawn,
    folded onto the 128 px period, with a TOP px band for the tips; a cell
    drawn twice must agree with itself."""
    scene = parse_scene(table.extract(FLAME['scene'])[0])
    atlas = decode_indexed(table.extract(FLAME['atlas'])[0], table.extract(FLAME['palette'])[0])
    tiles = []
    for index, parts in enumerate(scene.frames):
        tile = Image.new('RGBA', (TILE, TOP + TILE))
        seen = {}
        for part in parts:
            if part.flip_x or part.y < -8 - TOP or part.y + part.h > 120:
                raise ValueError(f'flame frame {index} has an unexpected part')
            piece = atlas.crop((part.s, part.t, part.s + part.w, part.t + part.h))
            place = ((part.x + 128) % TILE, part.y + 8 + TOP)
            if place[0] + part.w > TILE:
                raise ValueError(f'flame frame {index} has a part across the tile period')
            key = place + piece.size
            if key in seen and ImageChops.difference(seen[key], piece).getbbox():
                raise ValueError(f'flame frame {index} is not one tile repeated')
            seen[key] = piece
            tile.paste(piece, place)
        tiles.append(tile)
    return tiles


def wrapped(tile: Image.Image) -> Image.Image:
    cell = Image.new('RGBA', (TILE + 2 * WRAP, HEIGHT))
    cell.paste(tile.crop((TILE - WRAP, 0, TILE, HEIGHT)), (0, 0))
    cell.paste(tile, (WRAP, 0))
    cell.paste(tile.crop((0, 0, WRAP, HEIGHT)), (WRAP + TILE, 0))
    return cell


def on_black(image: Image.Image) -> Image.Image:
    flat = Image.new('RGBA', image.size, (0, 0, 0, 255))
    flat.alpha_composite(image.convert('RGBA'))
    return flat.convert('RGB')


LOGO_PROMPT = ('忠实高清修复图1这张1999年游戏标题Logo「スーパーロボット大戦64」。严格保留每个字的字形、笔画粗细、'
               '倾斜、位置、黄色填充与深色描边、灰色金属底板的外形与铆钉、所有轮廓和配色，仅将像素锯齿恢复为清晰锐利的边缘，'
               '并把金属底板和黄色字面恢复为细腻的质感。不要改字，不要增减笔画，不要添加文字、光效或物体。'
               '黑色背景保持纯黑，画幅、大小和位置与输入完全相同。')
FLAME_PROMPT = ('图1是按2行2列排列的4张1999年游戏标题画面的火焰动画帧，每格是黑色背景上从下往上燃烧的熊熊烈火，'
                '格子之间是纯黑色间隔。逐格忠实高清修复：严格保留每一格火焰的形状、火舌的位置、明暗分布和红橙黄配色，'
                '仅将像素块和抖动噪点恢复为细腻自然的火焰纹理与光晕。不要改变构图，各格互不影响，不要合并，不要越过格子，'
                '不添加文字、烟雾或物体，黑色背景和间隔保持纯黑，画幅与输入完全相同。')


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / 'inputs').mkdir(parents=True, exist_ok=False)
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    samples = []
    logo = scene_frame(table, LOGO, 0)
    logo.save(out / 'inputs' / 'logo-source.png')
    canvas = Image.new('RGBA', (logo.width + 2 * LOGO_MARGIN, logo.height + 2 * LOGO_MARGIN))
    canvas.paste(logo, (LOGO_MARGIN, LOGO_MARGIN))
    path = out / 'inputs' / 'logo.png'
    on_black(canvas).resize((canvas.width * LOGO_SCALE, canvas.height * LOGO_SCALE), Image.Resampling.NEAREST).save(path)
    samples.append({'id': 'logo', 'input': str(path.relative_to(out)), 'input_sha256': sha(path),
                    'output_size': f'{canvas.width * LOGO_SCALE}*{canvas.height * LOGO_SCALE}',
                    'scale': LOGO_SCALE, 'source': 'inputs/logo-source.png', 'origin': [LOGO_MARGIN, LOGO_MARGIN],
                    'prompt': LOGO_PROMPT})
    tiles = flame_tiles(table)
    for k, tile in enumerate(tiles):
        tile.save(out / 'inputs' / f'flame-{k:02d}-source.png')
    width, height = 2 * (TILE + 2 * WRAP), 2 * HEIGHT + 3 * GAP
    for group in range(4):
        canvas = Image.new('RGBA', (width, height))
        cells = []
        for n in range(4):
            k = group * 4 + n
            origin = ((n % 2) * (TILE + 2 * WRAP), GAP + (n // 2) * (HEIGHT + GAP))
            canvas.paste(wrapped(tiles[k]), origin)
            cells.append({'frame': k, 'origin': list(origin), 'source': f'inputs/flame-{k:02d}-source.png'})
        path = out / 'inputs' / f'flame-{group}.png'
        on_black(canvas).resize((width * FLAME_SCALE, height * FLAME_SCALE), Image.Resampling.NEAREST).save(path)
        samples.append({'id': f'flame-{group}', 'input': str(path.relative_to(out)), 'input_sha256': sha(path),
                        'output_size': f'{width * FLAME_SCALE}*{height * FLAME_SCALE}', 'scale': FLAME_SCALE,
                        'cells': cells, 'prompt': FLAME_PROMPT})
    (out / 'samples.json').write_text(json.dumps({'schema': 'srw64.hd-ai-samples.v1',
        'purpose': f'title logo and flame frames; {MODEL}, seed of candidate {CANDIDATE}',
        'samples': samples}, ensure_ascii=False, indent=2) + '\n')
    print({'requests': len(samples), 'estimated_cny': round(len(samples) * 0.2, 2)})


def run(args: argparse.Namespace) -> None:
    out = args.output
    config = load_env(args.env_file)
    for sample in json.loads((out / 'samples.json').read_text())['samples']:
        if args.only and sample['id'] not in args.only:
            continue
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


def _shift(ref: Image.Image, cand: Image.Image, box: tuple, radius: int) -> tuple[int, int, float]:
    base = ref.crop(box)
    best = None
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            error = ImageStat.Stat(ImageChops.difference(base, cand.crop((box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy)))).mean[0]
            if best is None or error < best[2]:
                best = (dx, dy, error)
    return best


def fit(source: Image.Image, generated: Image.Image, scale: int, grid: int = 6, radius: int = 6) -> dict:
    """Per-axis scale and offset of the model output against the source, in output px.

    Window shifts are measured on a 2x grid (source nearest-scaled, output
    box-reduced), then x' = ax*x + bx is fitted by least squares; a 2x shift
    d = s*p + o becomes s*p + o*scale/2 in output px."""
    ref = source.convert('L').resize((source.width * 2, source.height * 2), Image.Resampling.NEAREST).filter(ImageFilter.GaussianBlur(1))
    cand = generated.convert('L').resize(ref.size, Image.Resampling.BOX).filter(ImageFilter.GaussianBlur(1))
    width, height = ref.size
    step_x, step_y = width // grid, height // max(1, grid * height // width)
    samples = []
    for y in range(radius, height - step_y - radius + 1, step_y):
        for x in range(radius, width - step_x - radius + 1, step_x):
            box = (x, y, x + step_x, y + step_y)
            if ImageStat.Stat(ref.crop(box)).stddev[0] < 6:
                continue  # flat black: no evidence
            dx, dy, _ = _shift(ref, cand, box, radius)
            samples.append(((box[0] + box[2]) / 2, (box[1] + box[3]) / 2, dx, dy))

    def line(points: list[tuple[float, float]]) -> tuple[float, float]:
        n = len(points)
        mx, my = sum(p for p, _ in points) / n, sum(q for _, q in points) / n
        spread = sum((p - mx) ** 2 for p, _ in points)
        slope = sum((p - mx) * (q - my) for p, q in points) / spread if spread else 0.0
        return slope, my - slope * mx
    sx, ox = line([(cx, dx) for cx, _, dx, _ in samples])
    sy, oy = line([(cy, dy) for _, cy, _, dy in samples])
    return {'x': [1 + sx, ox * scale / 2], 'y': [1 + sy, oy * scale / 2], 'windows': len(samples)}


def aligned(generated: Image.Image, placement: dict) -> Image.Image:
    (ax, bx), (ay, by) = placement['x'], placement['y']
    return generated.convert('RGB').transform(generated.size, Image.Transform.AFFINE, (ax, 0, bx, 0, ay, by),
                                              resample=Image.Resampling.BICUBIC)


def color_lock(generated: Image.Image, source: Image.Image, radius: float) -> Image.Image:
    """The model's detail over the source's low-frequency colour (as tactical_map_hd)."""
    smooth = source.convert('RGB').resize(generated.size, Image.Resampling.BICUBIC).filter(ImageFilter.GaussianBlur(radius))
    detail = ImageChops.subtract(generated, generated.filter(ImageFilter.GaussianBlur(radius)), 1, 128)
    return ImageChops.add(detail, smooth, 1, -128)


def soft_mask(alpha: Image.Image, scale: int) -> Image.Image:
    """The source's 1-bit alpha at `scale`, its stair steps smoothed into an edge one output px wide."""
    big = alpha.resize((alpha.width * scale, alpha.height * scale), Image.Resampling.NEAREST)
    big = big.filter(ImageFilter.GaussianBlur(scale * 0.45))
    return big.point(lambda a: max(0, min(255, (a - 128) * 4 + 128)))


def output_of(out: Path, sample: dict) -> Image.Image:
    report = json.loads((out / 'runs' / f"{sample['id']}--{MODEL}--{CANDIDATE}" / 'request.json').read_text())
    if report.get('status') != 'completed':
        raise SystemExit(f"{sample['id']} has no completed output")
    image = Image.open(out / report['output']).convert('RGB')
    expected = tuple(int(v) for v in sample['output_size'].split('*'))
    if image.size != expected:
        image = image.resize(expected, Image.Resampling.LANCZOS)
    return image


def compose(args: argparse.Namespace) -> None:
    out = args.output
    masters = out / 'masters'
    masters.mkdir(exist_ok=True)
    samples = json.loads((out / 'samples.json').read_text())['samples']
    report = {}
    for sample in samples:
        if args.only and sample['id'] not in args.only:
            continue
        generated, scale = output_of(out, sample), sample['scale']
        if sample['id'] == 'logo':
            source = Image.open(out / sample['source']).convert('RGBA')
            canvas = Image.new('RGBA', (source.width + 2 * LOGO_MARGIN, source.height + 2 * LOGO_MARGIN))
            canvas.paste(source, tuple(sample['origin']))
            placement = fit(on_black(canvas), generated, scale)
            straight = aligned(generated, placement)
            ox, oy = (v * scale for v in sample['origin'])
            logo = straight.crop((ox, oy, ox + source.width * scale, oy + source.height * scale))
            logo = color_lock(logo, on_black(source), LOCK_RADIUS * scale)
            logo.putalpha(soft_mask(source.getchannel('A'), scale))
            logo.save(masters / 'logo.png')
            report['logo'] = {'fit': placement, 'difference': difference(logo, source)}
            continue
        for cell in sample['cells']:
            tile = Image.open(out / cell['source']).convert('RGBA')
            ox, oy = cell['origin']
            box = (ox * scale, oy * scale, (ox + TILE + 2 * WRAP) * scale, (oy + HEIGHT) * scale)
            margin = GAP * scale // 2
            window = (box[0], box[1] - margin, box[2], box[3] + margin)
            reference = Image.new('RGBA', (TILE + 2 * WRAP, HEIGHT + GAP))
            reference.paste(wrapped(tile), (0, GAP // 2))
            placement = fit(on_black(reference), generated.crop(window), scale)
            # Resample from the cell with its edge rows repeated, not from the black gap:
            # a vertical offset would otherwise pull black into the bottom rows.
            cell_rgb = generated.crop(box)
            padded = Image.new('RGB', (cell_rgb.width, cell_rgb.height + 2 * margin))
            padded.paste(cell_rgb, (0, margin))
            padded.paste(cell_rgb.crop((0, 0, cell_rgb.width, 1)).resize((cell_rgb.width, margin)), (0, 0))
            padded.paste(cell_rgb.crop((0, cell_rgb.height - 1, cell_rgb.width, cell_rgb.height)).resize((cell_rgb.width, margin)),
                         (0, margin + cell_rgb.height))
            cellular = aligned(padded, placement).crop((0, margin, box[2] - box[0], margin + HEIGHT * scale))
            middle = cellular.crop((WRAP * scale, 0, (WRAP + TILE) * scale, HEIGHT * scale))
            # The model's continuation past the right edge is what the left edge must meet.
            onward = cellular.crop(((WRAP + TILE) * scale, 0, (WRAP + TILE + FADE) * scale, HEIGHT * scale))
            ramp = Image.linear_gradient('L').rotate(-90, expand=True).resize(onward.size)  # 255 at left
            left = Image.composite(onward, middle.crop((0, 0, FADE * scale, HEIGHT * scale)), ramp)
            middle.paste(left, (0, 0))
            # Colour and alpha against the tile wrapped three wide, so the blur and edge see across the seam.
            wide_source = Image.new('RGBA', (3 * TILE, HEIGHT))
            for k in range(3):
                wide_source.paste(tile, (k * TILE, 0))
            wide = Image.new('RGB', (3 * TILE * scale, HEIGHT * scale))
            for k in range(3):
                wide.paste(middle, (k * TILE * scale, 0))
            wide = color_lock(wide, on_black(wide_source), LOCK_RADIUS * scale)
            final = wide.crop((TILE * scale, 0, 2 * TILE * scale, HEIGHT * scale))
            final.putalpha(soft_mask(wide_source.getchannel('A'), scale).crop((TILE * scale, 0, 2 * TILE * scale, HEIGHT * scale)))
            name = f"flame-{cell['frame']:02d}.png"
            final.save(masters / name)
            seam = seam_error(final)
            report[name] = {'fit': placement, 'difference': difference(final, tile), 'wrap_seam': seam}
    (out / 'compose.json').write_text(json.dumps(report, indent=2) + '\n')
    review(out)
    print(json.dumps({k: {'difference': v['difference'], **({'wrap_seam': v['wrap_seam']} if 'wrap_seam' in v else {})}
                      for k, v in report.items()}, indent=1))


def difference(high: Image.Image, source: Image.Image) -> float:
    """Mean colour difference once scaled back to the source size, over opaque source pixels."""
    small = high.convert('RGB').resize(source.size, Image.Resampling.BOX)
    mask = source.getchannel('A').point(lambda a: 255 if a else 0)
    return round(ImageStat.Stat(ImageChops.difference(small, source.convert('RGB')).convert('L'), mask).mean[0], 2)


def seam_error(tile: Image.Image) -> float:
    """Mean step across the wrap (last column to first) relative to the mean step between neighbours."""
    rgb = tile.convert('L')
    w, h = rgb.size
    across = ImageStat.Stat(ImageChops.difference(rgb.crop((w - 1, 0, w, h)), rgb.crop((0, 0, 1, h)))).mean[0]
    inside = ImageStat.Stat(ImageChops.difference(rgb.crop((w // 2 - 1, 0, w // 2, h)), rgb.crop((w // 2, 0, w // 2 + 1, h)))).mean[0]
    return round(across / max(inside, 1e-3), 2)


def review(out: Path) -> None:
    """Originals (nearest) beside the masters on black; the flames shown two tiles wide to expose the wrap."""
    masters = out / 'masters'
    rows = []
    if (masters / 'logo.png').exists():
        high = Image.open(masters / 'logo.png')
        source = Image.open(out / 'inputs' / 'logo-source.png').resize(high.size, Image.Resampling.NEAREST)
        rows.append((source, high))
    for k in range(16):
        path = masters / f'flame-{k:02d}.png'
        if not path.exists():
            continue
        high = Image.open(path)
        source = Image.open(out / 'inputs' / f'flame-{k:02d}-source.png').resize(high.size, Image.Resampling.NEAREST)
        twice = lambda im: (lambda c: (c.paste(im, (0, 0)), c.paste(im, (im.width, 0)), c)[2])(Image.new('RGBA', (im.width * 2, im.height)))
        rows.append((twice(source), twice(high)))
    if not rows:
        return
    width = max(a.width for a, _ in rows)
    sheet = Image.new('RGBA', (width * 2 + 16, sum(a.height + 16 for a, _ in rows)), (0, 0, 0, 255))
    y = 0
    for a, b in rows:
        sheet.alpha_composite(a.convert('RGBA'), (0, y))
        sheet.alpha_composite(b.convert('RGBA'), (width + 16, y))
        y += a.height + 16
    sheet.thumbnail((2400, 100000))
    sheet.save(out / 'review.png')


def build(args: argparse.Namespace) -> None:
    """The runtime set: whole-frame images keyed by (scene, atlas, palette, frame)."""
    out, target = args.output, args.target
    target.mkdir(parents=True, exist_ok=False)
    images = []
    logo = out / 'masters' / 'logo.png'
    shutil.copyfile(logo, target / 'title-logo.png')
    images.append({**LOGO, 'frames': [0], 'file': 'title-logo.png', 'uv': [0, 0, 1, 1], 'wrap': False,
                   'sha256': sha(target / 'title-logo.png')})
    # A frame spans x -160..160 and its tile starts at -128, so u runs -0.25..2.25.
    # It spans y -8..120, or -24..120 when it draws flame tips into the top band.
    for k in range(16):
        name = f'title-flame-{k:02d}.png'
        shutil.copyfile(out / 'masters' / f'flame-{k:02d}.png', target / name)
        source = Image.open(out / 'inputs' / f'flame-{k:02d}-source.png')
        top = 0 if source.getchannel('A').crop((0, 0, TILE, TOP)).getbbox() else TOP / HEIGHT
        images.append({**FLAME, 'frames': [k], 'file': name, 'uv': [-0.25, top, 2.25, 1], 'wrap': True,
                       'sha256': sha(target / name)})
    (target / 'scene-images.json').write_text(json.dumps({'schema': 'srw64.scene-images.v1',
        'note': 'Whole scene frames drawn in place of their parts (native_sprite.cpp).',
        'images': images}, indent=2) + '\n')
    print({'images': len(images), 'target': str(target)})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'run', 'compose', 'build'):
        command = commands.add_parser(name)
        command.add_argument('--output', type=Path, required=True)
        if name == 'run':
            command.add_argument('--env-file', type=Path, required=True)
        if name in ('run', 'compose'):
            command.add_argument('--only', nargs='*')
        if name == 'build':
            command.add_argument('--target', type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'run': run, 'compose': compose, 'build': build}[args.command](args)


if __name__ == '__main__':
    main()
