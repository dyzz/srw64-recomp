"""Fixed-size whole portraits for the host's sprite-mode-7 replacement.

The game draws every portrait as nine 32x32 tiles (800964E4); the host keeps
one rectangle as a marker and paints a single image there (native_portrait.cpp).
The sprite record holds resource handles, not ids, so the host recognises a
portrait by FNV-1a 64 digests of the pixel and palette data the display list
points at; this tool records those digests from the ROM.
Each portrait image gets one SIZE x SIZE PNG covering the 96x96 area the game
draws: the 8x master itself, or its top-left 768 px for a 97 px portrait. The
palette the game loads picks base or silhouette at run time; the four palettes
generic face 134 borrows scramble its colours and stay original.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image

from srw64_rom.resources import ResourceTable
from tools.hd_ai.portrait_batch import SILHOUETTE, portraits
from tools.hd_ai.run_benchmark import ROOT

SIZE = 768
SOURCE = 96


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fnv1a64(data: bytes) -> str:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f'{value:016x}'


def pixel_digest(table: ResourceTable, image_id: int) -> str:
    """Digest of the CI8 index bytes SETTIMG points at (resource body after the 8-byte header)."""
    raw = table.extract(image_id)[0]
    width = struct.unpack_from('>H', raw, 2)[0]
    return fnv1a64(raw[8:8 + width * width])


def palette_digest(table: ResourceTable, palette_id: int) -> str:
    """Digest of the 64 RGBA5551 entries LoadTLUT reads (count 0x3F)."""
    return fnv1a64(table.extract(palette_id)[0][8:8 + 128])


def masters(batch: Path, reviewed: Path) -> dict[int, Path]:
    found = {}
    for grid in json.loads((batch / 'report.json').read_text())['results'].values():
        for cell in grid.get('cells', []):
            if cell['status'] == 'completed':
                found[cell['resource_id']] = batch / cell['master']
    for row in json.loads((reviewed / 'report.json').read_text())['portraits']:
        found[row['resource_id']] = reviewed / f"{row['id']}-master.png"  # the reviewed single redraws win
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--batch', type=Path, required=True, help='portrait_batch output with report.json')
    parser.add_argument('--reviewed', type=Path, required=True, help='portrait_matte output of the reviewed single redraws')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bind', action='store_true', help='point content/art/stage1-hd.json at these portraits')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    sources = masters(args.batch, args.reviewed)
    rows = []
    for row in portraits(rom):
        image_id = row['resource_id']
        master = Image.open(sources[image_id]).convert('RGBA')
        # Masters are 8 px per source px: 768 for a 96 px portrait, 776 for a 97 px one.
        if master.size not in ((SIZE, SIZE), (SIZE + SIZE // SOURCE, SIZE + SIZE // SOURCE)):
            raise ValueError(f'unexpected master size {master.size} for {image_id}')
        name = f'portrait-{image_id}.png'
        master.crop((0, 0, SIZE, SIZE)).save(args.output / name)
        rows.append({'image': image_id, 'palette': row['palette_id'], 'file': name, 'sha256': sha(args.output / name),
                     'pixels_fnv1a64': pixel_digest(table, image_id), 'palette_fnv1a64': palette_digest(table, row['palette_id'])})
    index = {'schema': 'srw64.portrait-images.v1', 'size': SIZE, 'source_size': SOURCE,
             'silhouette': {'palette': SILHOUETTE, 'palette_fnv1a64': palette_digest(table, SILHOUETTE), 'rgb': [41, 41, 41]},
             'left_original': 'image 134 under its borrowed palettes 309/313/392/515 (colours scramble)',
             'images': rows}
    (args.output / 'portraits.json').write_text(json.dumps(index, indent=2) + '\n')
    print({'portraits': len(rows), 'size': SIZE})
    if args.bind:
        path = ROOT / 'content/art/stage1-hd.json'
        manifest = json.loads(path.read_text())
        manifest['textures'] = [r for r in manifest['textures'] if r['kind'] != 'portrait']
        base = ROOT / 'assets/hd-ai/portrait-matte/v2/pack'
        manifest['source'] = {'path': str(base.relative_to(ROOT)), 'manifest_sha256': sha(base / 'rt64.json')}
        manifest['portraits'] = {'path': str(args.output.resolve().relative_to(ROOT)),
                                 'manifest_sha256': sha(args.output / 'portraits.json')}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', path.relative_to(ROOT), len(manifest['textures']), 'textures,', len(rows), 'whole portraits')


if __name__ == '__main__':
    main()
