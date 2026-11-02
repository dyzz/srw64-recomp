"""HD frames of the kill explosions, rendered by program rather than upscaled.

A unit shot down runs hit script 156 (warships and the large bosses, battle unit record
column 6 == 2), 157 or 158 (the same two); each puts seven actors on, behaviour 382, drawing
battle_scenes 1042 (a small fireball, 11 frames, five of them staggered), 1043 (the big
flash, at alpha 190), and 1044 (the warships' white ball and debris) or 1045 (sparks and
debris) (docs/design/battle-animation-rendering.md §6.4, §16). Each frame is rendered at
PACK_SCALE from the original frame as a guide: the same canvas, outline and step timing,
with turbulent fire cooling from white to smoke by noise that runs on with the ticks, a
soft flash, a plasma ball, glowing sparks with tails away from the blast and shaded metal
shards for the dark pieces. Nothing is drawn past a few pixels of the original outline.

    .venv/bin/python tools/hd_ai/battle_explosion_hd.py render      # masters + review sheet
    .venv/bin/python tools/hd_ai/battle_explosion_hd.py pack --into assets/hd-ai/cutins/pack-vN [--bind]

`pack` hard-links the bound battle_sprites pack into a new folder as cutin_hd.py extend does
and adds (or replaces) the explosion frames, cut to each frame's parts box; cutin_hd.py
extend keeps them, its full `pack` does not.

Output under assets/hd-ai/battle-explosions: hd/<scene>-<atlas>-<palette>-f<n>.png (the
whole scene canvas at PACK_SCALE) and review.png (original above, HD below).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import decode_atlas, parse_scene, read_triplets, render_scene  # noqa: E402
from tools.hd_ai.unit_extra_derive import PACK_SCALE, pack_image  # noqa: E402

OUT = ROOT / 'assets/hd-ai/battle-explosions'
MANIFEST = ROOT / 'content/art/stage1-hd.json'
REGISTRIES = (1042, 1043, 1044, 1045)
S = PACK_SCALE
_scenes: dict = {}


def scene_of(reg: int):
    """(triplet, Scene, original frames as float RGBA on the scene's canvas, ticks per frame, origin)."""
    if reg not in _scenes:
        rom = (ROOT / 'rom.z64').read_bytes()
        table = ResourceTable(rom)
        s, a, p = read_triplets(rom, 'battle_scenes')[reg]
        scene = parse_scene(table.extract(s)[0])
        atlas, _ = decode_atlas(table.extract(a)[0], table.extract(p)[0])
        images, _ = render_scene(scene, atlas)
        ticks = [0] * len(scene.frames)
        for frame, length in scene.steps:
            if frame != 0xFF:
                ticks[frame] += length
        x0, y0 = scene.bounds()[:2]
        frames = [np.asarray(i.convert('RGBA')).astype(np.float32) / 255 for i in images]
        _scenes[reg] = ((s, a, p), scene, frames, ticks, (-x0, -y0))
    return _scenes[reg]


class _Scenes:
    """SCENES[reg] -> (triplet, frames, width, height, ticks per frame, origin)."""

    def __getitem__(self, reg):
        triplet, _, frames, ticks, origin = scene_of(reg)
        return triplet, len(frames), frames[0].shape[1], frames[0].shape[0], ticks, origin


SCENES = _Scenes()


def frames(reg):
    return scene_of(reg)[2]


def start_ticks(reg):
    ticks = scene_of(reg)[3]
    return np.concatenate([[0], np.cumsum(ticks)[:-1]])


# ---------------------------------------------------------------- noise
class Noise:
    """Smooth value noise on a 3D lattice (t, y, x), cubic-spline sampled."""

    def __init__(self, seed, h, w, cell, tcell, tlen=80):
        rng = np.random.default_rng(seed)
        self.cell, self.tcell = cell, tcell
        self.grid = ndi.spline_filter(rng.random((int(tlen / tcell) + 4, int(h / cell) + 4, int(w / cell) + 4)), order=3)

    def __call__(self, ys, xs, t):
        tz = np.full(ys.shape, t / self.tcell + 1)
        return ndi.map_coordinates(self.grid, [tz, ys / self.cell + 1, xs / self.cell + 1], order=3, prefilter=False, mode='nearest')


def fbm(seed, ys, xs, t, h, w, cell, tcell, octaves=4):
    total, amp, norm = 0, 1.0, 0
    for o in range(octaves):
        n = Noise(seed * 31 + o, h, w, cell / 2 ** o, tcell / 1.6 ** o)
        total = total + amp * n(ys, xs, t)
        norm += amp
        amp *= 0.5
    return total / norm


# ---------------------------------------------------------------- helpers
def upscale(frame):
    """Premultiplied bicubic 5x of an original frame, then softened: the guide."""
    a = frame[..., 3:4]
    pm = np.concatenate([frame[..., :3] * a, a], -1)
    im = Image.fromarray((pm * 255).astype(np.uint8), 'RGBA').resize((frame.shape[1] * S, frame.shape[0] * S), Image.BICUBIC)
    g = np.asarray(im).astype(np.float32) / 255
    g = ndi.gaussian_filter(g, (S * 0.7, S * 0.7, 0))
    a = g[..., 3:4]
    rgb = np.where(a > 1e-3, g[..., :3] / np.maximum(a, 1e-3), 0)
    return np.clip(rgb, 0, 1), a[..., 0]


def ramp(t, stops):
    t = np.clip(t, 0, 1)
    xs = [s[0] for s in stops]
    out = np.zeros(t.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, xs, [s[1][c] / 255 for s in stops])
    return out


FIRE = [(0.0, (70, 58, 54)), (0.18, (130, 44, 18)), (0.36, (215, 85, 18)), (0.55, (250, 155, 35)),
        (0.72, (255, 210, 80)), (0.86, (255, 238, 160)), (0.96, (255, 252, 235)), (1.0, (255, 255, 250))]


def bleed(rgb, alpha):
    """Fill the colour of transparent pixels from their nearest opaque neighbour (no dark fringe)."""
    mask = alpha < 0.02
    if mask.all():
        return rgb
    _, (iy, ix) = ndi.distance_transform_edt(mask, return_indices=True)
    return rgb[iy, ix]


def save(rgb, alpha, path):
    rgb = bleed(rgb, alpha)
    out = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)])
    Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA').save(path)


