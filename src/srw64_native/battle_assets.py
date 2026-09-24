"""Prepare ROM-owned battle poses, pilot portraits and weapon markers for the native UI.

One PNG per unique binding, with its source ids and digest. Unit poses keep
original pixels and orientation; the UI mirrors the left card at draw time.
The weapon markers are the font's own icon glyphs, cut from the ROM font.
"""
import json
import struct
from functools import lru_cache
from pathlib import Path

from PIL import Image

from srw64_rom.resources import ResourceTable, decode_i4_texture
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
    result = {'schema': 'srw64.battle-assets.v1', 'units': units, 'portraits': portraits,
              'weapon_markers': weapon_markers(resource(FONT_RESOURCE), resource(TEXT_PALETTE), output)}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


# The weapon menu names carry these icon glyphs (reference/original-glyph-map.csv):
# the 格闘 and 射撃 icons in front, P (usable after moving), B (beam) and MAP behind.
# The text keeps 格／射／P／B／MAP as stand-ins; the pages draw these icons for them.
FONT_RESOURCE = 0
TEXT_PALETTE = 2   # the white-text palette (4 is the dimmed one): 1 white, 2-10 greys, 11 the MAP red
MARKER_GLYPHS = {'格': 244, '射': 243, 'P': 241, 'B': 242, 'MAP': 575}
MARKER_SCALE = 8   # nearest-neighbour, so the pixels stay square when the page scales them


def font_tile(font: Image.Image, glyph: int) -> Image.Image:
    """Font resource 0: 8x14 narrow glyphs, 63 a row, then 14x14 wide ones from y 70."""
    if glyph < 0x13B:
        x, y, w = glyph % 63 * 8, glyph // 63 * 14, 8
    else:
        wide = glyph - 0x13B
        x, y, w = wide % 36 * 14, 70 + wide // 36 * 14, 14
    return font.crop((x, y, x + w, y + 14))


def text_palette(resource: bytes) -> list[tuple[int, int, int, int]]:
    """16 RGBA5551 colours after an 8-byte header; index 0 is transparent."""
    colours = struct.unpack_from('>16H', resource, 8)
    return [(round((c >> 11 & 31) * 255 / 31), round((c >> 6 & 31) * 255 / 31),
             round((c >> 1 & 31) * 255 / 31), 255 if c & 1 else 0) for c in colours]


def weapon_markers(font_resource: bytes, palette_resource: bytes, output: Path) -> dict:
    """The icons as the text engine draws them: the font's colour indices through the
    white-text palette. All share the rows the icons use; width and height are original pixels."""
    font, _ = decode_i4_texture(font_resource)
    palette = text_palette(palette_resource)
    # The raw colour indices (the decoder attaches a grey palette to them).
    tiles = {token: Image.frombytes('L', (tile := font_tile(font, glyph)).size, tile.tobytes())
             for token, glyph in MARKER_GLYPHS.items()}
    boxes = {token: tile.getbbox() for token, tile in tiles.items()}
    if not all(boxes.values()):
        raise ValueError('A weapon marker glyph is empty in the ROM font')
    top, bottom = min(b[1] for b in boxes.values()), max(b[3] for b in boxes.values())
    markers = {}
    for token, tile in tiles.items():
        left, _, right, _ = boxes[token]
        indices = tile.crop((left, top, right, bottom))
        icon = Image.new('RGBA', indices.size)
        icon.putdata([palette[v] for v in indices.tobytes()])
        icon = icon.resize((indices.width * MARKER_SCALE, indices.height * MARKER_SCALE), Image.NEAREST)
        path = output / f'marker-{MARKER_GLYPHS[token]}.png'
        path.write_bytes(png_bytes(icon))
        markers[token] = {'path': str(path), 'sha256': sha(path.read_bytes()), 'glyph': MARKER_GLYPHS[token],
                          'width': indices.width, 'height': indices.height}
    return markers
