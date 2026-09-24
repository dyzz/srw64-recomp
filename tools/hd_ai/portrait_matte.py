"""Matte an AI-redrawn portrait against the original portrait's mask.

The model returns an opaque picture on the flat grey backdrop that
prepare_stage1_portraits.py composited behind the source. The first matte
(stage1 pack-7) had four faults, found 2026-09-23:

- The premultiplied resize stored every transparent texel as black. RT64 and
  the RmlUi pages filter straight alpha, so the outline picked up a dark rim
  wherever the portrait is scaled.
- The model redraws up to 1.5 source px / 1% scale off the source (Lawrence
  shifted right, Manami shrunk), and the matte followed that drifted outline.
- The backdrop flood started from all four image edges, so clothes and hair
  cut by the frame lost pixels along the bottom and top.
- Gaps the original leaves transparent inside the outline (between hair
  strands) stayed grey.

Now the output is registered to the source first. The model may move the
outline only within BAND source px of the original mask, and the backdrop
flood starts only where the original is transparent. Every transparent texel
carries its nearest foreground colour, and the runtime downscale resizes
colour and alpha separately, so no filter can reach black or grey.
"""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
import math
from pathlib import Path
import shutil

from PIL import Image, ImageChops, ImageFilter, ImageStat

BACKDROP = (100, 100, 112)  # default colour composited behind the source before generation
WORK = 8                    # master pixels per source pixel
RUNTIME = 4                 # runtime pixels per source pixel (RT64 tiles, UI pages)
BAND = 1.5                  # source px the model may move the outline
EDGE = 3                    # master px of soft edge either side of the hard matte
TOLERANCE = 20              # largest channel distance still counted as backdrop
SOLID = 48                  # smallest channel distance counted as unmixed foreground


def flatten(source: Image.Image, backdrop: tuple = BACKDROP) -> Image.Image:
    flat = Image.new('RGBA', source.size, tuple(backdrop) + (255,))
    flat.alpha_composite(source.convert('RGBA'))
    return flat.convert('RGB')


def _best_shift(ref: Image.Image, cand: Image.Image, box: tuple, radius: int) -> tuple[int, int]:
    base = ref.crop(box)
    best = None
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            moved = cand.crop((box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy))
            error = ImageStat.Stat(ImageChops.difference(base, moved)).mean[0]
            if best is None or error < best[0]:
                best = (error, dx, dy)
    return best[1], best[2]


def _line(points: list[tuple[float, float]]) -> tuple[float, float]:
    n = len(points)
    mean_p = sum(p for p, _ in points) / n
    mean_q = sum(q for _, q in points) / n
    spread = sum((p - mean_p) ** 2 for p, _ in points)
    slope = sum((p - mean_p) * (q - mean_q) for p, q in points) / spread if spread else 0.0
    return slope, mean_q - slope * mean_p


def register(generated: Image.Image, source: Image.Image, radius: int = 10, backdrop: tuple = BACKDROP) -> dict:
    """Fit x' = ax*x + bx and y' = ay*y + by (runtime px) from source to model output.

    Local shifts of detailed 64 px windows are fitted per axis by least squares.
    """
    size = (source.width * RUNTIME, source.height * RUNTIME)
    blur = ImageFilter.GaussianBlur(2)
    ref = flatten(source, backdrop).resize(size, Image.Resampling.BICUBIC).convert('L').filter(blur)
    cand = generated.convert('RGB').resize(size, Image.Resampling.LANCZOS).convert('L').filter(blur)
    half, step = 32, 48
    samples = []
    for cy in range(step, size[1] - step + 1, step):
        for cx in range(step, size[0] - step + 1, step):
            box = (cx - half, cy - half, cx + half, cy + half)
            if ImageStat.Stat(ref.crop(box)).stddev[0] < 8:
                continue
            dx, dy = _best_shift(ref, cand, box, radius)
            samples.append((cx, cy, dx, dy))
    if len(samples) < 6:
        raise RuntimeError('portrait has too little detail to register')
    sx, ox = _line([(cx, dx) for cx, _, dx, _ in samples])
    sy, oy = _line([(cy, dy) for _, cy, _, dy in samples])
    residual = sum(abs(dx - sx * cx - ox) + abs(dy - sy * cy - oy) for cx, cy, dx, dy in samples) / len(samples)
    report = {'x': [round(1 + sx, 5), round(ox, 2)], 'y': [round(1 + sy, 5), round(oy, 2)],
              'windows': len(samples), 'mean_residual_px': round(residual, 2), 'units': 'runtime px'}
    # Above 3 px the model bent part of the face (flagged for review); above 5
    # it drew something else there.
    if max(abs(sx), abs(sy)) > 0.03 or max(abs(ox), abs(oy)) > 12 or residual > 5:
        raise RuntimeError(f'portrait registration needs inspection: {report}')
    return report


