"""HD beam frames by intensity and ramp, the way Super Robot Wars Z colours its grey effect
textures (docs/design/battle-animation-rendering.md, the beams).

Each original frame becomes an intensity map: a pixel's value is its colour's brightness rank
among the colours the scene draws with that palette. The map is smoothed (dithered blends
inside the shape averaged out), enlarged to the pack's scale and run through a ramp built from
the same colours: transparent, the palette's most saturated bright colour, white at the core.
Inside, flow lines run along the beam: puffs of thin horizontal streaks, each bright at its
head and fading along its tail (tools/hd_ai/beam_elements.py, drawn after Z's, none of Z's art), laid
across the beam from its centre line to its edges so the lines bend with its bulges, and moved
on from frame to frame. A soft halo of the rim colour stays inside the frame's box (the host
draws the picture over the box of the frame's parts). The shape, size and timing are the
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
from tools.hd_ai.cutin_hd import MANIFEST, bind_pack, runtime_palette, share_files  # noqa: E402
from tools.hd_ai.unit_extra_derive import PACK_SCALE  # noqa: E402

# (scene, atlas): the generic beams (docs/design/battle-animation-rendering.md, the beam
# catalogue): the beam rifle family (a thin beam whose tail breaks into dots, 3558, and the green
# 3649), the mega particle cannons (a thick wavy beam with a muzzle ring, 3624, 3627 the
# thickest), the smooth beams 3650 and 3587, the funnels' thin beams 3612 and 3661, the beam
# Gatling's bolts 4809 (two atlases), the twisted laser 4375 and the zigzag lightning 3709.
BEAMS = ((3558, 3023), (3560, 3023), (3624, 3048), (3626, 3048), (3627, 3049), (3629, 3049),
         (3650, 3052), (3652, 3052), (3587, 3033), (3589, 3033), (3649, 3051), (3612, 3045),
         (3661, 3056), (4809, 3027), (4809, 4807), (4375, 4371), (3709, 3079))
# The scenes the beam's actor steps on to where it hits (the beam routine moves it to the next
# registration's scene at run time): not paired with a palette in battle_scenes, drawn in the
# palette of the scene it came from (seen in scene-sprites.jsonl in the battle viewer).
FOLLOW_ONS = {(3559, 3023): (3558, 3023), (3625, 3048): (3624, 3048), (3628, 3049): (3627, 3049),
              (3651, 3052): (3650, 3052), (3588, 3033): (3587, 3033),
              (3613, 3045): (3612, 3045), (3614, 3045): (3612, 3045),
              (3662, 3056): (3661, 3056), (3663, 3056): (3661, 3056),
              (4810, 3027): (4809, 3027), (4811, 3027): (4809, 3027),
              (4810, 4807): (4809, 4807), (4811, 4807): (4809, 4807),
              (4376, 4371): (4375, 4371), (4377, 4371): (4375, 4371),
              (3710, 3079): (3709, 3079), (3711, 3079): (3709, 3079), (3712, 3079): (3709, 3079)}
S = PACK_SCALE
WHITE_FROM = .38          # where the ramp starts turning white: lower is a wider core


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


def silhouette(mask: np.ndarray) -> np.ndarray:
    """The original shape at the pack's scale, its stair steps smoothed into curves through
    the same edges: 0..1 coverage, anti-aliased over about a pixel of the picture."""
    soft = enlarge(mask.astype(np.float32), .5)
    # a line one pixel thin blurs below half: measured against its own peak it stays
    soft = soft / np.maximum(ndimage.maximum_filter(soft, 2 * S + 1), .5)
    # and in a dithered spray nothing reaches more than a third of a pixel past the original's
    # pixels, so its gaps stay open (the solid body's edge stays a smooth curve)
    dense = ndimage.binary_dilation(mask & (ndimage.uniform_filter(mask.astype(np.float32), 3) >= .66))
    near = ndimage.binary_dilation(np.kron(mask, np.ones((S, S), bool)), iterations=max(S // 3, 1)) | np.kron(dense, np.ones((S, S), bool))
    soft = np.where(near, soft, 0)
    return np.clip((soft - .5) * 2.5 + .5, 0, 1)


def dots(single: np.ndarray, value: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Specks of original pixels (the beam rifle's tail breaking up) as round sparks in their own
    cells, after Z's: coverage and intensity, a white centre fading to the dot's colour."""
    h, w = single.shape
    yy, xx = np.mgrid[0:h * S, 0:w * S].astype(np.float32)
    r = np.hypot((yy % S) - (S - 1) / 2, (xx % S) - (S - 1) / 2) / (S / 2)
    cell = np.kron(single, np.ones((S, S), bool))
    cover = np.where(cell, np.clip((1.05 - r) * 3, 0, 1), 0)
    inten = np.where(cell, np.clip(np.kron(value, np.ones((S, S))) + .6 * np.clip(1 - r * 1.6, 0, 1), 0, 1), 0)
    return cover.astype(np.float32), inten.astype(np.float32)