def over(dst_rgb, dst_a, src_rgb, src_a):
    a = src_a + dst_a * (1 - src_a)
    rgb = (src_rgb * src_a[..., None] + dst_rgb * (dst_a * (1 - src_a))[..., None]) / np.maximum(a, 1e-4)[..., None]
    return rgb, a


def add_glow(rgb, alpha, hot, radius, strength):
    """A soft halo from the hottest parts, kept inside a few pixels of the silhouette."""
    glow = ndi.gaussian_filter(hot, radius) * strength
    glow_rgb = np.broadcast_to(np.array([1.0, 0.72, 0.35], np.float32), rgb.shape)
    ga = np.clip(glow, 0, 0.85)
    return over(glow_rgb.copy(), ga, rgb, alpha)


# ---------------------------------------------------------------- 1042: fireball
def guide_temperature(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.3 * r + 0.59 * g + 0.11 * b
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 1e-3, (mx - mn) / np.maximum(mx, 1e-3), 0)
    t = (g ** 0.9) * r
    smoke = np.clip((0.5 - sat) / 0.2, 0, 1) * np.clip((0.82 - y) / 0.2, 0, 1)
    return t * (1 - smoke), smoke


def fireball(reg=1042, seed=1):
    _, n, w, h, _, _ = SCENES[reg]
    H, W = h * S, w * S
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    starts = start_ticks(reg)
    out = []
    for i, frame in enumerate(frames(reg)):
        t = float(starts[i])
        rgb, a = upscale(frame)
        temp, smoke = guide_temperature(rgb)
        # Billowing edges: warp the guide by noise that drifts outwards over time.
        wx = fbm(seed + 1, ys, xs, t, H, W, 22 * S, 9) - 0.5
        wy = fbm(seed + 2, ys, xs, t, H, W, 22 * S, 9) - 0.5
        amp = 4.5 * S
        coords = [np.clip(ys + wy * amp, 0, H - 1), np.clip(xs + wx * amp, 0, W - 1)]
        a2 = ndi.map_coordinates(a, coords, order=1)
        t2 = ndi.map_coordinates(temp, coords, order=1)
        s2 = ndi.map_coordinates(smoke, coords, order=1)
        detail = fbm(seed + 3, ys, xs, t * 1.3, H, W, 9 * S, 5, octaves=5)
        p = i / (n - 1)
        heat = np.clip(t2 * (0.9 + 0.9 * (detail - 0.5)), 0, 1)
        # Edges cool off (thin flame at the rim), the inside keeps the guide's heat.
        heat = heat * np.clip(0.55 + a2, 0, 1)
        col = ramp(heat, FIRE)
        # Smoke: lit from the fire below/inside, shaded by the noise slope.
        billow = fbm(seed + 4, ys, xs, t, H, W, 7 * S, 6, octaves=3)
        gy, gx = np.gradient(ndi.gaussian_filter(billow, 1.2 * S))
        shade = np.clip(0.88 - 9 * S * (gy * 0.7 + gx * 0.3) + 0.25 * (billow - 0.5), 0.45, 1.15)
        smoke_col = np.stack([0.66 * shade, 0.6 * shade, 0.56 * shade], -1)
        sm = (np.clip(s2 * 1.5 + 0.9 * p, 0, 1) * np.clip((0.5 - heat) / 0.38, 0, 1) ** 0.8)[..., None]
        col = col * (1 - sm) + smoke_col * sm
        alpha = np.clip(a2 * (1.1 + 0.6 * (detail - 0.5)), 0, 1) * (1 - 0.3 * sm[..., 0])
        alpha = np.clip((alpha - 0.03) / 0.97, 0, 1)
        hot = np.clip((heat - 0.55) / 0.45, 0, 1) * alpha
        col, alpha = add_glow(col, alpha, hot, 2.5 * S, 0.9 * (1 - 0.6 * p))
        out.append((col, alpha))
    return out


