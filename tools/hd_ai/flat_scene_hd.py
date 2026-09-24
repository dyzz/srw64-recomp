"""HD scene frames for flat-colour art: the BANPRESTO logo and GAME OVER, no AI.

Both are scene sprites (docs/native/native-title-and-story-images.md) drawn
from a handful of flat colours plus anti-aliasing between them: the boot logo
(scene 619, atlas 617, palette 618) and the defeat banner (614 / 615 / 616).
The logo is a trademark, so it is not redrawn by a model; both are upscaled
deterministically instead:

- every source pixel is split between its two nearest key colours (the
  in-between shades are the original's anti-aliasing);
- keys are grouped into layers; each layer's membership is upscaled, smoothed
  and resolved by a steep soft-argmax, so edges are about one output pixel
  wide however coarse the source is;
- a layer's colour is the smoothed mix of its own keys, so shading inside a
  layer (GAME OVER's grey outline) survives.

`build` renders the original frames from the ROM, upscales them and writes a
new scene-image set: the existing set plus these frames. With --bind it points
content/art/stage1-hd.json's scene_images at the new set.

  .venv/bin/python -m tools.hd_ai.flat_scene_hd build \\
      --base assets/hd-ai/title/whole-v1 --target assets/hd-ai/title/whole-v2 --bind
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageFilter, ImageMath

from srw64_rom.resources import ResourceTable
from srw64_native.battle_graphics import parse_scene, render_scene
from srw64_native.original_images import decode_indexed

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'content/art/stage1-hd.json'
SCALE = 8
# Layers as groups of key colours, read off each palette: the rest is anti-aliasing.
SCENES = {
    'banpresto-logo': {'scene': 619, 'atlas': 617, 'palette': 618,
                       'layers': [[(8, 8, 8), (0, 0, 0)], [(255, 255, 255)], [(189, 49, 49)]]},
    'game-over': {'scene': 614, 'atlas': 615, 'palette': 616,
                  'layers': [[(255, 255, 255), (239, 239, 239), (222, 222, 222)],
                             [(16, 16, 16), (33, 33, 33), (49, 49, 49), (74, 74, 74), (90, 90, 90), (107, 107, 107)]]},
}
BLUR = .45    # source px of smoothing before the layers are resolved
SHARP = 2.0   # soft-argmax gain per output px: edges about 1/SHARP px wide


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _eval(f, **images):
    return ImageMath.lambda_eval(lambda e: f(e), **images)


def memberships(image: Image.Image, keys: list) -> list[Image.Image]:
    """Per key colour (and transparency last), how much of each pixel it covers."""
    image = image.convert('RGBA')
    w, h = image.size
    layers = [Image.new('F', (w, h)) for _ in range(len(keys) + 1)]
    data = [layer.load() for layer in layers]
    k = [tuple(c / 255 for c in key) for key in keys]
    source = image.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = (v / 255 for v in source[x, y])
            data[-1][x, y] = 1 - a
            if a == 0:
                continue
            best = None
            for i in range(len(k)):
                for j in range(i, len(k)):
                    d = [k[j][c] - k[i][c] for c in range(3)]
                    dd = sum(v * v for v in d)
                    t = 0 if dd == 0 else min(1, max(0, sum(((r, g, b)[c] - k[i][c]) * d[c] for c in range(3)) / dd))
                    err = sum(((r, g, b)[c] - (k[i][c] + t * d[c])) ** 2 for c in range(3))
                    if best is None or err < best[0]:
                        best = (err, i, j, t)
            _, i, j, t = best
            data[i][x, y] += a * (1 - t)
            data[j][x, y] += a * t
    return layers


def _smooth(layer: Image.Image, size: tuple, radius: float) -> Image.Image:
    """Bicubic upscale and Gaussian blur of a [0,1] float layer (Pillow blurs 8-bit bands only)."""
    big = _eval(lambda e: e['a'] * 255, a=layer.resize(size, Image.BICUBIC)).convert('L')
    return _eval(lambda e: e['float'](e['a']) / 255, a=big.filter(ImageFilter.GaussianBlur(radius)))


def upscale(image: Image.Image, scale: int, layers: list, blur: float = BLUR, sharp: float = SHARP) -> Image.Image:
    """Straight-alpha RGBA at `scale` times the size; transparent texels keep the nearest layer colour."""
    keys = [c for group in layers for c in group]
    m = memberships(image, keys)
    size = (image.width * scale, image.height * scale)
    radius = blur * scale
    covers, colours, start = [], [], 0
    for group in layers:
        own = list(range(start, start + len(group)))
        start += len(group)
        total = m[own[0]]
        for i in own[1:]:
            total = _eval(lambda e: e['a'] + e['b'], a=total, b=m[i])
        covers.append(_smooth(total, size, radius))
        rgb = []
        for c in range(3):
            mix = Image.new('F', image.size)
            for i in own:
                mix = _eval(lambda e: e['a'] + e['b'] * (keys[i][c] / 255), a=mix, b=m[i])
            rgb.append(_eval(lambda e: e['a'] / e['max'](e['b'], 1e-4), a=_smooth(mix, size, radius), b=covers[-1]))
        colours.append(rgb)
    covers.append(_smooth(m[-1], size, radius))
    gain = sharp * scale
    weights = []
    for i, cover in enumerate(covers):
        others = [c for j, c in enumerate(covers) if j != i]
        top = others[0]
        for other in others[1:]:
            top = _eval(lambda e: e['max'](e['a'], e['b']), a=top, b=other)
        weights.append(_eval(lambda e: e['min'](e['max']((e['a'] - e['b']) * gain + 0.5, 0.0), 1.0), a=cover, b=top))
    total = weights[0]
    for w in weights[1:]:
        total = _eval(lambda e: e['a'] + e['b'], a=total, b=w)
    opaque = Image.new('F', size)
    for w in weights[:-1]:
        opaque = _eval(lambda e: e['a'] + e['b'], a=opaque, b=w)
    alpha = _eval(lambda e: e['min'](e['a'] / e['max'](e['b'], 1e-6), 1.0) * 255, a=opaque, b=total).convert('L')
    bands = []
    for c in range(3):
        mix = Image.new('F', size)
        for w, rgb in zip(weights[:-1], colours):
            mix = _eval(lambda e: e['a'] + e['b'] * e['c'], a=mix, b=w, c=rgb[c])
        bands.append(_eval(lambda e: e['min'](e['a'] / e['max'](e['b'], 1e-4), 1.0) * 255, a=mix, b=opaque).convert('L'))
    return Image.merge('RGBA', bands + [alpha])


def frame(table: ResourceTable, spec: dict) -> Image.Image:
    data = lambda i: table.extract(i)[0]
    images, clipped = render_scene(parse_scene(data(spec['scene'])), decode_indexed(data(spec['atlas']), data(spec['palette'])))
    if clipped or len(images) != 1:
        raise ValueError(f"scene {spec['scene']}: expected one frame inside its atlas")
    return images[0]


def build(args: argparse.Namespace) -> None:
    base, target = args.base, args.target
    index = json.loads((base / 'scene-images.json').read_text())
    if index.get('schema') != 'srw64.scene-images.v1':
        raise ValueError('unsupported scene image set')
    table = ResourceTable((ROOT / 'rom.z64').read_bytes())
    target.mkdir(parents=True, exist_ok=False)
    for row in index['images']:
        if sha(base / row['file']) != row['sha256']:
            raise ValueError(f"scene image changed: {row['file']}")
        shutil.copyfile(base / row['file'], target / row['file'])
    images = [row for row in index['images'] if (row['scene'], row['atlas'], row['palette']) not in
              {(s['scene'], s['atlas'], s['palette']) for s in SCENES.values()}]
    for name, spec in SCENES.items():
        source = frame(table, spec)
        high = upscale(source, SCALE, spec['layers'])
        file = f'{name}.png'
        high.save(target / file)
        # Check: back at the original size, the upscale matches the original frame.
        back = high.resize(source.size, Image.BOX)
        diff = sum(abs(a - b) for p, q in zip(back.convert('RGBA').get_flattened_data(), source.convert('RGBA').get_flattened_data())
                   for a, b in zip(p, q)) / (source.width * source.height * 4)
        print({'image': file, 'size': high.size, 'mean_difference': round(diff, 2)})
        images.append({'scene': spec['scene'], 'atlas': spec['atlas'], 'palette': spec['palette'], 'frames': [0],
                       'file': file, 'uv': [0, 0, 1, 1], 'wrap': False, 'sha256': sha(target / file)})
    (target / 'scene-images.json').write_text(json.dumps({**index, 'images': images}, indent=2) + '\n')
    if args.bind:
        text = MANIFEST.read_text()
        manifest = json.loads(text)
        manifest['scene_images'] = {'path': str(target.resolve().relative_to(ROOT)),
                                    'manifest_sha256': sha(target / 'scene-images.json')}
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print({'images': len(images), 'target': str(target)})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('build')
    command.add_argument('--base', type=Path, required=True, help='the scene-image set to extend')
    command.add_argument('--target', type=Path, required=True)
    command.add_argument('--bind', action='store_true', help='point stage1-hd.json scene_images at the new set')
    args = parser.parse_args()
    {'build': build}[args.command](args)


if __name__ == '__main__':
    main()
