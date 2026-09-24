"""Prepare ROM-owned battle poses and pilot portraits for the native UI.

One PNG per unique binding, with its source ids and digest. Unit poses keep
original pixels and orientation; the UI mirrors the left card at draw time.
"""
import json
import struct
from functools import lru_cache
from pathlib import Path

from srw64_rom.resources import ResourceTable
from .battle_graphics import decode_atlas, parse_scene, read_triplets, render_scene
from .catalog import sha
from .original_images import decode_indexed, png_bytes


def prepare_battle_assets(root: Path, rom: bytes, output: Path, hd_portrait=None) -> dict:
    """`hd_portrait(image, palette)` names the whole HD portrait for a pilot, if any."""
    spec = json.loads((root / 'config/data/original-jp-v1.json').read_text())['images']['actors']
    bindings = rom[spec['rom_offset']:spec['rom_offset'] + spec['count'] * spec['stride']]
    if sha(bindings) != spec['sha256']:
        raise ValueError('Battle portrait bindings changed')
    poses = read_triplets(rom, 'unit_poses')
    table = ResourceTable(rom)

    @lru_cache(None)
    def resource(index):
        return table.extract(index)[0]

    output.mkdir(parents=True, exist_ok=False)
    written = {}

    def save(key, pixels, ids):
        if key not in written:
            path = output / (key + '.png')
            path.write_bytes(png_bytes(pixels))
            written[key] = {'path': str(path), 'sha256': sha(path.read_bytes()),
                            'width': pixels.width, 'height': pixels.height, 'resources': list(ids)}
        return written[key]

    units = {}
    for uid, (scene_id, atlas_id, palette_id) in enumerate(poses):
        if not scene_id:
            continue
        scene = parse_scene(resource(scene_id))
        atlas, _ = decode_atlas(resource(atlas_id), resource(palette_id))
        frames, clipped = render_scene(scene, atlas)
        frame = next((f for f, _ in scene.steps if f != 0xFF), 0)
        units[str(uid)] = {**save(f'unit-{scene_id}-{atlas_id}-{palette_id}', frames[frame],
                                  (scene_id, atlas_id, palette_id)), 'facing': 'left', 'frame': frame, 'clipped_parts': clipped}
    portraits = {}
    for actor in range(spec['count']):
        image_id, palette_id = struct.unpack_from('>2H', bindings, actor * spec['stride'])
        entry = save(f'face-{image_id}-{palette_id}', decode_indexed(resource(image_id), resource(palette_id)),
                     (image_id, palette_id))
        hd = hd_portrait(image_id, palette_id) if hd_portrait else None
        portraits[str(actor)] = {**entry, 'hd': hd} if hd else entry
    result = {'schema': 'srw64.battle-assets.v1', 'units': units, 'portraits': portraits}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    return result
