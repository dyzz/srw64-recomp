"""The battle's landing shadow in HD: a soft-edged disc in place of the original's hard one.

801C28E0 puts the shadow under a unit as sprite sub 1: atlas 0x1580 (5504, a 32x32 4-bit
disc) with palette 0x1581 (5505: index 0 clear, 1 black), its depth from the PRIM alpha,
loaded by 8009C004 -> 8009BD68 with LOADBLOCK, so RT64 replaces it by its texture hash
(docs/design/battle-animation-rendering.md §16). The HD picture is drawn, not upscaled:
black, opaque inside the original disc's own radius and fading out to it, so it covers no
more ground than the original (HD effects keep the original's footprint).

    .venv/bin/python tools/hd_ai/battle_shadow_hd.py --into assets/hd-ai/worldmap-surfaces/pack-vN [--bind]

`--into` hard-links the bound texture pack (the art manifest's `source`) into a new folder
and adds the shadow; `--bind` points the manifest at it and lists the texture.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402

MANIFEST = ROOT / 'content/art/stage1-hd.json'
ATLAS = 5504
# RT64 v5 hash of the shadow as the battle loads it (taken from a --dump-textures run:
# the dumped TMEM matches resource 5504 row for row; the palette is fixed, so one hash).
HASH = 'ba56a5d567d933cb'
SCALE = 8
SOFT = 0.45        # the outer part of the radius that fades


def picture() -> Image.Image:
    data = ResourceTable((ROOT / 'rom.z64').read_bytes()).extract(ATLAS)[0]
    width, height = int.from_bytes(data[2:4], 'big'), int.from_bytes(data[4:6], 'big')
    pixels = np.frombuffer(data[8:8 + width * height // 2], np.uint8)
    index = np.stack([pixels >> 4, pixels & 15], 1).reshape(height, width)
    # The disc's centre and radius from its own pixels (in pixel-centre coordinates).
    ys, xs = np.nonzero(index)
    cx, cy = (xs.min() + xs.max() + 1) / 2, (ys.min() + ys.max() + 1) / 2
    radius = ((xs.max() + 1 - xs.min()) + (ys.max() + 1 - ys.min())) / 4
    size = width * SCALE
    grid = (np.arange(size) + 0.5) / SCALE
    distance = np.hypot(grid[None, :] - cx, grid[:, None] - cy) / radius
    t = np.clip((1 - distance) / SOFT, 0, 1)
    alpha = (t * t * (3 - 2 * t) * 255).round().astype(np.uint8)
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 3] = alpha
    return Image.fromarray(rgba, 'RGBA')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--into', type=Path, required=True, help='the new texture pack folder')
    parser.add_argument('--bind', action='store_true', help='point the art manifest at it')
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    current = ROOT / manifest['source']['path']
    into = args.into.absolute()
    into.mkdir(parents=True, exist_ok=False)
    for path in current.iterdir():
        if path.is_file() and path.name != 'rt64.json':
            os.link(path, into / path.name)
    database = json.loads((current / 'rt64.json').read_text())
    name = f'battle-{HASH}.png'
    if any(entry['hashes']['rt64'] == HASH for entry in database['textures']):
        raise SystemExit(f'{HASH} is already in {current.name}')
    picture().save(into / name, optimize=True)
    database['textures'].append({'hashes': {'rt64': HASH}, 'path': name})
    database['textures'].sort(key=lambda entry: entry['hashes']['rt64'])
    (into / 'rt64.json').write_text(json.dumps(database, indent=2) + '\n')
    digest = hashlib.sha256((into / 'rt64.json').read_bytes()).hexdigest()
    print(name, 'added to', current.name, '->', into.name, 'rt64.json', digest)
    if args.bind:
        manifest['source'] = {'path': str(into.relative_to(ROOT)), 'manifest_sha256': digest}
        manifest['textures'].append({'hash': HASH, 'kind': 'battle', 'sha256': hashlib.sha256((into / name).read_bytes()).hexdigest()})
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT))


if __name__ == '__main__':
    main()
