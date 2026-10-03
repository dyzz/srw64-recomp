"""Derive HD frames of a unit's extra battle images from its HD standing pose.

Many of the extra images a unit's battle atlas holds are the standing pose again: the
same drawing, the same drawing recoloured (a gold super mode, a coloured aura), row-shifted into an after-image, dissolved frame by frame, or with fin funnels flying
off it (docs/design/battle-animation-rendering.md §13). The HD standing pose
(assets/hd-ai/unit-poses/whole-v1, 8x the scene bounds) already exists, so these need no
new drawing: every frame is aligned to the pose in the game's own coordinates (both
canvases are their scenes' bounds) and each original pixel says what to take:

  same as the pose                -> the HD pose there
  same place, other colour        -> the HD pose scaled by the colour ratio
  drawn where the pose has nothing -> the original enlarged (small additions only)
  empty                           -> transparent

After-images are first matched row by row (each source row's horizontal shift that best
lines it up with the pose), and the HD rows are shifted the same way.

    build/esrgan-venv/bin/python tools/hd_ai/unit_extra_derive.py [--only SCENE ...]
    build/esrgan-venv/bin/python tools/hd_ai/unit_extra_derive.py --pack

--pack writes the art pack's set, pack/unit-extras.json (srw64.unit-extra-images.v1) and
one PNG per (scene, atlas, palette, frame): the frame cropped to the parts it draws, the
rectangle the host puts it on (native_sprite.cpp, like the whole unit poses).

Inputs: assets/hd-ai/battle-backgrounds/unit-extra-analysis.json (method per image).
Output: assets/hd-ai/unit-extras-derived/<scene>-<palette>-f<frame>.png (8x the scene
bounds, straight alpha), manifest.json, compare-*.jpg.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import decode_atlas, parse_scene, read_triplets, render_scene  # noqa: E402

K = 8
PACK_SCALE = 5   # pack pixels per source pixel: battle sprites show at 80-129% of a 4x picture
HD = ROOT / 'assets/hd-ai/unit-poses/whole-v1'
OUT = ROOT / 'assets/hd-ai/unit-extras-derived'
ANALYSIS = ROOT / 'assets/hd-ai/battle-backgrounds/unit-extra-analysis.json'
METHODS = ('直接复用站姿', '整体换色或发光', '残影或分解特效', '站姿＋飞出的浮游炮')
FONT = '/System/Library/Fonts/Hiragino Sans GB.ttc'
# Recolours of a pose (same drawing, colour for colour) that the colour-ratio route
# smears or miscolours (dark panel lines turned olive, eyes and gems lost): mapped
# through the colours instead, from the HD pose in the palette named here (None = the
# pose's own). Not for frames that add parts to the pose; those keep derive_frame.
# (scene, palette) -> source palette.
PALETTE_SWAPS = {
    (2237, 2997): 1935, (2237, 2998): 1935,   # マスターガンダム 酔舞 after-images
    (2485, 1910): None,                         # ゴッドガンダムH gold
    (2901, 2994): None,                         # スーパーアースゲイン 爆斧無双断
    (2928, 2993): None,                         # アヴィエスレルム オメガクラッシュ
    (2934, 2992): None,                         # ヴァルディスキューズ スピリッツクラッシュ
    (2862, 2864): None,                         # ヴァイローズ violet
    (2809, 2770): None,                         # スヴァンヒルド
}


class Rom:
    def __init__(self):
        rom = (ROOT / 'rom.z64').read_bytes()
        self.table = ResourceTable(rom)
        self.atlas_of = {s: a for s, a, p in read_triplets(rom, 'battle_scenes') if s}
        self.poses = {}
        for s, a, p in read_triplets(rom, 'unit_poses'):
            if s:
                self.poses.setdefault(s, (s, a, p))

    def frames(self, scene: int, atlas: int, palette: int):
        """[(frame index, RGBA int16 array)] in step order, and the scene bounds."""
        sc = parse_scene(self.table.extract(scene)[0])
        at, _ = decode_atlas(self.table.extract(atlas)[0], self.table.extract(palette)[0])
        images, _ = render_scene(sc, at)
        used = []
        for f, _ in sc.steps:
            if f != 0xFF and f not in used:
                used.append(f)
        return [(f, np.asarray(images[f]).astype(np.float32)) for f in (used or [0])], sc.bounds()


def hd_pose(pose: tuple[int, int, int], palette: int) -> tuple[np.ndarray, tuple[int, int, int]]:
    """The HD pose for this palette if one was made, else the pose's own."""
    s, a, p = pose
    for q in (palette, p):
        path = HD / f'unit-{s}-{a}-{q}.png'
        if path.exists():
            return np.asarray(Image.open(path).convert('RGBA')).astype(np.float32), (s, a, q)
    raise FileNotFoundError(f'no HD pose for {pose}')