def body_of(frame: np.ndarray, rank: dict) -> tuple:
    """An original frame (RGBA float 0-1, cut to its box) split into its body and specks: the
    intensity per pixel, the speck and body masks, the body's coverage and its intensity at the
    pack's scale."""
    h, w = frame.shape[:2]
    intensity = np.zeros((h, w), np.float32)
    solid = frame[..., 3] > 0
    intensity[solid] = [rank[tuple(c)] for c in np.round(frame[solid][:, :3] * 255).astype(int)]
    # specks of up to three pixels become dots; the rest is the beam's body
    labels, count = ndimage.label(solid, np.ones((3, 3), bool))
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    single = solid & (sizes[labels] <= 3)
    body_mask = solid & ~single
    cover = silhouette(body_mask)
    inside = ndimage.minimum_filter(body_mask, 3)
    smooth = np.where(inside, ndimage.uniform_filter(intensity, 3), intensity).astype(np.float32)
    # the value inside the body, filled out to its edge so the edge keeps the body's colour
    near = ndimage.distance_transform_edt(~body_mask, return_distances=False, return_indices=True)
    filled = smooth[near[0], near[1]] if body_mask.any() else smooth
    return intensity, single, body_mask, cover, np.clip(enlarge(filled, .45), 0, 1)


def frame_peak(frame: np.ndarray, rank: dict) -> float:
    _, _, body_mask, cover, body = body_of(frame, rank)
    return float(np.percentile(body[cover > .5], 99)) if body_mask.any() and (cover > .5).any() else 0.0


def beam_frame(frame: np.ndarray, rank: dict, table: tuple, shift: float, peak: float) -> Image.Image:
    """One original frame (RGBA float 0-1, cut to its box) as the HD picture (straight alpha),
    covering exactly the original's pixels: no glow outside them. `peak` is the brightest body
    of the scene's frames (frame_peak): a frame where the original fades stays as much darker."""
    rgb_ramp, _, rim = table
    intensity, single, body_mask, cover, body = body_of(frame, rank)
    body = body / max(peak, .5)
    # Z's cross-section (measured on its mega particle cannon): a flat white core over most of
    # the width, then a narrow band to the edge, pale, then the saturated colour, then darker at
    # the very edge. Depth from the edge over the half-thickness of each vertical run of the
    # shape gives where a pixel lies across; the flow lines ripple the band along the beam.
    inside = cover > .5
    depth = ndimage.distance_transform_edt(inside)
    runs, count = ndimage.label(inside, np.array([[0, 1, 0], [0, 1, 0], [0, 1, 0]], bool))
    half = np.r_[1.0, ndimage.maximum(depth, runs, np.arange(1, count + 1))] if count else np.ones(1)
    # smoothed across neighbouring runs, so no seam shows where the beam meets its ring
    weight = ndimage.gaussian_filter(inside.astype(np.float32), 2 * S)
    half = ndimage.gaussian_filter(half[runs] * inside, 2 * S) / np.maximum(weight, 1e-3)
    across = depth / np.maximum(half, 1)
    across = across * (.82 + .36 * lines_along(body_mask, shift))
    profile = np.interp(across, (0, .1, .22, .34, .45), (.18, .32, .55, .85, 1))
    # where the original darkens (a fading tail) it stays darker
    body = np.minimum(profile, np.clip(body * 1.4, 0, 1))
    dot_cover, dot_value = dots(single, intensity)
    a = np.maximum(cover, dot_cover)
    v = np.where(dot_cover > cover, dot_value, body)
    k = np.clip((v * 255).astype(int), 0, 255)
    rgb = np.where((a > 0)[..., None], rgb_ramp[k], rim)   # transparent pixels keep the rim colour: no dark fringe when filtered
    return Image.fromarray((np.dstack([rgb, a]) * 255 + .5).clip(0, 255).astype(np.uint8), 'RGBA')


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
        crops = {}
        for f in frames:
            parts = scene.frames[f]
            box = (min(q.x for q in parts), min(q.y for q in parts), max(q.x + q.w for q in parts), max(q.y + q.h for q in parts))
            crops[f] = np.asarray(images[f].convert('RGBA').crop((box[0] - x0, box[1] - y0, box[2] - x0, box[3] - y0))).astype(np.float32) / 255
        peak = max(frame_peak(c, rank) for c in crops.values())
        for f, crop in crops.items():
            # the lines run toward the beam's head (it flies to -x) a little each frame
            out.append(({'scene': s, 'atlas': a, 'palette': p, 'frame': f}, beam_frame(crop, rank, ramps, 37.0 * f, peak)))
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
    keys = {(r['scene'], r['atlas'], r['palette'], r['frame']) for r in added}   # by frame: rows may share a file (cutin_hd.py share)
    index['images'] = [r for r in index['images'] if (r['scene'], r['atlas'], r['palette'], r['frame']) not in keys] + added
    share_files(into, index)
    print(len(added), 'beam frames,', len(triplets), 'triplets ->', into.name)
    if args.bind:
        bind_pack(into)


if __name__ == '__main__':
    main()
