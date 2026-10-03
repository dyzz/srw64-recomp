"""image_gen kit for the cut-in pictures whose faces are too small for ESRGAN.

A cut-in's frames are cut from whole pictures kept intact in its atlas (several
figures, a group of busts, a couple dancing), placed part by part. Where a face is
only 6-20 original pixels, the ESRGAN recipe invents the eyes and mouth
(2026-10-02, cut-ins 1029 and 1023), so those pictures are redrawn with image_gen
instead, one per picture, and the frames are rebuilt from an 8x atlas made of them.

    build/esrgan-venv/bin/python tools/hd_ai/cutin_imagegen.py kit
    build/esrgan-venv/bin/python tools/hd_ai/cutin_imagegen.py compose

writes assets/hd-ai/cutin-imagegen: items/NN-atlas-x-y-input.png (the original
picture on grey, 6x bicubic), -reference.png (a close-up of the same characters
from the same cut-in, already HD), -prompt.txt, manifest.json, README.md and
index.jpg. The outputs go in outputs/NN-...-out.png.

compose reads selected-manifest.json (the chosen version of each output), registers
and mattes every picture against the original (portrait_matte), pastes them into an
8x copy of the atlas, and rebuilds at 8x every frame of the cut-ins whose parts all
come from redrawn pictures, by the scene's own part list (render_frame at 8x). The
frames go to assets/hd-ai/cutins/imagegen/<scene>-<atlas>-<palette>-f<frame>.png,
which cutin_hd.py pack prefers over the ESRGAN frame; review-imagegen-*.jpg shows
them beside the originals.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import decode_atlas, parse_scene, read_triplets  # noqa: E402

OUT = ROOT / 'assets/hd-ai/cutin-imagegen'
CUTINS = ROOT / 'assets/hd-ai/cutins'
GREY = (100, 100, 112)
SCALE = 6
OVERLAY_LIMIT = 600   # opaque texels of other pictures a rebuilt frame may leave out
FONT = '/System/Library/Fonts/Hiragino Sans GB.ttc'

# (group, cut-ins, atlas, palette, box in atlas pixels, what it shows, reference: (registry, frame))
PICTURES = (
    ('A', (1029,), 1357, 1389, (444, 18, 495, 98), 'a young man in a golden bodysuit raising one open palm inside a glowing red-rimmed circle', (1027, 13)),
    ('A', (1029,), 1357, 1389, (286, 119, 338, 210), 'a young man in a golden bodysuit raising one open palm inside a glowing red-rimmed circle', (1027, 13)),
    ('A', (1029,), 1357, 1389, (446, 129, 498, 210), 'a young man in a golden bodysuit raising one open palm inside a glowing red-rimmed circle', (1027, 13)),
    ('A', (1029,), 1357, 1389, (0, 228, 65, 322), 'a tall young man in a golden bodysuit raising one open palm inside a glowing red-rimmed circle', (1027, 13)),
    ('A', (1029,), 1357, 1389, (90, 234, 142, 290), 'a small boy in a golden bodysuit raising one open palm inside a glowing red-rimmed circle', (1027, 13)),
    ('A', (1023,), 1361, 1387, (25, 0, 146, 98), 'a dark-haired man with a red headband holding a brown-haired woman from behind', (1023, 2)),
    ('A', (1023,), 1361, 1387, (150, 110, 257, 210), 'a brown-haired woman in a red dress and a dark-haired man with a red headband facing each other, a small flame in her hand', (1023, 2)),
    ('A', (1023,), 1361, 1387, (0, 222, 114, 322), 'a brown-haired woman in a red dress thrusting a flaming hand beside a dark-haired man with a red headband', (1023, 3)),
    ('A', (1023,), 1361, 1387, (142, 222, 258, 322), 'a brown-haired woman in a red dress pushing a huge flaming hand toward the viewer, a dark-haired man with a red headband behind her', (1023, 3)),
    ('A', (1023,), 1361, 1387, (0, 336, 114, 434), 'a couple in a dance pose on top of a golden hill against a white full moon: a woman in a red dress leaning back, a dark-haired man behind her', (1023, 2)),
    ('B', (1027,), 1357, 1389, (24, 32, 130, 98), 'a shouting young man with a ponytail in a golden suit, fist raised, seen from the side', None),
    ('B', (1027,), 1357, 1389, (142, 3, 258, 98), 'a shouting blond man in a golden suit, fist raised, seen from the side', None),
    ('B', (1027,), 1357, 1389, (292, 7, 402, 98), 'a shouting man with long messy hair and a headband in a golden suit, fist raised', None),
    ('B', (1027,), 1357, 1389, (0, 110, 114, 210), 'a shouting man with spiky hair and a headband in a golden suit, fist raised', None),
    ('B', (1027,), 1357, 1389, (145, 110, 258, 210), 'a shouting man with long swept blond hair in a golden suit, fist raised, seen from the side', None),
)

PROMPT = """Use case: style-transfer. Asset: one picture from the battle cut-in of a 1999 Japanese tactical RPG: {what}. It is cut out against the flat grey background and the game puts it back at the same place and size as the original, so the silhouette and every figure's position must not move.
Image 1 is the edit target: the original low-resolution picture, enlarged (the blocks and stair-step edges are only the old pixel grid). The faces are drawn with very few pixels.
Image 2 is a CHARACTER AND STYLE REFERENCE ONLY: a larger close-up of the same characters from the same cut-in, already redrawn in the look the game now uses. Use it for what their faces, eyes, hair and costume look like, and match its clean 1990s anime cel style: confident dark outlines, flat cel colours with two-tone shading, the same palette. Do not copy Image 2's composition.
Redraw Image 1 as that high-resolution anime cel illustration. Keep EXACTLY Image 1's composition, poses, gestures, facing directions, crop, silhouettes, the position and size of every figure, head and hand, and the colours. Faces must stay the same people with the same expressions (open or closed mouth, eye direction) as Image 1 suggests; give them clean, well-proportioned anime faces, never smudged, melted or distorted. Replace every stair-step edge with a smooth hand-drawn line and remove all pixel blocks.
Composition: same aspect ratio as Image 1, output about {size}, the picture filling the same area of the canvas as in Image 1, nothing cropped differently.
Background: flat solid grey RGB(100,100,112) everywhere outside the picture, no gradient, no shadow, no glow.
Critical exclusions: no text, logos, speed lines, extra effects, extra characters, frame or border. Not a 3D render, not a photo, not pixel art."""

# Frames that overlay small patches from pictures not redrawn: (id, cut-in, frame, what changes)
FRAME_EDITS = (
    ('16-1029-f24', 1029, 24, 'the five men open their eyes and glare'),
)

EDIT_PROMPT = """Use case: local edit. Asset: the last frame of a battle cut-in of a 1999 Japanese tactical RPG: five young men in golden bodysuits, each raising an open palm inside a glowing red-rimmed circle. In this frame {what}.
Image 1 is the target: the original low-resolution frame, enlarged (the blocks are only the old pixel grid).
Image 2 is the same frame already redrawn in high resolution, except that the eyes are still those of the frame before.
Image 3 marks what to change: red is where Image 1 differs from Image 2 (the eyes), grey is where they are the same.
Output Image 2 unchanged everywhere grey: same drawing, lines, colours, figures and positions. Only in the red areas redraw the eyes as Image 1 shows them, in Image 2's clean anime cel style, so every face stays the same person, well proportioned, never smudged or distorted, and the seams are invisible.
Composition: exactly Image 2's canvas, aspect ratio and framing, output about {size}.
Background: flat solid grey RGB(100,100,112) outside the figures. No text, effects or extra characters."""

GROUP_PROMPT = PROMPT.replace(
    'Image 2 is a CHARACTER AND STYLE REFERENCE ONLY: a larger close-up of the same characters from the same cut-in, already redrawn in the look the game now uses. Use it for what their faces, eyes, hair and costume look like, and match its clean 1990s anime cel style: confident dark outlines, flat cel colours with two-tone shading, the same palette. Do not copy Image 2\'s composition.',
    'There is no second image. Use a clean 1990s anime cel style: confident dark outlines, flat cel colours with two-tone shading, keeping Image 1\'s palette.')


def reference(registry: int, frame: int) -> Image.Image:
    frames = json.loads((CUTINS / 'frames.json').read_text())['frames']
    row = next(r for r in frames if r['registry'] == registry and r['frame'] == frame)
    image = Image.open(CUTINS / 'hd' / f"{row['input']}.png").convert('RGBA')
    flat = Image.new('RGBA', image.size, GREY + (255,))
    flat.alpha_composite(image)
    return flat.convert('RGB')


def kit() -> None:
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    (OUT / 'items').mkdir(parents=True, exist_ok=True)
    (OUT / 'outputs').mkdir(exist_ok=True)
    atlases = {}
    items = []
    for n, (group, registries, atlas, palette, box, what, ref) in enumerate(PICTURES, 1):
        if (atlas, palette) not in atlases:
            atlases[atlas, palette] = decode_atlas(table.extract(atlas)[0], table.extract(palette)[0])[0].convert('RGBA')
        picture = atlases[atlas, palette].crop(box)
        ident = f'{n:02d}-{atlas}-{box[0]}-{box[1]}'
        flat = Image.new('RGBA', picture.size, GREY + (255,))
        flat.alpha_composite(picture)
        flat.convert('RGB').resize((picture.width * SCALE, picture.height * SCALE), Image.Resampling.BICUBIC).save(OUT / 'items' / f'{ident}-input.png')
        k = 1536 / max(picture.size)
        size = f'{round(picture.width * k / 32) * 32}x{round(picture.height * k / 32) * 32}'
        if ref:
            reference(*ref).save(OUT / 'items' / f'{ident}-reference.png')
        (OUT / 'items' / f'{ident}-prompt.txt').write_text((PROMPT if ref else GROUP_PROMPT).format(what=what, size=size) + '\n')
        items.append({'id': ident, 'group': group, 'cutins': list(registries), 'atlas': atlas, 'palette': palette, 'box': list(box),
                      'source_size': list(picture.size), 'input': f'items/{ident}-input.png', 'input_scale': SCALE,
                      'reference': f'items/{ident}-reference.png' if ref else None, 'reference_frame': list(ref) if ref else None,
                      'prompt': f'items/{ident}-prompt.txt', 'output': f'outputs/{ident}-out.png', 'output_size': size})
    items += frame_edit_items(table)
    (OUT / 'manifest.json').write_text(json.dumps({'schema': 'srw64.imagegen-kit.v1', 'kind': 'battle cut-in pictures',
                                                   'background': list(GREY), 'items': items}, ensure_ascii=False, indent=1) + '\n')
    index(items)
    print(len(items), 'items ->', OUT.relative_to(ROOT))


def frame_parts(table: ResourceTable, registry: int, frame: int):
    rom = (ROOT / 'rom.z64').read_bytes()
    s, a, p = read_triplets(rom, 'battle_scenes')[registry]
    parts = parse_scene(table.extract(s)[0]).frames[frame]
    original = decode_atlas(table.extract(a)[0], table.extract(p)[0])[0].convert('RGBA')
    return (s, a, p), parts, original


def overlay_split(parts, boxes):
    return [any(q.s < b[2] and q.t < b[3] and q.s + q.w > b[0] and q.t + q.h > b[1] for b in boxes) for q in parts]


def render_low(parts, atlas: Image.Image, only=None) -> Image.Image:
    x0, y0 = min(q.x for q in parts), min(q.y for q in parts)
    canvas = Image.new('RGBA', (max(q.x + q.w for q in parts) - x0, max(q.y + q.h for q in parts) - y0))
    for i, q in enumerate(parts):
        if only is not None and not only[i]:
            continue
        piece = atlas.crop((q.s, q.t, q.s + q.w, q.t + q.h))
        if q.flip_x:
            piece = piece.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        canvas.alpha_composite(piece, (q.x - x0, q.y - y0))
    return canvas


def frame_edit_items(table: ResourceTable) -> list[dict]:
    """A frame the rebuild drew without its overlay patches: Image 1 the original frame,
    Image 2 the rebuilt one (cutins/imagegen), Image 3 the patches in red."""
    boxes = {}
    for picture in PICTURES:
        boxes.setdefault(picture[2], []).append(picture[4])
    items = []
    for ident, registry, frame, what in FRAME_EDITS:
        (s, a, p), parts, original = frame_parts(table, registry, frame)
        rebuilt = CUTINS / 'imagegen' / f'{s}-{a}-{p}-f{frame}.png'
        if not rebuilt.exists():
            print(ident, 'skipped: run compose first')
            continue
        whole = render_low(parts, original)
        inside = overlay_split(parts, boxes[a])
        patches = render_low(parts, original, [not ok for ok in inside])
        flat = Image.new('RGBA', whole.size, GREY + (255,))
        flat.alpha_composite(whole)
        flat.convert('RGB').resize((whole.width * SCALE, whole.height * SCALE), Image.Resampling.BICUBIC).save(OUT / 'items' / f'{ident}-input.png')
        hd = Image.open(rebuilt).convert('RGBA')
        ref = Image.new('RGBA', hd.size, GREY + (255,))
        ref.alpha_composite(hd)
        ref.convert('RGB').resize((whole.width * SCALE, whole.height * SCALE), Image.Resampling.LANCZOS).save(OUT / 'items' / f'{ident}-reference.png')
        marks = np.zeros((whole.height, whole.width, 3), np.uint8)
        marks[np.asarray(whole)[..., 3] > 0] = (110, 110, 110)
        marks[np.asarray(patches)[..., 3] > 0] = (255, 0, 0)
        Image.fromarray(marks).resize((whole.width * SCALE, whole.height * SCALE), Image.Resampling.NEAREST).save(OUT / 'items' / f'{ident}-changed.png')
        k = 1536 / max(whole.size)
        size = f'{round(whole.width * k / 32) * 32}x{round(whole.height * k / 32) * 32}'
        (OUT / 'items' / f'{ident}-prompt.txt').write_text(EDIT_PROMPT.format(what=what, size=size) + '\n')
        items.append({'id': ident, 'group': 'A', 'kind': 'frame edit', 'cutins': [registry], 'frame': frame,
                      'scene': s, 'atlas': a, 'palette': p, 'source_size': list(whole.size),
                      'input': f'items/{ident}-input.png', 'input_scale': SCALE, 'reference': f'items/{ident}-reference.png',
                      'changed_map': f'items/{ident}-changed.png', 'prompt': f'items/{ident}-prompt.txt',
                      'output': f'outputs/{ident}-out.png', 'output_size': size})
    return items


def index(items: list[dict]) -> None:
    font = ImageFont.truetype(FONT, 18)
    T = 300
    sheet = Image.new('RGB', (2 * (T + 8) * 2 + 24, -(-len(items) // 2) * (T + 32) + 8), (28, 30, 36))
    draw = ImageDraw.Draw(sheet)
    for i, item in enumerate(items):
        x, y = 8 + (i % 2) * (2 * (T + 8) + 16), 6 + (i // 2) * (T + 32)
        draw.text((x, y), f"{item['id']}  组{item['group']}  cut-in {item['cutins'][0]}", fill='white', font=font)
        for j, path in enumerate((item['input'], item['reference'])):
            if not path:
                continue
            image = Image.open(OUT / path)
            k = min(T / image.width, T / image.height)
            image = image.resize((round(image.width * k), round(image.height * k)), Image.Resampling.LANCZOS)
            sheet.paste(image, (x + j * (T + 8), y + 26))
    sheet.save(OUT / 'index.jpg', quality=88)


def compose() -> None:
    from tools.hd_ai.portrait_matte import matte_portrait
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    scenes = read_triplets(rom, 'battle_scenes')
    selected = json.loads((OUT / 'selected-manifest.json').read_text())['items']
    hd_atlas, boxes, report = {}, {}, {}
    for item in selected:
        atlas, palette, box = item['atlas'], item['palette'], tuple(item['box'])
        original = decode_atlas(table.extract(atlas)[0], table.extract(palette)[0])[0].convert('RGBA')
        if (atlas, palette) not in hd_atlas:
            hd_atlas[atlas, palette] = Image.new('RGBA', (original.width * 8, original.height * 8))
        source = original.crop(box)
        generated = Image.open(OUT / item['output'])
        try:
            master, fit = matte_portrait(generated, source, GREY)
        except RuntimeError:
            master, fit = matte_portrait(generated, source, GREY, (0.09, 20, 9))
            fit['relaxed'] = True
        hd_atlas[atlas, palette].paste(master, (box[0] * 8, box[1] * 8))
        boxes.setdefault((atlas, palette), []).append(box)
        report[item['id']] = {k: fit[k] for k in ('registration', 'silhouette_iou_against_original', 'warnings') if k in fit} | {'relaxed': fit.get('relaxed', False)}
        print(item['id'], 'relaxed' if fit.get('relaxed') else '', f"iou {fit['silhouette_iou_against_original']:.3f}", fit.get('warnings', ''), flush=True)
    target = CUTINS / 'imagegen'
    target.mkdir(exist_ok=True)
    for old in target.iterdir():
        old.unlink()
    rebuilt, mixed, dropped, pairs = [], [], [], []
    for registry in sorted({r for item in selected for r in item['cutins']}):
        s, a, p = scenes[registry]
        if (a, p) not in hd_atlas:
            continue
        redrawn = boxes[a, p]
        scene = parse_scene(table.extract(s)[0])
        original = decode_atlas(table.extract(a)[0], table.extract(p)[0])[0].convert('RGBA')
        for frame in sorted({f for f, _ in scene.steps if f != 0xFF}):
            parts = scene.frames[frame]
            # a part belongs to a redrawn picture when its atlas rectangle overlaps it
            inside = [any(q.s < b[2] and q.t < b[3] and q.s + q.w > b[0] and q.t + q.h > b[1] for b in redrawn) for q in parts]
            if not any(inside):
                continue
            if not all(inside):
                # small overlays (the eye patches of 1029's last frame) are left out and
                # listed; a frame that needs a whole other picture keeps its ESRGAN frame
                extra = sum(int((np.asarray(original.crop((q.s, q.t, q.s + q.w, q.t + q.h)))[..., 3] > 0).sum())
                            for q, ok in zip(parts, inside) if not ok)
                if extra > OVERLAY_LIMIT:
                    mixed.append((registry, frame))
                    continue
                dropped.append((registry, frame, extra))
                parts = [q for q, ok in zip(parts, inside) if ok]
            x0, y0 = min(q.x for q in parts), min(q.y for q in parts)
            x1, y1 = max(q.x + q.w for q in parts), max(q.y + q.h for q in parts)
            canvas = Image.new('RGBA', ((x1 - x0) * 8, (y1 - y0) * 8))
            low = Image.new('RGBA', (x1 - x0, y1 - y0))
            for q in parts:
                piece = hd_atlas[a, p].crop((q.s * 8, q.t * 8, (q.s + q.w) * 8, (q.t + q.h) * 8))
                small = original.crop((q.s, q.t, q.s + q.w, q.t + q.h))
                if q.flip_x:
                    piece, small = piece.transpose(Image.Transpose.FLIP_LEFT_RIGHT), small.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                canvas.alpha_composite(piece, ((q.x - x0) * 8, (q.y - y0) * 8))
                low.alpha_composite(small, (q.x - x0, q.y - y0))
            name = f'{s}-{a}-{p}-f{frame}.png'
            canvas.save(target / name)
            rebuilt.append(name)
            pairs.append((f'{registry} f{frame}', low, canvas))
    (OUT / 'compose-report.json').write_text(json.dumps({'pictures': report, 'rebuilt_frames': rebuilt,
                                                         'mixed_frames_left_esrgan': mixed,
                                                         'overlays_left_out': dropped}, indent=1) + '\n')
    print(len(rebuilt), 'frames rebuilt;', len(mixed), 'mixed frames keep ESRGAN', mixed, '; overlays left out', dropped)
    font = ImageFont.truetype(FONT, 16)
    T = 280
    for page in range(0, len(pairs), 12):
        chunk = pairs[page:page + 12]
        sheet = Image.new('RGB', (2 * (2 * T + 24), -(-len(chunk) // 2) * (T + 30) + 8), (28, 30, 36))
        draw = ImageDraw.Draw(sheet)
        for i, (label, low, high) in enumerate(chunk):
            x, y = 8 + (i % 2) * (2 * T + 24), 6 + (i // 2) * (T + 30)
            draw.text((x, y), label + '   原图 | image_gen', fill='white', font=font)
            for j, image in enumerate((low, high)):
                k = min(T / image.width, T / image.height)
                image = image.resize((max(1, round(image.width * k)), max(1, round(image.height * k))),
                                     Image.Resampling.NEAREST if j == 0 else Image.Resampling.LANCZOS)
                tile = Image.new('RGBA', (T, T), GREY + (255,))
                tile.alpha_composite(image, ((T - image.width) // 2, (T - image.height) // 2))
                sheet.paste(tile.convert('RGB'), (x + j * (T + 8), y + 22))
        sheet.save(OUT / f'review-imagegen-{page // 12 + 1}.jpg', quality=88)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('kit', 'compose'))
    args = parser.parse_args()
    kit() if args.step == 'kit' else compose()


if __name__ == '__main__':
    main()