def place(img: np.ndarray, bounds, world, k: int = 1) -> np.ndarray:
    out = np.zeros(((world[3] - world[1]) * k, (world[2] - world[0]) * k, 4), np.float32)
    y, x = (bounds[1] - world[1]) * k, (bounds[0] - world[0]) * k
    h, w = min(img.shape[0], out.shape[0] - y), min(img.shape[1], out.shape[1] - x)
    out[y:y + h, x:x + w] = img[:h, :w]
    return out


def row_shifts(target: np.ndarray, pose: np.ndarray, reach: int = 48) -> np.ndarray:
    """Per row, the x shift of the pose that best matches the target (after-images)."""
    shifts = np.zeros(target.shape[0], int)
    for y in range(target.shape[0]):
        t = target[y]
        if not (t[:, 3] > 0).any():
            continue
        best, best_score = 0, -1
        for dx in range(-reach, reach + 1):
            p = np.roll(pose[y], dx, axis=0)
            if dx > 0:
                p[:dx] = 0
            elif dx < 0:
                p[dx:] = 0
            both = (t[:, 3] > 0) & (p[:, 3] > 0)
            score = (both & (np.abs(t[:, :3] - p[:, :3]).max(-1) < 40)).sum() - 0.02 * abs(dx)
            if score > best_score:
                best, best_score = dx, score
        shifts[y] = best
    return shifts


def shift_rows(img: np.ndarray, shifts: np.ndarray, k: int) -> np.ndarray:
    out = np.zeros_like(img)
    for y, dx in enumerate(shifts):
        band = img[y * k:(y + 1) * k]
        out[y * k:(y + 1) * k] = np.roll(band, dx * k, axis=1)
        if dx > 0:
            out[y * k:(y + 1) * k, :dx * k] = 0
        elif dx < 0:
            out[y * k:(y + 1) * k, dx * k:] = 0
    return out


def up(a: np.ndarray, k: int = K) -> np.ndarray:
    return np.repeat(np.repeat(a, k, 0), k, 1)


