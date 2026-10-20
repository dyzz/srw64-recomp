"""Compose still-sky outpaints; invoked by battle_sky_hd wide-{prepare,compose}.

Pillow and numpy (the ESRGAN environment) suffice. Generation is deliberately a separate
image_gen edit: rerunning composition never sends a paid request. Coordinates describe
an output-to-input affine transform after resizing the generated image to the canvas.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re

from PIL import Image

from tools.hd_ai.battle_sky_hd import ROOT, sha

SOURCE = ROOT / 'assets/hd-ai/battle-skies/v1/sky-6107-6213.png'
CANVAS = (1708, 960)
LEFT = 214
EXTENT = [-53.333333333333336, 0, 373.3333333333333, 240]
PROMPT = '''Outpaint this image horizontally to fill the empty left and right margins of the canvas.
The centre picture is a spiral galaxy / cosmic vortex background from a 1999 Nintendo 64
mecha strategy game (Super Robot Taisen 64), shown behind battle animations.
Keep the centre picture exactly as it is: do not redraw, recolour, shift or crop it.
Fill only the blank margins so the image reads as one continuous, wider scene:
- continue the spiral arms, dark dust lanes and star clusters outward from both edges
  of the centre picture, following their curvature around the bright core;
- match the existing palette exactly (deep navy and teal blues, near-black voids,
  pale cyan-white highlights) and the same brightness and contrast;
- match the texture: soft painted nebula with a fine, slightly pixelated grain and
  scattered small bright specks, like an upscaled 16-colour sprite background;
- the outer parts may get slightly darker and sparser toward the edges, as galaxy
  outskirts do, but no vignette or hard border.
No seam, line or visible join where the margins meet the original.
No new objects: no planets, ships, asteroids, robots, lens flares, text, logos or frames.
No change to the image height; output the full canvas at the same resolution.
Canvas 1708x960; original centre at x=214..1493; fill 214 pixels on each side.'''


def opaque(source):
    out = Image.new('RGB', source.size, 'black')
    out.paste(source, mask=source.getchannel('A'))
    return out


def prepare(args):
    args.output.mkdir(parents=True, exist_ok=False)
    source = Image.open(args.source).convert('RGBA')
    if source.size != (1280, 960):
        raise ValueError('expected a 1280x960 source')
    canvas = Image.new('RGB', CANVAS, 'black')
    canvas.paste(opaque(source), (LEFT, 0))
    canvas.save(args.output / 'input.png')
    (args.output / 'prompt.txt').write_text(PROMPT + '\n')


def align(generated, source):
    """Coarse-to-fine scale/translation fit on the centre, invariant to channel gains."""
    import numpy as np
    size = (214, 120)
    g = generated.resize(size, Image.Resampling.BILINEAR)
    # Work in 1/8 pixels; constrain the search so unrelated structures cannot win.
    ref = np.asarray(opaque(source).resize((160, 120), Image.Resampling.BILINEAR), dtype=float)[4:-4, 4:-4]
    ref -= ref.mean(axis=(0, 1))
    ref /= np.maximum(ref.std(axis=(0, 1)), 1)

    def score(p):
        sx, sy, tx, ty = p
        crop = g.transform((160, 120), Image.Transform.AFFINE,
                           (sx, 0, (LEFT / 8) * sx + tx / 8, 0, sy, ty / 8), Image.Resampling.BILINEAR)
        a = np.asarray(crop, dtype=float)[4:-4, 4:-4]
        a -= a.mean(axis=(0, 1)); a /= np.maximum(a.std(axis=(0, 1)), 1)
        return float(np.mean((a - ref) ** 2))
    p = [1., 1., 0., 0.]
    initial = score(p)
    for ds, dt in ((.01, 8), (.003, 2), (.001, .5)):
        for _ in range(20):
            options = [p]
            for i, step in enumerate((ds, ds, dt, dt)):
                for direction in (-1, 1):
                    q = p.copy(); q[i] += direction * step
                    if .94 <= q[0] <= 1.06 and .94 <= q[1] <= 1.06 and abs(q[2]) <= 40 and abs(q[3]) <= 40:
                        options.append(q)
            best = min(options, key=score)
            if best == p: break
            p = best
    return p, {'before': initial, 'after': score(p)}


def compose(args):
    import numpy as np
    source = Image.open(args.source).convert('RGBA')
    if source.size != (1280, 960) or not 1 <= args.feather <= LEFT:
        raise ValueError('invalid source size or feather')
    match = re.fullmatch(r'sky-(\d+)-(\d+)\.png', args.source.name)
    if not match: raise ValueError('source filename must be sky-IMAGE-PALETTE.png')
    image, palette = map(int, match.groups())
    generated = Image.open(args.generated).convert('RGBA').resize(CANVAS, Image.Resampling.LANCZOS)
    if args.transparent_margins and generated.getchannel('A').getextrema()[0] == 255:
        raise ValueError('transparent overlay requires a generated image with real alpha')
    transform, scores = align(opaque(generated), source)
    sx, sy, tx, ty = transform
    # Alignment may reach a few pixels beyond the generated raster: extend its edge,
    # rather than introducing PIL's black fill as a new outer border.
    padded = Image.fromarray(np.pad(np.asarray(generated), ((64, 64), (64, 64), (0, 0)), mode='edge'))
    generated = padded.transform(CANVAS, Image.Transform.AFFINE, (sx, 0, tx+64, 0, sy, ty+64), Image.Resampling.BICUBIC)
    a = np.array(generated.convert('RGB'), dtype=float)
    ref = np.array(source.convert('RGB') if args.transparent_margins else opaque(source), dtype=float)
    alpha = np.array(generated.getchannel('A'), dtype=float)
    # Per-channel least-squares gain/offset restores the original's brightness.
    centre = a[:, LEFT:LEFT + 1280]
    valid = np.array(source.getchannel('A')) == 255
    if args.transparent_margins:
        valid &= alpha[:, LEFT:LEFT+1280] > 220
    gain, offset = [], []
    for c in range(3):
        x, y = centre[..., c][valid], ref[..., c][valid]
        slope = float(np.sum((x-x.mean()) * (y-y.mean())) / max(np.sum((x-x.mean())**2), 1))
        intercept = float(y.mean() - slope*x.mean())
        gain.append(slope); offset.append(intercept)
        a[..., c] = a[..., c]*slope + intercept
    # Feather entirely OUTSIDE the original; edge residual fades over the new sides.
    def feather(values, reference):
        for edge, direction, ref_x in ((LEFT, -1, 0), (LEFT + 1279, 1, 1279)):
            delta = reference[:, ref_x] - values[:, edge]
            padded = np.pad(delta, ((8, 8), (0, 0)), mode='edge')
            low = sum(padded[i:i+960] for i in range(17)) / 17
            for d in range(1, args.feather + 1):
                weight = (1 + np.cos(np.pi * (d-1)/args.feather)) / 2
                values[:, edge + direction*d] += low * weight + (delta-low) * np.exp(-(d-1)/3)
    feather(a, ref)
    if args.transparent_margins:
        # The generated alpha fringe can contain saturated key-colour remnants.
        # Keep its lightness, but project colours onto the original overlay's
        # cyan/white ramp, learned only from its opaque source pixels.
        levels = np.clip(ref.mean(2).round().astype(int), 0, 255)
        counts = np.bincount(levels[valid], minlength=256)
        present = counts > 0
        lightness = a.mean(2).clip(0, 255)
        for c in range(3):
            total = np.bincount(levels[valid], weights=ref[..., c][valid], minlength=256)
            ramp = total[present] / counts[present]
            a[..., c] = np.interp(lightness, np.arange(256)[present], ramp)
    result = Image.fromarray(np.clip(a, 0, 255).round().astype('uint8')).convert('RGBA')
    if args.transparent_margins:
        feather(alpha[..., None], np.array(source.getchannel('A'), dtype=float)[..., None])
        result.putalpha(Image.fromarray(np.clip(alpha, 0, 255).round().astype('uint8')))
    result.paste(source, (LEFT, 0))
    if result.crop((LEFT, 0, LEFT + 1280, 960)).tobytes() != source.tobytes():
        raise AssertionError('centre was changed')
    # A new background set, never overwrite an old hardlinked picture.
    base = json.loads((args.base / 'backgrounds.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for row in base['images']:
        row = dict(row)
        target = args.output / row['file']
        if (row['image'], row['palette']) == (image, palette):
            result.save(target)
            row.update(sha256=sha(target), size=list(CANVAS), extent=EXTENT,
                       model='image_gen (built-in; model ID not exposed) + 4x-PixelPerfectV4 centre')
        else:
            os.link(args.base / row['file'], target)
        rows.append(row)
    if sum((r['image'], r['palette']) == (image, palette) for r in rows) != 1:
        raise ValueError('base must contain exactly one matching sky row')
    base['images'] = rows
    (args.output / 'backgrounds.json').write_text(json.dumps(base, indent=2) + '\n')
    def relative(p): return p.resolve().relative_to(ROOT).as_posix()
    prompt_path = args.generated.parent / 'prompt.txt'
    recipe = {'source': relative(args.source), 'source_sha256': sha(args.source),
              'generated': relative(args.generated), 'generated_sha256': sha(args.generated),
              'base': relative(args.base), 'canvas': list(CANVAS), 'centre': [LEFT, 0, LEFT+1280, 960],
              'model': 'image_gen built-in; model ID not exposed',
              'prompt': prompt_path.read_text().strip() if prompt_path.exists() else PROMPT,
              'alignment': dict(zip(('scale_x', 'scale_y', 'translate_x', 'translate_y'), transform)),
              'alignment_normalized_mse': scores, 'colour_gain': gain, 'colour_offset': offset,
              'feather': args.feather, 'feather_location': 'new margins only; centre RGBA exact',
              'feather_lowpass_rows': 17, 'feather_detail_decay_pixels': 3,
              'transparent_margins': args.transparent_margins,
              'source_colour_ramp': args.transparent_margins,
              'extent': EXTENT, 'output_sha256': sha(args.output / args.source.name)}
    (args.generated.parent / 'recipe.json').write_text(json.dumps(recipe, indent=2) + '\n')
    print(json.dumps(recipe, indent=2))