# ---------------------------------------------------------------- shards and sparks
def components(frame, min_area=1):
    a = frame[..., 3] > 0.05
    lab, n = ndi.label(a)
    comps = []
    for k in range(1, n + 1):
        m = lab == k
        area = int(m.sum())
        if area < min_area:
            continue
        cy, cx = ndi.center_of_mass(m)
        rgb = frame[..., :3][m].mean(0)
        y = float(0.3 * rgb[0] + 0.59 * rgb[1] + 0.11 * rgb[2])
        comps.append(dict(mask=m, area=area, cy=cy, cx=cx, rgb=rgb, y=y, hot=bool(rgb[0] - rgb[2] > 0.35 or y > 0.6)))
    return comps


def shard(draw, cx, cy, size, angle, seed, fill, edge):
    rng = np.random.default_rng(seed)
    k = int(rng.integers(4, 7))
    pts = []
    for j in range(k):
        th = angle + 2 * math.pi * j / k + rng.uniform(-0.35, 0.35)
        r = size * rng.uniform(0.55, 1.15) * (1.6 if j == 0 else 1)
        pts.append((cx + r * math.cos(th), cy + r * math.sin(th)))
    draw.polygon(pts, fill=fill)
    lit = [pts[0], pts[1], ((pts[0][0] + pts[1][0] + 2 * cx) / 4, (pts[0][1] + pts[1][1] + 2 * cy) / 4)]
    draw.polygon(lit, fill=edge)