def smooth(a: np.ndarray, radius: float) -> np.ndarray:
    """Separable Gaussian blur of a float map (any channels), no clipping."""
    taps = np.arange(-int(3 * radius), int(3 * radius) + 1)
    kernel = np.exp(-taps ** 2 / (2 * radius ** 2))
    kernel /= kernel.sum()
    out = a.astype(np.float32)
    for axis in (0, 1):
        padded = np.pad(out, [(len(taps) // 2,) * 2 if i == axis else (0, 0) for i in range(out.ndim)], mode='edge')
        acc = np.zeros_like(out)
        for i, w in enumerate(kernel):
            acc += w * np.take(padded, np.arange(i, i + out.shape[axis]), axis=axis)
        out = acc
    return out


def luma(c: np.ndarray) -> np.ndarray:
    return c[..., 0] * 0.3 + c[..., 1] * 0.59 + c[..., 2] * 0.11


def monochrome(colours: np.ndarray) -> bool:
    """True when the drawn colours share one hue (their chroma directions agree)."""
    chroma = colours - colours.mean(-1, keepdims=True)
    length = np.linalg.norm(chroma, axis=-1)
    strong = length > 20
    if strong.sum() < 50:
        return False
    unit = chroma[strong] / length[strong, None]
    return np.linalg.norm(unit.mean(0)) > 0.85


def brightness_map(source: np.ndarray, target: np.ndarray, hd: np.ndarray) -> np.ndarray:
    bins = 32
    index = np.clip(luma(source) / 256 * bins, 0, bins - 1).astype(int)
    table, count = np.zeros((bins, 3)), np.zeros(bins)
    np.add.at(table, index, target)
    np.add.at(count, index, 1)
    known = count > 0
    centres = np.arange(bins)
    for c in range(3):
        table[:, c] = np.interp(centres, centres[known], table[known, c] / count[known])
    x = np.clip(luma(hd) / 256 * bins - 0.5, 0, bins - 1)
    lo = np.floor(x).astype(int); hi = np.minimum(lo + 1, bins - 1); f = (x - lo)[..., None]
    return table[lo] * (1 - f) + table[hi] * f


def palette_swap(target, t_bounds, pose, p_bounds, hd, k: int = 4):
    """The HD pose recoloured through the colour pairs: where frame and pose both draw,
    each pose colour's most common frame colour is its target; each HD colour is matched
    to its nearest pose colours and takes their targets (plus its own offset from them,
    scaled by the brightness ratio), weighted by closeness. A soft mask cuts
    away what the pose draws and the frame does not."""
    world = (min(t_bounds[0], p_bounds[0]), min(t_bounds[1], p_bounds[1]),
             max(t_bounds[2], p_bounds[2]), max(t_bounds[3], p_bounds[3]))
    T, P, H = place(target, t_bounds, world), place(pose, p_bounds, world), place(hd, p_bounds, world, K)
    both = (T[..., 3] > 0) & (P[..., 3] > 0)
    pairs = np.concatenate([P[both][:, :3], T[both][:, :3]], 1)
    keys, counts = np.unique(pairs, axis=0, return_counts=True)
    order = np.lexsort((-counts, *keys[:, 2::-1].T))      # by pose colour, most common first
    keys = keys[order]
    first = np.ones(len(keys), bool)
    first[1:] = (keys[1:, :3] != keys[:-1, :3]).any(1)
    source, dest = keys[first, :3], keys[first, 3:]
    rgb = H[..., :3].reshape(-1, 3)
    d = ((rgb[:, None, :] - source[None]) ** 2).sum(-1)
    near = np.argsort(d, 1)[:, :k]
    w = 1 / (np.take_along_axis(d, near, 1) + 25.0)
    w /= w.sum(1, keepdims=True)
    gain = ((dest[near].mean(-1, keepdims=True) + 8) / (source[near].mean(-1, keepdims=True) + 8)).clip(0, 2)
    mapped = (w[..., None] * (dest[near] + (rgb[:, None, :] - source[near]) * gain)).sum(1)
    out = H.copy()
    out[..., :3] = np.clip(mapped, 0, 255).reshape(H.shape[:2] + (3,))
    cut = (P[..., 3] > 0) & ~(T[..., 3] > 0)              # pose pixels the frame leaves out
    out[..., 3] = np.minimum(H[..., 3], 255 - smooth(up(cut.astype(np.float32) * 255), 3.0))
    y, x = (t_bounds[1] - world[1]) * K, (t_bounds[0] - world[0]) * K
    h, w_ = (t_bounds[3] - t_bounds[1]) * K, (t_bounds[2] - t_bounds[0]) * K
    stats = {'same': 0, 'recolour': int(both.sum()), 'added': int(((T[..., 3] > 0) & ~both).sum()),
             'drawn': int((T[..., 3] > 0).sum())}
    return out[y:y + h, x:x + w_], stats


def derive_frame(target, t_bounds, pose, p_bounds, hd, after_image: bool):
    world = (min(t_bounds[0], p_bounds[0]), min(t_bounds[1], p_bounds[1]),
             max(t_bounds[2], p_bounds[2]), max(t_bounds[3], p_bounds[3]))
    T, P, H = place(target, t_bounds, world), place(pose, p_bounds, world), place(hd, p_bounds, world, K)
    if after_image:
        shifts = row_shifts(T, P)
        P = shift_rows(P, shifts, 1)
        H = shift_rows(H, shifts, K)
    t_on, p_on = T[..., 3] > 0, P[..., 3] > 0
    colour_gap = np.abs(T[..., :3] - P[..., :3]).max(-1)
    same = t_on & p_on & (colour_gap < 40)
    recolour = t_on & p_on & ~same
    added = t_on & ~p_on
    # colour ratio per original pixel (1 where unchanged), softened so it follows the HD lines
    ratio = np.ones(T.shape[:2] + (3,), np.float32)
    ratio[recolour] = (T[recolour, :3] + 12) / (P[recolour, :3] + 12)
    ratio_hd = np.exp(smooth(np.log(up(ratio)), 2.0))
    out = H.copy()
    out[..., :3] = np.clip(H[..., :3] * ratio_hd, 0, 255)
    if monochrome(T[t_on, :3]) and recolour.sum() > 0.6 * t_on.sum():
        # a one-hue glow (gold super mode, violet aura): the pose's brightness mapped to the
        # target's colours, learned from the original pair, over the whole HD pose
        out[..., :3] = brightness_map(P[recolour, :3], T[recolour, :3], H[..., :3])
    # the target's own silhouette, soft, cuts away what the pose has and the frame lacks
    mask = smooth(up(t_on.astype(np.float32) * 255), 3.0)
    out[..., 3] = np.minimum(H[..., 3], mask)
    # pixels only the frame has: the original enlarged
    if added.any():
        big = np.asarray(Image.fromarray(np.clip(T, 0, 255).astype(np.uint8), 'RGBA').resize(
            (T.shape[1] * K, T.shape[0] * K), Image.BICUBIC)).astype(np.float32)
        take = smooth(up(added.astype(np.float32) * 255), 2.0) / 255
        take = np.maximum(take, (out[..., 3] < 8).astype(np.float32) * (big[..., 3] > 0))
        out[..., :3] = out[..., :3] * (1 - take[..., None]) + big[..., :3] * take[..., None]
        out[..., 3] = np.maximum(out[..., 3], big[..., 3] * take)
    # back to the target's canvas
    y, x = (t_bounds[1] - world[1]) * K, (t_bounds[0] - world[0]) * K
    h, w = (t_bounds[3] - t_bounds[1]) * K, (t_bounds[2] - t_bounds[0]) * K
    stats = {'same': int(same.sum()), 'recolour': int(recolour.sum()), 'added': int(added.sum()), 'drawn': int(t_on.sum())}
    return out[y:y + h, x:x + w], stats


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--only', type=int, nargs='*', default=[])
    parser.add_argument('--pack', action='store_true', help='write pack/ from the derived frames')
    args = parser.parse_args()
    if args.pack:
        return pack()
    rom = Rom()
    rows = [r for r in json.loads(ANALYSIS.read_text()) if r['method'] in METHODS]
    if args.only:
        rows = [r for r in rows if r['scene'] in args.only]
    OUT.mkdir(parents=True, exist_ok=True)
    manifest, previews = [], []
    for r in rows:
        pose_tr = rom.poses[r['pose']]
        atlas = rom.atlas_of.get(r['scene'], pose_tr[1])
        swap = (r['scene'], r['palette']) in PALETTE_SWAPS
        source = PALETTE_SWAPS.get((r['scene'], r['palette']))
        hd, used = hd_pose((pose_tr[0], pose_tr[1], source) if source else pose_tr, r['palette'])
        (pose_frames, p_bounds) = rom.frames(*used)
        pose = pose_frames[0][1]
        frames, t_bounds = rom.frames(r['scene'], atlas, r['palette'])
        out_frames = []
        for f, target in frames:
            if swap:
                image, stats = palette_swap(target, t_bounds, pose, p_bounds, hd)
            else:
                image, stats = derive_frame(target, t_bounds, pose, p_bounds, hd, r['method'] == '残影或分解特效')
            name = f"{r['scene']}-{r['palette']}-f{f}.png"
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8), 'RGBA').save(OUT / name)
            out_frames.append({'frame': f, 'file': name, **stats})
        manifest.append({'scene': r['scene'], 'atlas': atlas, 'palette': r['palette'], 'unit': r['units'][0],
                         'method': r['method'], 'hd_pose': list(used), 'bounds': list(t_bounds), 'scale': K,
                         'frames': out_frames})
        mid = frames[len(frames) // 2]
        previews.append((r, frames[0][1], out_frames[0]['file'], mid[1] if len(frames) > 2 else None,
                         out_frames[len(frames) // 2]['file'] if len(frames) > 2 else None))
        print(r['scene'], r['method'], len(out_frames), 'frames', flush=True)
    if (OUT / 'manifest.json').exists():
        # a partial run keeps the other items; any run keeps the image_gen redraws
        # (unit_extra_imagegen.py) that share this folder
        done = {(m['scene'], m['palette']) for m in manifest}
        old = json.loads((OUT / 'manifest.json').read_text())['items']
        manifest = [m for m in old if (args.only or m.get('method') == 'image_gen')
                    and (m['scene'], m['palette']) not in done] + manifest
    (OUT / 'manifest.json').write_text(json.dumps({'schema': 'srw64.unit-extras-derived.v1', 'items': manifest},
                                                 ensure_ascii=False, indent=1))
    sheets(previews, 'compare-only' if args.only else 'compare-v3')


def pack() -> None:
    import hashlib
    rom = Rom()
    manifest = json.loads((OUT / 'manifest.json').read_text())
    folder = OUT / 'pack'
    if folder.exists():
        for old in folder.iterdir():
            old.unlink()
    folder.mkdir(exist_ok=True)
    rows, seen = [], set()
    for item in manifest['items']:
        scene = parse_scene(rom.table.extract(item['scene'])[0])
        x0, y0 = item['bounds'][0], item['bounds'][1]
        for f in item['frames']:
            key = (item['scene'], item['atlas'], item['palette'], f['frame'])
            parts = scene.frames[f['frame']]
            if key in seen or not parts:
                continue
            seen.add(key)
            box = (min(p.x for p in parts), min(p.y for p in parts), max(p.x + p.w for p in parts), max(p.y + p.h for p in parts))
            image = Image.open(OUT / f['file']).convert('RGBA').crop(
                ((box[0] - x0) * K, (box[1] - y0) * K, (box[2] - x0) * K, (box[3] - y0) * K))
            image = pack_image(image, K)
            name = '{}-{}-{}-f{}.png'.format(*key)
            image.save(folder / name, optimize=True)
            rows.append({'scene': key[0], 'atlas': key[1], 'palette': key[2], 'frame': key[3], 'file': name,
                         'sha256': hashlib.sha256((folder / name).read_bytes()).hexdigest(),
                         'width': image.width, 'height': image.height, 'method': item['method'], 'unit': item['unit']})
    index = {'schema': 'srw64.unit-extra-images.v1', 'scale': PACK_SCALE,
             'recipe': 'derived from the whole HD unit poses (tools/hd_ai/unit_extra_derive.py)', 'images': rows}
    (folder / 'unit-extras.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(rows), 'frames packed')


def pack_image(image: Image.Image, scale: int) -> Image.Image:
    """A master at `scale` x as the pack stores it: transparent pixels near the outline take
    the nearest drawn colour (straight-alpha filtering never darkens the edge), farther ones
    the mean colour, then colour and alpha are resized apart to PACK_SCALE x. Only a few
    master pixels past the outline are ever sampled (the resize kernel, then the runtime's
    bilinear filter), so the colour is spread 8 px out, not over the whole field."""
    rgba = np.asarray(image.convert('RGBA')).astype(np.float32)
    if (rgba[..., 3] == 0).any() and (rgba[..., 3] > 0).any():
        solid = rgba[..., 3] > 0
        rgba[~solid, :3] = rgba[solid, :3].mean(0)
        colour = rgba[..., :3] * solid[..., None]
        weight = solid.astype(np.float32)
        for radius in (2, 4, 8):
            c, w = smooth(colour, radius), smooth(weight, radius)
            fill = (weight == 0) & (w > 1e-3)
            rgba[fill, :3] = c[fill] / w[fill, None]
            colour, weight = np.where(fill[..., None], rgba[..., :3], colour), np.maximum(weight, fill)
    image = Image.fromarray(np.clip(rgba, 0, 255).round().astype(np.uint8), 'RGBA')
    if scale == PACK_SCALE:
        return image
    size = (max(1, round(image.width * PACK_SCALE / scale)), max(1, round(image.height * PACK_SCALE / scale)))
    colour = image.convert('RGB').resize(size, Image.Resampling.LANCZOS)
    colour.putalpha(image.getchannel('A').resize(size, Image.Resampling.LANCZOS))
    return colour


def sheets(previews, prefix: str) -> None:
    font, small = ImageFont.truetype(FONT, 20), ImageFont.truetype(FONT, 16)
    H = 220

    def tile(img: Image.Image) -> Image.Image:
        k = min(H / img.height, 300 / img.width)
        img = img.resize((max(1, int(img.width * k)), max(1, int(img.height * k))),
                         Image.NEAREST if img.width < 200 else Image.LANCZOS)
        bg = Image.new('RGBA', (max(img.width, 120), H), (84, 90, 104, 255))
        bg.alpha_composite(img, ((bg.width - img.width) // 2, H - img.height))
        return bg.convert('RGB')

    by_method = {}
    for p in previews:
        by_method.setdefault(p[0]['method'], []).append(p)
    for n, method in enumerate(METHODS, 1):
        items = by_method.get(method, [])
        if not items:
            continue
        blocks = []
        for r, src, out, src_mid, out_mid in items:
            pics = [('原图', tile(Image.fromarray(np.clip(src, 0, 255).astype(np.uint8), 'RGBA'))),
                    ('推导 HD', tile(Image.open(OUT / out)))]
            if src_mid is not None:
                pics += [('原图（中间帧）', tile(Image.fromarray(np.clip(src_mid, 0, 255).astype(np.uint8), 'RGBA'))),
                         ('推导 HD（中间帧）', tile(Image.open(OUT / out_mid)))]
            label = f"{r['units'][0]}  {r['scene']}" + (f"  {r['frames']}帧" if r['frames'] > 1 else '')
            blocks.append((label, pics))
        cols = 2 if all(len(p) == 2 for _, p in blocks) else 1
        width = max(sum(im.width + 8 for _, im in pics) for _, pics in blocks) + 20
        rows = -(-len(blocks) // cols)
        sheet = Image.new('RGB', (width * cols, 50 + rows * (H + 62)), (24, 26, 32))
        d = ImageDraw.Draw(sheet)
        d.text((12, 12), f'{method}（{len(blocks)} 张）', fill='white', font=font)
        for j, (label, pics) in enumerate(blocks):
            x, y = (j % cols) * width + 12, 50 + (j // cols) * (H + 62)
            d.text((x, y), label, fill='white', font=small)
            for cap, im in pics:
                sheet.paste(im, (x, y + 24))
                d.text((x + 2, y + 26 + H), cap, fill=(190, 200, 215), font=small)
                x += im.width + 8
        sheet.save(OUT / f'{prefix}-{n}-{METHODS[n - 1]}.jpg', quality=88)


if __name__ == '__main__':
    main()
