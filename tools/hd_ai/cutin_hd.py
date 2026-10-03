"""HD frames of the battle cut-ins and the units' props, by the settled local ESRGAN recipe.

The close-up cut-ins are battle_scenes entries 984-1038 (CUTIN_REGISTRY): ordinary
actors, drawn by the scene sprite engine like the units, so the host replaces them
the same way, one whole HD picture per (scene, atlas, palette, frame) over the parts
the frame draws (native_sprite.cpp; docs/design/battle-animation-rendering.md §6.3).
Every frame the scenes step through is rendered, cut to its parts' box, and run through
the unit poses' recipe (esrgan_pose.py: 4x-UltraSharpV2 and 4x-PixelPerfectV4 at 8x,
alpha through the model separately, 50/50 blend). Identical frames run once.

The props are the other images in the units' atlases (rifles, sabres, shields, fists,
missiles, the odd flame or flash): every battle_scenes entry drawn from a unit pose's
atlas that is neither a pose, nor one of the extra images that unit_extra_derive.py and
unit_extra_imagegen.py rebuild from the pose (unit-extra-analysis.json), nor a cut-in.
They are replaced the same way and share the pack (`battle_sprites`).

    .venv/bin/python tools/hd_ai/cutin_hd.py export
    build/esrgan-venv/bin/python tools/hd_ai/cutin_hd.py run [--models build/esrgan-models]
    build/esrgan-venv/bin/python tools/hd_ai/cutin_hd.py pack [--bind]
    .venv/bin/python tools/hd_ai/cutin_hd.py scenes [--bind]

Output under assets/hd-ai/cutins: inputs/<digest>.png (the original frames),
frames.json, hd/<digest>.png (8x masters), review-*.jpg, and pack/ (battle-sprites.json,
srw64.unit-extra-images.v1, one PNG per frame at PACK_SCALE, 5x) that --bind points
the art manifest's `battle_sprites` at (the index other battle sprites will share).

The pack also carries the battle viewer's scene thumbnails (`scenes`: one daytime render
per scene of its list, from the battle backgrounds catalog), which `pack` adds and
`scenes` adds alone.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import CUTIN_REGISTRY, decode_atlas, parse_scene, read_triplets, render_scene  # noqa: E402

OUT = ROOT / 'assets/hd-ai/cutins'
MANIFEST = ROOT / 'content/art/stage1-hd.json'
ANALYSIS = ROOT / 'assets/hd-ai/battle-backgrounds/unit-extra-analysis.json'
RECIPE = ('4x-UltraSharpV2', '4x-PixelPerfectV4')
K = 8
# The battle viewer's scenes (src/host/battle_viewer.cpp scenes_table): key, a, b; the
# background record is D_800C5940[a] x 101 + b (docs/design/battle-viewer.md §2).
VIEWER_SCENES = (('plains', 0, 0), ('wasteland', 0, 7), ('rocks', 0, 21), ('village', 0, 22), ('mountains', 0, 24),
                 ('forest', 0, 25), ('sea', 0, 31), ('city', 0, 37), ('sky', 0, 39), ('ruins', 0, 41), ('base', 0, 51),
                 ('bridge', 0, 54), ('harbor', 0, 58), ('snow', 6, 32), ('desert', 10, 7), ('cave', 15, 15), ('moon', 17, 21),
                 ('space', 17, 100), ('fortress', 20, 15), ('other', 20, 83), ('otherworld', 22, 61))
SCENE_GROUPS = 0x800C5940 - 0x80075610
CATALOG = ROOT / 'assets/hd-ai/battle-backgrounds/catalog'


def digest(image: Image.Image) -> str:
    return hashlib.sha256(image.tobytes() + repr(image.size).encode()).hexdigest()[:16]


def prop_registries(rom: bytes) -> list[int]:
    scenes = read_triplets(rom, 'battle_scenes')
    poses = {k for k in read_triplets(rom, 'unit_poses') if k[0]}
    atlases = {a for _, a, _ in poses}
    extras = {(r['scene'], r['palette']) for r in json.loads(ANALYSIS.read_text())}
    return [r for r, (s, a, p) in enumerate(scenes)
            if s and a in atlases and (s, a, p) not in poses and (s, p) not in extras
            and not CUTIN_REGISTRY[0] <= r <= CUTIN_REGISTRY[1]]


def export() -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    scenes = read_triplets(rom, 'battle_scenes')
    (OUT / 'inputs').mkdir(parents=True, exist_ok=True)
    rows, seen = [], set()
    sets = [('cutin', r) for r in range(CUTIN_REGISTRY[0], CUTIN_REGISTRY[1] + 1)] + [('prop', r) for r in prop_registries(rom)]
    for kind, registry in sets:
        s, a, p = scenes[registry]
        if not s or (s, a, p) in seen:
            continue
        seen.add((s, a, p))
        scene = parse_scene(table.extract(s)[0])
        atlas, _ = decode_atlas(table.extract(a)[0], table.extract(p)[0])
        images, _ = render_scene(scene, atlas)
        x0, y0 = scene.bounds()[:2]
        for frame in sorted({f for f, _ in scene.steps if f != 0xFF}):
            parts = scene.frames[frame]
            if not parts:
                continue
            box = (min(q.x for q in parts), min(q.y for q in parts), max(q.x + q.w for q in parts), max(q.y + q.h for q in parts))
            image = images[frame].convert('RGBA').crop((box[0] - x0, box[1] - y0, box[2] - x0, box[3] - y0))
            key = digest(image)
            if not (OUT / 'inputs' / f'{key}.png').exists():
                image.save(OUT / 'inputs' / f'{key}.png')
            rows.append({'set': kind, 'registry': registry, 'scene': s, 'atlas': a, 'palette': p, 'frame': frame,
                         'box': list(box), 'input': key})
    (OUT / 'frames.json').write_text(json.dumps({'schema': 'srw64.cutin-frames.v1', 'frames': rows}, indent=1) + '\n')
    for kind in ('cutin', 'prop'):
        own = [r for r in rows if r['set'] == kind]
        print(kind, len(own), 'frames,', len({r['input'] for r in own}), 'distinct')


def run(models_dir: Path) -> None:
    import numpy as np
    import spandrel
    import torch
    from tools.hd_ai.esrgan_pose import run_pose
    device = torch.device('mps' if torch.backends.mps.is_available() else 'cpu')
    models = [spandrel.ModelLoader().load_from_file(str(next(models_dir.glob(f'{name}.*')))).to(device).eval() for name in RECIPE]
    keys = sorted({r['input'] for r in json.loads((OUT / 'frames.json').read_text())['frames']})
    (OUT / 'hd').mkdir(exist_ok=True)
    for n, key in enumerate(keys, 1):
        target = OUT / 'hd' / f'{key}.png'
        if target.exists():
            continue
        start = time.monotonic()
        source = Image.open(OUT / 'inputs' / f'{key}.png').convert('RGBA')
        a, b = (run_pose(model, source, device) for model in models)
        Image.blend(a, b, .5).save(target)
        print(f'{n}/{len(keys)} {key} {source.size} {time.monotonic() - start:.1f}s', flush=True)
    del np


def pack(bind: bool) -> None:
    frames = json.loads((OUT / 'frames.json').read_text())['frames']
    folder = OUT / 'pack'
    folder.mkdir(exist_ok=True)
    for old in folder.iterdir():
        old.unlink()
    rows = []
    for r in frames:
        name = f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png"
        redrawn = OUT / 'imagegen' / name            # cutin_imagegen.py compose: small faces redrawn
        source = redrawn if redrawn.exists() else OUT / 'hd' / f"{r['input']}.png"
        if not source.exists():
            raise SystemExit(f'no HD frame for {r} (run first)')
        from tools.hd_ai.unit_extra_derive import pack_image
        image = pack_image(Image.open(source), K)
        image.save(folder / name, optimize=True)
        rows.append({'scene': r['scene'], 'atlas': r['atlas'], 'palette': r['palette'], 'frame': r['frame'], 'file': name,
                     'sha256': hashlib.sha256((folder / name).read_bytes()).hexdigest(),
                     'width': image.width, 'height': image.height, 'method': 'image_gen' if source == redrawn else 'esrgan',
                     'unit': f"{'cut-in' if r.get('set', 'cutin') == 'cutin' else 'prop'} {r['registry']}"})
    from tools.hd_ai.unit_extra_derive import PACK_SCALE
    index = {'schema': 'srw64.unit-extra-images.v1', 'scale': PACK_SCALE,
             'recipe': f'50% {RECIPE[0]} + 50% {RECIPE[1]} (tools/hd_ai/cutin_hd.py)', 'images': rows}
    (folder / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(rows), 'frames packed')
    scenes(bind)


def scenes(bind: bool) -> None:
    """The viewer's scene thumbnails into the pack (scene-<key>.jpg, index `scenes`)."""
    folder = OUT / 'pack'
    rom = (ROOT / 'rom.z64').read_bytes()
    sets = json.loads((CATALOG / 'catalog.json').read_text())['sets']
    index = json.loads((folder / 'battle-sprites.json').read_text())
    rows = []
    for key, a, b in VIEWER_SCENES:
        record = rom[SCENE_GROUPS + a] * 101 + b
        found = next(s for s in sets if record in s['records'] and not s['night'])
        name = f'scene-{key}.jpg'
        data = (CATALOG / 'thumbs' / f"{found['thumb']}.jpg").read_bytes()
        (folder / name).write_bytes(data)
        with Image.open(folder / name) as image:
            rows.append({'key': key, 'record': record, 'file': name, 'sha256': hashlib.sha256(data).hexdigest(),
                         'width': image.width, 'height': image.height})
    index['scenes'] = rows
    (folder / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(rows), 'scene thumbnails')
    if bind:
        manifest = json.loads(MANIFEST.read_text())
        manifest['battle_sprites'] = {'path': str(folder.relative_to(ROOT)),
                              'manifest_sha256': hashlib.sha256((folder / 'battle-sprites.json').read_bytes()).hexdigest()}
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT))