def debris_layer(frame, origin, i, H, W, ss=3, skip=None):
    """Dark metal shards where the original has small dark/grey pieces."""
    img = Image.new('RGBA', (W * ss, H * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for c in components(frame):
        if skip and skip(c):
            continue
        if c['hot'] or c['area'] > 120:
            continue
        ang = math.atan2(c['cy'] - origin[1], c['cx'] - origin[0])
        size = math.sqrt(c['area']) * S * 0.95 * ss
        seed = int((ang + 4) * 10) * 7 + int(math.sqrt(c['area']))
        x, y = (c['cx'] + 0.5) * S * ss, (c['cy'] + 0.5) * S * ss
        g = int(70 + 60 * c['y'])
        shard(d, x, y, size, ang + i * 0.7, seed, (g, g - 6, g - 12, 255), (200, 120, 70, 255))
    img = img.resize((W, H), Image.LANCZOS)
    arr = np.asarray(img).astype(np.float32) / 255
    return arr[..., :3], arr[..., 3]


def sparks_layer(frame, origin, H, W, ss=2):
    """Glowing sparks with a streak pointing away from the blast centre."""
    acc = np.zeros((H, W), np.float32)
    core = np.zeros((H, W), np.float32)
    img = Image.new('L', (W * ss, H * ss), 0)
    imc = Image.new('L', (W * ss, H * ss), 0)
    d, dc = ImageDraw.Draw(img), ImageDraw.Draw(imc)
    for c in components(frame):
        if not c['hot'] or c['area'] > 200:
            continue
        dx, dy = c['cx'] - origin[0], c['cy'] - origin[1]
        dist = math.hypot(dx, dy) + 1e-3
        ux, uy = dx / dist, dy / dist
        r = (0.7 + math.sqrt(c['area']) * 0.5) * S * ss
        L = (3 + dist * 0.08 + math.sqrt(c['area'])) * S * ss
        x, y = (c['cx'] + 0.5) * S * ss, (c['cy'] + 0.5) * S * ss
        tail = (x - ux * L, y - uy * L)
        for k in range(6):   # tapered streak
            f = k / 6
            rr = r * (1 - f * 0.8)
            px, py = x + (tail[0] - x) * f, y + (tail[1] - y) * f
            d.ellipse([px - rr, py - rr, px + rr, py + rr], fill=int(255 * (1 - f) ** 1.5))
        if c['area'] > 3:
            dc.ellipse([x - r * 0.4, y - r * 0.4, x + r * 0.4, y + r * 0.4], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(S * ss * 0.5)).resize((W, H), Image.LANCZOS)
    imc = imc.filter(ImageFilter.GaussianBlur(S * ss * 0.15)).resize((W, H), Image.LANCZOS)
    acc = np.asarray(img).astype(np.float32) / 255
    core = np.asarray(imc).astype(np.float32) / 255
    glow = ndi.gaussian_filter(acc, S * 1.6)
    heat = np.clip(acc * 0.6 + core * 0.6, 0, 1)
    col = ramp(0.3 + 0.7 * heat, FIRE)
    alpha = np.clip(np.maximum(acc, core) + glow * 0.55, 0, 1)
    return col, alpha


# ---------------------------------------------------------------- 1043: flash, 1044: plasma ball, 1045: sparks
def flash(reg=1043):
    _, _, w, h, _, origin = SCENES[reg]
    H, W = h * S, w * S
    f = frames(reg)[0]
    a = f[..., 3] > 0.05
    big = max(components(f), key=lambda c: c['area'])
    cy, cx = big['cy'], big['cx']
    radius = math.sqrt(big['area'] / math.pi)
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    rr = np.hypot(ys / S - cy, xs / S - cx) / radius
    ang = np.arctan2(ys / S - cy, xs / S - cx)
    rng = np.random.default_rng(11)
    rays = sum(np.sin(ang * k + rng.uniform(0, 6.3)) / k ** 0.6 for k in (5, 8, 13, 21, 34)) / 2.2
    rays = (0.5 + 0.5 * rays) * np.clip(rr * 1.4, 0, 1)
    r2 = rr * (1 - 0.1 * (rays - 0.5))
    edge = np.clip((1.04 - r2) / 0.14, 0, 1) ** 1.2
    alpha = np.clip(edge, 0, 1)
    fibre = sum(np.sin(ang * k + rng.uniform(0, 6.3) + rr * rng.uniform(-2, 2)) for k in (47, 71, 109, 163, 211)) / 5
    heat = np.clip(1.0 - r2 ** 2.2 * 0.8 + 0.07 * fibre * np.clip(rr, 0, 1), 0, 1)
    col = ramp(0.36 + 0.64 * heat, FIRE)
    drgb, da = debris_layer(f, (cx, cy), 0, H, W, skip=lambda c: c['area'] > 400)
    col, alpha = over(col, alpha, drgb, da)
    return [(col, alpha)]


def plasma(reg=1044, seed=7):
    _, n, w, h, _, origin = SCENES[reg]
    H, W = h * S, w * S
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    starts = start_ticks(reg)
    out = []
    for i, f in enumerate(frames(reg)):
        t = float(starts[i])
        big = max(components(f), key=lambda c: c['area'])
        cy, cx = big['cy'], big['cx']
        radius = math.sqrt(big['area'] / math.pi)
        rr = np.hypot(ys / S - cy, xs / S - cx) / radius
        turb = fbm(seed, ys, xs, t, H, W, 14 * S, 8, octaves=5)
        r2 = rr * (1 + 0.07 * (turb - 0.5))
        body = np.clip((1.0 - r2) / 0.06, 0, 1)
        limb = np.clip(r2, 0, 1) ** 3
        heat = np.clip(1.0 - 0.38 * limb + 0.12 * (turb - 0.5), 0, 1)
        col = ramp(0.62 + 0.38 * heat, FIRE)
        corona = np.clip((1.18 - rr) / 0.18, 0, 1) ** 2 * 0.55
        ccol = ramp(np.full(rr.shape, 0.7, np.float32), FIRE)
        col, alpha = over(ccol, corona, col, body)
        drgb, da = debris_layer(f, (cx, cy), i, H, W, skip=lambda c: c['area'] > 400)
        col, alpha = over(col, alpha, drgb, da)
        out.append((col, alpha))
    return out


def sparks(reg=1045):
    _, n, w, h, _, origin = SCENES[reg]
    H, W = h * S, w * S
    out = []
    for i, f in enumerate(frames(reg)):
        col, alpha = sparks_layer(f, origin, H, W)
        drgb, da = debris_layer(f, origin, i, H, W)
        col, alpha = over(col, alpha, drgb, da)
        out.append((col, alpha))
    return out


MAKERS = {1042: lambda: fireball(), 1043: lambda: flash(), 1044: lambda: plasma(), 1045: lambda: sparks()}


def name(reg: int, frame: int) -> str:
    s, a, p = SCENES[reg][0]
    return f'{s}-{a}-{p}-f{frame}.png'


def render() -> None:
    (OUT / 'hd').mkdir(parents=True, exist_ok=True)
    for reg in REGISTRIES:
        for i, (rgb, a) in enumerate(MAKERS[reg]()):
            save(rgb, a, OUT / 'hd' / name(reg, i))
        print(reg, SCENES[reg][1], 'frames')
    review()


def review() -> None:
    cell, rows = 200, []
    for reg in REGISTRIES:
        n = SCENES[reg][1]
        for row in range(2):
            strip = Image.new('RGBA', (cell * n + 50, cell), (52, 70, 98, 255))
            ImageDraw.Draw(strip).text((6, 8), f"{reg}\n{'orig' if row == 0 else 'HD'}", fill='white')
            for i in range(n):
                image = (Image.fromarray((frames(reg)[i] * 255).astype(np.uint8), 'RGBA') if row == 0
                         else Image.open(OUT / 'hd' / name(reg, i)))
                k = (cell - 8) / max(image.size)
                image = image.resize((round(image.width * k), round(image.height * k)),
                                     Image.Resampling.NEAREST if row == 0 else Image.Resampling.LANCZOS)
                strip.alpha_composite(image, (50 + i * cell + (cell - image.width) // 2, (cell - image.height) // 2))
            rows.append(strip)
    sheet = Image.new('RGB', (max(r.width for r in rows), sum(r.height + 4 for r in rows)), (20, 20, 24))
    y = 0
    for strip in rows:
        sheet.paste(strip.convert('RGB'), (0, y))
        y += strip.height + 4
    sheet.save(OUT / 'review.png')


def pack(into: Path, bind: bool) -> None:
    """A new battle_sprites pack: the bound one hard-linked, plus the explosion frames."""
    into = into.absolute()                       # not resolved: assets/ may be a link into another checkout
    current = ROOT / json.loads(MANIFEST.read_text())['battle_sprites']['path']
    into.mkdir(parents=True, exist_ok=False)
    index = json.loads((current / 'battle-sprites.json').read_text())
    rows = []
    for reg in REGISTRIES:
        (s, a, p), scene, _, _, origin = scene_of(reg)
        for frame, parts in enumerate(scene.frames):
            if not parts:
                continue
            box = (min(q.x for q in parts), min(q.y for q in parts), max(q.x + q.w for q in parts), max(q.y + q.h for q in parts))
            master = Image.open(OUT / 'hd' / name(reg, frame))
            x, y = (box[0] + origin[0]) * S, (box[1] + origin[1]) * S
            image = pack_image(master.crop((x, y, x + (box[2] - box[0]) * S, y + (box[3] - box[1]) * S)), S)
            image.save(into / name(reg, frame), optimize=True)
            rows.append({'scene': s, 'atlas': a, 'palette': p, 'frame': frame, 'file': name(reg, frame),
                         'sha256': hashlib.sha256((into / name(reg, frame)).read_bytes()).hexdigest(),
                         'width': image.width, 'height': image.height, 'method': 'procedural', 'unit': f'explosion {reg}'})
    ours = {r['file'] for r in rows}
    for path in current.iterdir():
        if path.is_file() and path.name != 'battle-sprites.json' and path.name not in ours:
            os.link(path, into / path.name)
    index['images'] = [r for r in index['images'] if r['file'] not in ours] + rows
    (into / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(rows), 'explosion frames added to', current.name, '->', into.name)
    if bind:
        manifest = json.loads(MANIFEST.read_text())
        manifest['battle_sprites'] = {'path': str(into.relative_to(ROOT)),
                                      'manifest_sha256': hashlib.sha256((into / 'battle-sprites.json').read_bytes()).hexdigest()}
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('render', 'review', 'pack'))
    parser.add_argument('--into', type=Path, help='pack: the new pack folder')
    parser.add_argument('--bind', action='store_true', help='pack: point the art manifest at it')
    args = parser.parse_args()
    if args.step == 'render':
        render()
    elif args.step == 'review':
        review()
    else:
        if args.into is None:
            parser.error('pack needs --into')
        pack(args.into, args.bind)


if __name__ == '__main__':
    main()
