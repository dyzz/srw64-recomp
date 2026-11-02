"""HD beam frames by intensity and ramp, the way Super Robot Wars Z colours its grey effect
textures (docs/design/battle-animation-rendering.md, the beams).

Each original frame becomes an intensity map: a pixel's value is its colour's brightness rank
among the colours the scene draws with that palette. The map is smoothed (dithered blends
inside the shape averaged out), enlarged to the pack's scale and run through a ramp built from
the same colours: transparent, the palette's most saturated bright colour, white at the core.
Inside, flow lines run along the beam: puffs of thin horizontal streaks, each bright at its
head and fading along its tail (tools/hd_ai/beam_elements.py, drawn after Z's, none of Z's art), laid
across the beam from its centre line to its edges so the lines bend with its bulges, and moved
on from frame to frame. A soft halo of the rim colour goes round it, and Z's flashes
(beam_elements: a glow ball and a star flash and a burst at the muzzle as the beam leaves, a
pulsing glow ball, a star flash and sparks where it hits) are drawn on: these reach past the
frame's box, so each picture is the box grown by a margin a side, given in its row as `bleed`
(original pixels: left, top, right, bottom, as the picture faces), and the host grows the
box it draws on by as much (native_sprite.cpp). The beam's own shape, size and timing are the
original's; another palette gives another colour.

    .venv/bin/python tools/hd_ai/beam_ramp_hd.py --into assets/hd-ai/cutins/pack-vN [--bind]
    .venv/bin/python tools/hd_ai/beam_ramp_hd.py --lines OUT.png      # the flow-line texture alone

Adds a picture per (scene, atlas, palette, frame) of the BEAMS scenes, in every palette the
battle_scenes table pairs them with, and of their FOLLOW_ONS in the same palettes, to a copy of the bound battle sprites pack (hard links
for the rest, as cutin_hd.py extend does), replacing rows of the same name.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import decode_atlas, parse_scene, read_triplets, render_scene  # noqa: E402
from tools.hd_ai import beam_elements  # noqa: E402
from tools.hd_ai.cutin_hd import MANIFEST, runtime_palette  # noqa: E402
from tools.hd_ai.unit_extra_derive import PACK_SCALE  # noqa: E402

# (scene, atlas): the beam rifle family (a thin beam whose tail breaks into dots) and the mega
# particle cannon family (a thick wavy beam with a muzzle ring).
BEAMS = ((3558, 3023), (3560, 3023), (3624, 3048), (3626, 3048))
# The scenes the beam's actor steps on to where it hits (the beam routine moves it to the next
# registration's scene at run time): not paired with a palette in battle_scenes, drawn in the
# palette of the scene it came from (seen in scene-sprites.jsonl in the battle viewer).
FOLLOW_ONS = {(3559, 3023): (3558, 3023), (3625, 3048): (3624, 3048)}
# Where the flashes go: the launch scenes' muzzle is the scene's origin, the beam leaving
# toward -x; in the others the beam's head stops at the target, the frame's left end.
LAUNCH = {3558, 3624}
HALO = 8                  # original pixels round the shape the halo needs
S = PACK_SCALE
WHITE_FROM = .45          # where the ramp starts turning white: lower is a wider core


def flow_lines(rows: int = 96, width: int = 640, seed: int = 64) -> np.ndarray:
    """Puffs of flow lines (beam_elements.flow, drawn after Z's) strung along x so the texture
    tiles: each puff a bunch of hairline streaks bright near its head and fading behind it, the
    puffs overlapping at random heights and spacings. A soft glow along the middle rows, as
    Z's lines bunch toward the core. 0..1, rows x width."""
    rng = np.random.default_rng(seed)
    out = np.zeros((rows, width * 2), np.float32)
    x = 0
    while x < width:
        h = int(rows * rng.uniform(.7, 1)); w = int(width * rng.uniform(.2, .35))
        puff = beam_elements.flow(h, w, seed=int(rng.integers(1 << 30)), streaks=int(rng.integers(40, 90)))
        y = int(rng.integers(0, rows - h + 1))
        out[y:y + h, x:x + w] = np.maximum(out[y:y + h, x:x + w], puff * rng.uniform(.7, 1))
        x += int(w * rng.uniform(.35, .6))
    out = np.maximum(out[:, :width], out[:, width:])                  # wrap the overhang round
    across = 1 - np.abs(np.linspace(-1, 1, rows))
    out = out + .3 * (across ** 1.5)[:, None]
    return out / max(out.max(), 1e-6)


LINES = flow_lines()


def luminance(c: np.ndarray) -> np.ndarray:
    return c[..., 0] * .299 + c[..., 1] * .587 + c[..., 2] * .114


def ramp(colours: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """256-entry colour and alpha ramp from a scene's colours (dark to bright), and the rim colour."""
    v = np.linspace(0, 1, 256)
    saturation = colours.max(1) - colours.min(1)
    rim = colours[int(np.argmax(saturation * (colours.max(1) > .5)))]
    position = np.linspace(0, 1, len(colours))
    rgb = np.stack([np.interp(v, position, colours[:, k]) for k in range(3)], 1)
    rgb = np.where((v < WHITE_FROM)[:, None], rim[None] * (.75 + .25 * np.clip(v / WHITE_FROM, 0, 1))[:, None], rgb)
    white = np.clip((v - WHITE_FROM) / (1 - WHITE_FROM), 0, 1) ** .9
    rgb = rgb * (1 - white[:, None]) + white[:, None]
    alpha = np.clip((v - .02) / .33, 0, 1)
    return rgb.astype(np.float32), (alpha * alpha * (3 - 2 * alpha)).astype(np.float32), rim.astype(np.float32)


def enlarge(field: np.ndarray, sigma: float) -> np.ndarray:
    h, w = field.shape
    big = np.asarray(Image.fromarray(field.astype(np.float32)).resize((w * S, h * S), Image.BICUBIC))
    return ndimage.gaussian_filter(big, sigma * S)


def lines_along(solid: np.ndarray, shift: float) -> np.ndarray:
    """The flow lines laid across the beam at the pack's scale: v from the centre line (-1)
    to the edges (1) of the shape in each column, u along x moved on by `shift` pixels."""
    mask = enlarge(solid.astype(np.float32), .4) > .5
    h, w = mask.shape
    columns = mask.any(0)
    top = np.where(columns, mask.argmax(0), 0)
    bottom = np.where(columns, h - 1 - mask[::-1].argmax(0), 0)
    centre = ndimage.uniform_filter1d(((top + bottom) / 2).astype(np.float32), 9 * S)
    half = ndimage.uniform_filter1d(np.maximum((bottom - top) / 2, 1).astype(np.float32), 9 * S)
    v = (np.arange(h)[:, None] - centre[None]) / np.maximum(half[None], 1)
    rows, width = LINES.shape
    vy = np.clip((v + 1) / 2, 0, 1) * (rows - 1)
    u = (np.arange(w)[None] * (width / (w * 1.0 + width)) * 2 + shift) % width
    return ndimage.map_coordinates(LINES, [vy, np.broadcast_to(u, vy.shape)], order=1, mode='grid-wrap')


def flash_field(h: int, w: int, flashes: list[dict]) -> np.ndarray:
    """The flashes at the pack's scale: each {kind, x, y (original px in the picture), r, strength}."""
    out = np.zeros((h, w), np.float32)
    def put(element: np.ndarray, cx: float, cy: float, strength: float) -> None:
        eh, ew = element.shape
        x0, y0 = int(round(cx - ew / 2)), int(round(cy - eh / 2))
        ys, xs = slice(max(y0, 0), min(y0 + eh, h)), slice(max(x0, 0), min(x0 + ew, w))
        if ys.start >= ys.stop or xs.start >= xs.stop: return
        cut = element[ys.start - y0:ys.stop - y0, xs.start - x0:xs.stop - x0] * strength
        out[ys, xs] = np.maximum(out[ys, xs], cut)
    for f in flashes:
        cx, cy, r, k = f['x'] * S, f['y'] * S, f['r'] * S, f['strength']
        if f['kind'] == 'glow':
            put(beam_elements.glow(int(2 * r) | 1), cx, cy, k)
        elif f['kind'] == 'star':
            put(beam_elements.star(int(1.3 * r) | 1, seed=f.get('seed', 0)), cx, cy, k)
        elif f['kind'] == 'burst':                  # fanning out toward -x from (x, y)
            b = beam_elements.burst(int(1.6 * r) | 1, int(2.6 * r), seed=f.get('seed', 0))[:, ::-1]
            put(b, cx - b.shape[1] / 2, cy, k)
        elif f['kind'] == 'sparks':
            put(beam_elements.sparks(int(3.2 * r), int(3.2 * r), seed=f.get('seed', 0), count=10, unit=S * .8), cx, cy, k)
    return out


def beam_frame(frame: np.ndarray, rank: dict, table: tuple, shift: float, flashes: list[dict] = ()) -> Image.Image:
    """One original frame (RGBA float 0-1, cut to its box and grown by its margins) as the HD
    picture (straight alpha)."""
    rgb_ramp, alpha_ramp, rim = table
    h, w = frame.shape[:2]
    intensity = np.zeros((h, w), np.float32)
    solid = frame[..., 3] > 0
    intensity[solid] = [rank[tuple(c)] for c in np.round(frame[solid][:, :3] * 255).astype(int)]
    inside = ndimage.minimum_filter(solid, 3)
    smooth = np.where(inside, ndimage.uniform_filter(intensity, 3), intensity).astype(np.float32)
    body = np.clip(enlarge(smooth, .55), 0, 1)
    if (body > .05).any():
        body = body / max(np.percentile(body[body > .05], 99), .5)
    body = np.clip(body * (.72 + .45 * lines_along(solid, shift)), 0, 1)
    # sparks in the glow just outside the body, as Z scatters them round its beams
    glow = np.clip(enlarge(smooth, 2.2) * 1.6, 0, 1) * (1 - np.clip(body * 3, 0, 1))
    seed = int(shift * 1000) % (1 << 30)
    body = np.maximum(body, beam_elements.sparks(h * S, w * S, seed=seed, count=max(2, int(solid.sum() / 60)), unit=S * .7) * glow * .8)
    if flashes:
        body = 1 - (1 - body) * (1 - flash_field(h * S, w * S, list(flashes)))
    k = np.clip((body * 255).astype(int), 0, 255)
    a_body, rgb_body = alpha_ramp[k], rgb_ramp[k]
    # the halo fades out before the picture's edge
    yy, xx = np.mgrid[0:h * S, 0:w * S]
    edge = np.minimum.reduce([xx, yy, w * S - 1 - xx, h * S - 1 - yy]).astype(np.float32) / (3 * S)
    a_halo = np.clip(enlarge(smooth, 3.2) * 1.4, 0, 1) * .55 * np.clip(edge, 0, 1)
    a = a_body + a_halo * (1 - a_body)
    rgb = (rgb_body * a_body[..., None] + rim * (a_halo * (1 - a_body))[..., None]) / np.maximum(a, 1e-6)[..., None]
    rgb = np.where((a > 0)[..., None], rgb, rim)      # transparent pixels keep the rim colour: no dark fringe when filtered
    return Image.fromarray((np.dstack([rgb, a]) * 255 + .5).clip(0, 255).astype(np.uint8), 'RGBA')


def flashes_for(scene_id: int, scene, frame: int, box: tuple, alpha: np.ndarray, origin: tuple) -> list[dict]:
    """Z's flashes on one frame, in frame coordinates: at the muzzle while a launch scene's beam
    leaves it, at the head where the other scenes' beam meets the target. `alpha` is the scene's
    rendered frame (bounds-sized), `origin` the scene bounds' top left."""
    ticks, t = {}, 0
    for f, n in scene.steps:
        ticks.setdefault(f, t); t += n
    def half_height(xa: float, xb: float) -> float:
        """Half the beam's typical height over columns xa..xb (the median column: a splash of
        dots or a ring does not count)."""
        cols = alpha[:, max(int(xa - origin[0]), 0):max(int(xb - origin[0]), 0)]
        heights = [np.ptp(np.nonzero(c)[0]) + 1 for c in cols.T if c.any()]
        return float(np.median(heights)) / 2 if heights else 2
    out = []
    if scene_id in LAUNCH:
        if box[2] < -2:
            return out                              # the beam has left the muzzle
        flash = float(np.exp(-ticks[frame] / 4))
        hh = half_height(-24, -8) if box[0] < -24 else half_height(box[0], box[2])
        out.append({'kind': 'glow', 'x': 0, 'y': 0, 'r': 1.3 * hh + 4, 'strength': .6 + .4 * flash})
        if flash > .2:
            out.append({'kind': 'star', 'x': 0, 'y': 0, 'r': 1.4 * hh + 5, 'strength': flash, 'seed': frame})
            out.append({'kind': 'burst', 'x': 0, 'y': 0, 'r': 1.4 * hh + 5, 'strength': flash, 'seed': frame})
        return out
    lefts = [min(q.x for q in scene.frames[f]) for f, _ in scene.steps if f != 0xFF and scene.frames[f]]
    reach, span = min(lefts), max(q.x + q.w for f, _ in scene.steps if f != 0xFF for q in scene.frames[f]) - min(lefts)
    hits = [f for f, _ in scene.steps if f != 0xFF and scene.frames[f] and min(q.x for q in scene.frames[f]) <= reach + .06 * span]
    if frame not in hits:
        return out
    since = ticks[frame] - min(ticks[f] for f in hits)
    x, y = box[0], (box[1] + box[3]) / 2
    hh = half_height(x + 12, x + 40)
    out.append({'kind': 'glow', 'x': x, 'y': y, 'r': 1.5 * hh + 5, 'strength': .75 + .25 * float(np.cos(since * 1.3))})
    out.append({'kind': 'sparks', 'x': x, 'y': y, 'r': 1.5 * hh + 5, 'strength': .9, 'seed': frame})
    if since < 5:
        out.append({'kind': 'star', 'x': x, 'y': y, 'r': 1.6 * hh + 5, 'strength': float(np.exp(-since / 3)), 'seed': frame})
    return out


def scene_rows(table: ResourceTable, triplets) -> list[tuple[dict, Image.Image]]:
    out = []
    for s, a, p in triplets:
        scene = parse_scene(table.extract(s)[0])
        atlas, _ = decode_atlas(table.extract(a)[0], runtime_palette(p, table.extract(p)[0]))
        images, _ = render_scene(scene, atlas)
        x0, y0 = scene.bounds()[:2]
        frames = sorted({f for f, _ in scene.steps if f != 0xFF and scene.frames[f]})
        colours = {}
        for f in frames:
            px = np.asarray(images[f].convert('RGBA'))
            for c in np.unique(px[px[..., 3] > 0][:, :3], axis=0):
                colours[tuple(int(x) for x in c)] = 1
        ordered = np.array(sorted(colours, key=lambda c: c[0] * .299 + c[1] * .587 + c[2] * .114), np.float32) / 255
        rank = {tuple(int(round(x * 255)) for x in c): (i + 1) / len(ordered) for i, c in enumerate(ordered)}
        ramps = ramp(ordered)
        for f in frames:
            parts = scene.frames[f]
            box = (min(q.x for q in parts), min(q.y for q in parts), max(q.x + q.w for q in parts), max(q.y + q.h for q in parts))
            whole = np.asarray(images[f].convert('RGBA')).astype(np.float32) / 255
            flashes = flashes_for(s, scene, f, box, whole[..., 3] > 0, (x0, y0))
            # the margins: the halo's, or as far as a flash reaches past the box
            grow = [HALO] * 4
            for fl in flashes:
                reach = fl['r'] * (2.6 if fl['kind'] == 'burst' else 1.6 if fl['kind'] == 'sparks' else 1) + 2
                grow[0] = max(grow[0], int(np.ceil(box[0] - (fl['x'] - reach))))
                grow[1] = max(grow[1], int(np.ceil(box[1] - (fl['y'] - reach))))
                grow[2] = max(grow[2], int(np.ceil(fl['x'] + reach - box[2])))
                grow[3] = max(grow[3], int(np.ceil(fl['y'] + reach - box[3])))
            canvas = (box[0] - grow[0], box[1] - grow[1], box[2] + grow[2], box[3] + grow[3])
            crop = np.zeros((canvas[3] - canvas[1], canvas[2] - canvas[0], 4), np.float32)
            sy0, sx0 = max(canvas[1] - y0, 0), max(canvas[0] - x0, 0)
            sy1, sx1 = min(canvas[3] - y0, whole.shape[0]), min(canvas[2] - x0, whole.shape[1])
            crop[sy0 - (canvas[1] - y0):sy1 - (canvas[1] - y0), sx0 - (canvas[0] - x0):sx1 - (canvas[0] - x0)] = whole[sy0:sy1, sx0:sx1]
            local = [fl | {'x': fl['x'] - canvas[0], 'y': fl['y'] - canvas[1]} for fl in flashes]
            # the lines run toward the beam's head (it flies to -x) a little each frame
            out.append(({'scene': s, 'atlas': a, 'palette': p, 'frame': f, 'bleed': grow},
                        beam_frame(crop, rank, ramps, 37.0 * f, local)))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--into', type=Path)
    ap.add_argument('--bind', action='store_true')
    ap.add_argument('--lines', type=Path, help='write the flow-line texture to this PNG and stop')
    args = ap.parse_args()
    if args.lines:
        Image.fromarray((LINES * 255).astype(np.uint8)).save(args.lines)
        return
    if not args.into:
        ap.error('--into is required')
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    triplets = {t for t in read_triplets(rom, 'battle_scenes') if t[:2] in BEAMS}
    triplets = sorted(triplets | {(s, a, p) for (s, a), base in FOLLOW_ONS.items() for t in triplets if t[:2] == base for p in [t[2]]})
    into = args.into.absolute()
    current = ROOT / json.loads(MANIFEST.read_text())['battle_sprites']['path']
    into.mkdir(parents=True, exist_ok=False)
    for path in current.iterdir():
        if path.is_file() and path.name != 'battle-sprites.json':
            os.link(path, into / path.name)
    index = json.loads((current / 'battle-sprites.json').read_text())
    added = []
    for row, image in scene_rows(table, triplets):
        name = f"{row['scene']}-{row['atlas']}-{row['palette']}-f{row['frame']}.png"
        (into / name).unlink(missing_ok=True)           # a hard link: never write through to the bound pack
        image.save(into / name, optimize=True)
        added.append(row | {'file': name, 'sha256': hashlib.sha256((into / name).read_bytes()).hexdigest(),
                            'width': image.width, 'height': image.height, 'method': 'ramp', 'unit': f"beam {row['scene']}"})
    files = {r['file'] for r in added}
    index['images'] = [r for r in index['images'] if r['file'] not in files] + added
    (into / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(added), 'beam frames,', len(triplets), 'triplets ->', into.name)
    if args.bind:
        manifest = json.loads(MANIFEST.read_text())
        manifest['battle_sprites'] = {'path': str(into.relative_to(ROOT)),
                                      'manifest_sha256': hashlib.sha256((into / 'battle-sprites.json').read_bytes()).hexdigest()}
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT))


if __name__ == '__main__':
    main()