def review() -> None:
    """Original beside HD for the middle frame of every cut-in and prop, eight to a sheet
    (review-<set>-N.jpg)."""
    frames = json.loads((OUT / 'frames.json').read_text())['frames']
    for kind in ('cutin', 'prop'):
        by_registry = {}
        for r in frames:
            if r.get('set', 'cutin') == kind:
                by_registry.setdefault(r['registry'], []).append(r)
        picks = [rs[len(rs) // 2] for rs in by_registry.values()]
        W, H = (320, 240) if kind == 'cutin' else (200, 150)
        cols, rows = (2, 4) if kind == 'cutin' else (4, 8)
        per = cols * rows
        for page in range(0, len(picks), per):
            chunk = picks[page:page + per]
            sheet = Image.new('RGB', (cols * (2 * W + 16) + 8, rows * (H + 26) + 8), (28, 30, 36))
            draw = ImageDraw.Draw(sheet)
            for i, r in enumerate(chunk):
                x, y = 8 + (i % cols) * (2 * W + 16), 6 + (i // cols) * (H + 26)
                draw.text((x, y), f"{r['registry']}  scene {r['scene']} f{r['frame']}", fill='white')
                hd = OUT / 'imagegen' / f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png"
                for j, path in enumerate((OUT / 'inputs' / f"{r['input']}.png", hd if hd.exists() else OUT / 'hd' / f"{r['input']}.png")):
                    image = Image.open(path).convert('RGBA')
                    k = min(W / image.width, H / image.height, 6)
                    image = image.resize((max(1, round(image.width * k)), max(1, round(image.height * k))),
                                         Image.Resampling.NEAREST if j == 0 else Image.Resampling.LANCZOS)
                    tile = Image.new('RGBA', (W, H), (70, 74, 88, 255))
                    tile.alpha_composite(image, ((W - image.width) // 2, (H - image.height) // 2))
                    sheet.paste(tile.convert('RGB'), (x + j * (W + 8), y + 16))
            sheet.save(OUT / f'review-{kind}-{page // per + 1}.jpg', quality=88)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('export', 'run', 'pack', 'scenes', 'review'))
    parser.add_argument('--models', type=Path, default=ROOT / 'build/esrgan-models')
    parser.add_argument('--bind', action='store_true', help='point the art manifest at the pack')
    args = parser.parse_args()
    if args.step == 'export':
        export()
    elif args.step == 'run':
        run(args.models)
    elif args.step == 'pack':
        pack(args.bind)
    elif args.step == 'scenes':
        scenes(args.bind)
    else:
        review()


if __name__ == '__main__':
    main()