def align(generated: Image.Image, fit: dict, size: tuple[int, int]) -> Image.Image:
    """Resample the model output onto the source grid at master size."""
    (ax, bx), (ay, by) = fit['x'], fit['y']
    k = WORK / RUNTIME
    pad = 32
    high = generated.convert('RGB').resize(size, Image.Resampling.LANCZOS)
    w, h = size
    padded = Image.new('RGB', (w + 2 * pad, h + 2 * pad))
    padded.paste(high, (pad, pad))
    # Replicate the edges so frame-cut clothes do not pull in black when shifted.
    padded.paste(high.crop((0, 0, w, 1)).resize((w, pad), Image.Resampling.NEAREST), (pad, 0))
    padded.paste(high.crop((0, h - 1, w, h)).resize((w, pad), Image.Resampling.NEAREST), (pad, h + pad))
    padded.paste(padded.crop((pad, 0, pad + 1, h + 2 * pad)).resize((pad, h + 2 * pad), Image.Resampling.NEAREST), (0, 0))
    padded.paste(padded.crop((w + pad - 1, 0, w + pad, h + 2 * pad)).resize((pad, h + 2 * pad), Image.Resampling.NEAREST), (w + pad, 0))
    return padded.transform(size, Image.Transform.AFFINE, (ax, 0, bx * k + pad, 0, ay, by * k + pad),
                            resample=Image.Resampling.BICUBIC)


def _morph(mask: Image.Image, radius: int, grow: bool) -> Image.Image:
    kernel = ImageFilter.MaxFilter(3) if grow else ImageFilter.MinFilter(3)
    for _ in range(radius):
        mask = mask.filter(kernel)  # Pillow replicates edges, so frame cuts stay put
    return mask


def _neighbours(i: int, w: int, n: int):
    x = i % w
    if x:
        yield i - 1
    if x + 1 < w:
        yield i + 1
    if i >= w:
        yield i - w
    if i + w < n:
        yield i + w


def _extend_cut_edges(image: Image.Image, original: Image.Image, bg: tuple, depth: int) -> int:
    """Where the frame cuts the figure, carry the first solid colour within
    `depth` master px out to the edge. In a grid the model often stops short
    of the cell edge (up to ~2 source px) and leaves a sliver of gutter grey
    (130 of 300 cells); a figure that really is grey there has no solid pixel
    and is left alone. Returns the number of pixels replaced."""
    w, h = image.size
    px = image.load()
    distance = lambda c: max(abs(c[k] - bg[k]) for k in range(3))
    replaced = 0
    sides = (
        ((lambda t, k: (t, h - 1 - k)), w, lambda t: (t // WORK, original.height - 1)),
        ((lambda t, k: (t, k)), w, lambda t: (t // WORK, 0)),
        ((lambda t, k: (k, t)), h, lambda t: (0, t // WORK)),
        ((lambda t, k: (w - 1 - k, t)), h, lambda t: (original.width - 1, t // WORK)),
    )
    for at, length, source in sides:
        for t in range(length):
            if not original.getpixel(source(t)):
                continue
            k = next((k for k in range(depth + 1) if distance(px[at(t, k)]) > SOLID), None)
            # Only a gutter sliver: backdrop grey up to a 3 px anti-aliased
            # ramp; a darker fold inside a real grey garment stays put.
            if k and all(distance(px[at(t, j)]) <= TOLERANCE for j in range(k - 3)):
                color = px[at(t, k)]
                for j in range(k):
                    px[at(t, j)] = color
                replaced += k
    return replaced


def matte_portrait(generated: Image.Image, source: Image.Image,
                   backdrop: tuple = BACKDROP) -> tuple[Image.Image, dict]:
    """Return the RGBA master at WORK x the source size and a QA report.

    `backdrop` is the colour the source was composited on for the model.
    """
    source = source.convert('RGBA')
    w, h = source.width * WORK, source.height * WORK
    n = w * h
    fit = register(generated, source, backdrop=backdrop)
    image = align(generated, fit, (w, h))
    original = source.getchannel('A').point(lambda v: 255 if v else 0)
    mask = original.resize((w, h), Image.Resampling.BILINEAR).point(lambda v: 255 if v >= 128 else 0)
    band = round(BAND * WORK)
    sure_fg = _morph(mask, band, False)
    sure_bg = ImageChops.invert(_morph(mask, band, True))
    stat = ImageStat.Stat(image, sure_bg)
    bg = tuple(round(v) for v in stat.median) if stat.count[0] >= 2000 else tuple(backdrop)
    extended = _extend_cut_edges(image, original, bg, 3 * WORK)
    r, g, b = ImageChops.difference(image, Image.new('RGB', (w, h), bg)).split()
    distance = ImageChops.lighter(ImageChops.lighter(r, g), b)
    keyed = distance.point(lambda v: 255 if v <= TOLERANCE else 0)

    # Backdrop: flood from pixels the original leaves transparent (the outer
    # field and gaps between strands), never into the sure foreground.
    seeds = ImageChops.multiply(keyed, ImageChops.invert(mask)).tobytes()
    allowed = ImageChops.multiply(keyed, ImageChops.invert(sure_fg)).tobytes()
    reached = bytearray(n)
    queue = deque()
    for i, v in enumerate(seeds):
        if v:
            reached[i] = 1
            queue.append(i)
    while queue:
        i = queue.popleft()
        for j in _neighbours(i, w, n):
            if allowed[j] and not reached[j]:
                reached[j] = 1
                queue.append(j)
    outside = sure_bg.tobytes()
    keep = bytearray(0 if reached[i] or outside[i] else 1 for i in range(n))

    # Drop specks the model drew in the band that touch nothing of the original.
    inside = mask.tobytes()
    connected = bytearray(n)
    queue = deque()
    for i in range(n):
        if keep[i] and inside[i]:
            connected[i] = 1
            queue.append(i)
    while queue:
        i = queue.popleft()
        for j in _neighbours(i, w, n):
            if keep[j] and not connected[j]:
                connected[j] = 1
                queue.append(j)
    specks = sum(keep) - sum(connected)
    hard = Image.frombytes('L', (w, h), bytes(255 if v else 0 for v in connected))

    # Soft edge: unmix ring pixels against the nearest solid colour, i.e. one
    # clearly off the backdrop (usually the outline itself; eroding the matte
    # instead reaches past a thin outline to the hair behind it and fades the
    # line). The same nearest colour fills every transparent texel.
    far = distance.tobytes()
    solid = bytearray(1 if connected[i] and far[i] > SOLID else 0 for i in range(n))
    outer = _morph(hard, EDGE, True).tobytes()
    # A mix lies between the backdrop and the first solid pixel: reachable
    # from outside within EDGE steps without crossing solid colour. The same
    # colour just inside a thin outline is anti-aliasing against the skin.
    mixed = bytearray(n)
    queue = deque()
    for i in range(n):
        if not connected[i] and any(connected[j] for j in _neighbours(i, w, n)):
            queue.append((i, 0))
    while queue:
        i, depth = queue.popleft()
        if depth == EDGE:
            continue
        for j in _neighbours(i, w, n):
            if connected[j] and not solid[j] and not mixed[j]:
                mixed[j] = 1
                queue.append((j, depth + 1))
    rgb = image.tobytes()
    owner = [-1] * n
    queue = deque()
    for i in range(n):
        if solid[i]:
            owner[i] = i
            if any(not solid[j] for j in _neighbours(i, w, n)):
                queue.append(i)
    while queue:
        i = queue.popleft()
        for j in _neighbours(i, w, n):
            if owner[j] < 0:
                owner[j] = owner[i]
                queue.append(j)
    out = bytearray(n * 4)
    edge_pixels = 0
    for i in range(n):
        c = rgb[3 * i:3 * i + 3]
        if connected[i] and not mixed[i]:
            out[4 * i:4 * i + 4] = bytes((*c, 255))
            continue
        o = owner[i]
        f = rgb[3 * o:3 * o + 3] if o >= 0 else bytes(bg)
        v = [f[k] - bg[k] for k in range(3)]
        denominator = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]
        t = sum((c[k] - bg[k]) * v[k] for k in range(3)) / denominator if denominator else 0.0
        residual = max(abs(c[k] - bg[k] - t * v[k]) for k in range(3))
        if connected[i]:
            # Only a pixel between the backdrop and that colour is a mix.
            if 0 < t < 1 and residual <= 24:
                edge_pixels += 1
                alpha = round(255 * t)
            else:
                f, alpha = c, 255
        elif outer[i] and not outside[i]:
            edge_pixels += 1
            alpha = round(255 * min(1.0, max(0.0, t)))
        else:
            alpha = 0
        if alpha < 8:
            alpha = 0
        out[4 * i:4 * i + 4] = bytes((*f, alpha))
    master = Image.frombytes('RGBA', (w, h), bytes(out))

    small = master.getchannel('A').resize(source.size, Image.Resampling.BOX)
    old = [a >= 128 for a in original.tobytes()]
    new = [a >= 128 for a in small.tobytes()]
    union = sum(a or b for a, b in zip(old, new))
    frame = {}
    sw, sh = source.size
    # Frame cuts count only at least BAND source px (rounded up) from any
    # transparent source pixel: nearer, the outline may move with the band.
    deep = original.filter(ImageFilter.MinFilter(2 * math.ceil(BAND) + 1))
    for side, points in (('top', [(x, 0) for x in range(sw)]), ('bottom', [(x, sh - 1) for x in range(sw)]),
                         ('left', [(0, y) for y in range(sh)]), ('right', [(sw - 1, y) for y in range(sh)])):
        # Where the frame cuts through clothes or hair; the outline's own corners
        # (within 2 px along the frame) may move with the band.
        cut = [p for p in points if deep.getpixel(p)]
        if cut:
            frame[side] = round(sum(small.getpixel(p) >= 128 for p in cut) / len(cut), 4)
    report = {'method': 'registered to source; outline limited to the original mask +/- 1.5 source px; '
                        'backdrop flooded from transparent source pixels; nearest-foreground colour bleed',
              'registration': fit, 'background_rgb': list(bg), 'master_dimensions': [w, h],
              'edge_pixels': edge_pixels, 'removed_specks_px': specks, 'cut_edge_filled_px': extended,
              'cut_outside_band_px': _count_cut(outside, distance.tobytes()),
              'silhouette_iou_against_original': sum(a and b for a, b in zip(old, new)) / union,
              'foreground_area_ratio_against_original': sum(new) / sum(old),
              'frame_cut_kept': frame}
    report['warnings'] = [w for w, bad in (('local distortion', fit['mean_residual_px'] > 3),) if bad]
    if not .9 < report['foreground_area_ratio_against_original'] < 1.1 or any(v < .98 for v in frame.values()):
        raise RuntimeError(f'portrait matte needs inspection: {report}')
    return master, report


def _count_cut(outside: bytes, distance: bytes) -> int:
    """Model pixels beyond the band that are not backdrop (dropped by the matte)."""
    return sum(1 for o, d in zip(outside, distance) if o and d > TOLERANCE)


def runtime_image(master: Image.Image, scale: int = RUNTIME) -> Image.Image:
    """Downscale colour and alpha separately; a straight-alpha RGBA resize in
    Pillow premultiplies and would write black back into transparent texels."""
    size = (master.width * scale // WORK, master.height * scale // WORK)
    result = master.convert('RGB').resize(size, Image.Resampling.LANCZOS)
    result.putalpha(master.getchannel('A').resize(size, Image.Resampling.LANCZOS))
    return result


def filter_fringe(image: Image.Image, background: tuple[int, int, int], zoom: int = 3) -> dict:
    """Edge error of straight-alpha bilinear magnification (what RT64 and the
    RmlUi sampler do) against the correct premultiplied result: the dark or grey
    rim appears where transparent texels carry the wrong colour."""
    size = (image.width * zoom, image.height * zoom)
    channels = [c.resize(size, Image.Resampling.BILINEAR) for c in image.split()]
    straight = Image.new('RGBA', size, background + (255,))
    straight.alpha_composite(Image.merge('RGBA', channels))
    exact = Image.new('RGBA', size, background + (255,))
    exact.alpha_composite(image.resize(size, Image.Resampling.BILINEAR))  # Pillow premultiplies RGBA
    error = ImageChops.difference(straight.convert('L'), exact.convert('L'))
    edge = channels[3].point(lambda a: 255 if 0 < a < 255 else 0)
    histogram = error.histogram(edge)
    return {'edge_px': sum(histogram), 'over_16': sum(histogram[17:]),
            'mean': round(sum(k * v for k, v in enumerate(histogram)) / max(1, sum(histogram)), 2)}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reviewed_output(portraits: Path, sample: dict) -> Path:
    if 'reuse_reviewed_output' in sample:
        path, expected = Path(sample['reuse_reviewed_output']), sample['reuse_sha256']
        if not path.exists() and 'hd-ai' in path.parts:  # build/hd-ai moved to assets/hd-ai
            path = Path(__file__).resolve().parents[2] / 'assets' / Path(*path.parts[path.parts.index('hd-ai'):])
    else:
        request = json.loads((portraits / 'runs' / f"{sample['id']}--qwen-image-3.0--2/request.json").read_text())
        assert request['status'] == 'completed'
        path, expected = portraits / request['output'], request['output_sha256']
    if sha(path) != expected:
        raise ValueError(f"{sample['id']}: reviewed output changed")
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--portraits', type=Path, required=True, help='folder with samples.json, inputs/ and runs/')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pack', type=Path, help='RT64 pack to copy with these portraits\' tiles replaced')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    samples = json.loads((args.portraits / 'samples.json').read_text())['samples']
    records, tiles = [], {}
    for sample in samples:
        generated = reviewed_output(args.portraits, sample)
        source = Image.open(args.portraits / sample['source']).convert('RGBA')
        master, report = matte_portrait(Image.open(generated), source)
        high = runtime_image(master)
        master.save(args.output / f"{sample['id']}-master.png")
        high.save(args.output / f"{sample['id']}-{high.width}.png")
        report['filter_fringe'] = {name: filter_fringe(high, bg) for name, bg in
                                       (('white', (255, 255, 255)), ('black', (0, 0, 0)))}
        for binding in sample['binding']['bindings']:
            x, y = binding['xy']
            bw, bh = binding['draw_size']
            tiles[binding['hash']] = high.crop((x * RUNTIME, y * RUNTIME, (x + bw) * RUNTIME, (y + bh) * RUNTIME))
        records.append({'id': sample['id'], 'resource_id': sample['resource_id'], 'character': sample['character'],
                        'generation_sha256': sha(generated), 'runtime': f"{sample['id']}-{high.width}.png",
                        'runtime_sha256': sha(args.output / f"{sample['id']}-{high.width}.png"), 'matte': report})
        print(sample['id'], {k: report[k] for k in ('silhouette_iou_against_original', 'frame_cut_kept', 'filter_fringe')})
    result = {'schema': 'srw64.portrait-matte.v2', 'portraits': records}
    if args.pack:
        pack = args.output / 'pack'
        shutil.copytree(args.pack, pack)
        paths = {e['hashes']['rt64']: e['path'] for e in json.loads((pack / 'rt64.json').read_text())['textures']}
        for digest, tile in tiles.items():
            if not paths.get(digest, '').startswith('portrait-'):
                raise ValueError(f'{digest} is not a portrait tile of {args.pack}')
            target = pack / paths[digest]
            target.unlink()  # never write through into the copied-from pack
            tile.save(target)
        result['pack'] = {'source': str(args.pack), 'manifest_sha256': sha(pack / 'rt64.json'),
                          'portrait_tiles': {d: sha(pack / paths[d]) for d in sorted(tiles)}}
    (args.output / 'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
