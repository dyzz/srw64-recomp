"""HD battle skies: every 320x240 CI4 sky picture of the battle backgrounds (the 2D layers
of ROM 0x5BC30's records, 44 pictures in 106 palettes), 4x, for native_background.cpp.

The recipe (2026-10-09, chosen by the user from four): the calm parts are de-dithered a
little (blurred where the local contrast is low, the edges kept), PixelPerfectV4 upscales
three periods side by side so the middle one wraps seamlessly, and a quarter of the
original's nearest-neighbour enlargement goes back over it for the original's grain.
Index 0 stays transparent. The host draws the skies that follow the camera's pitch a
little enlarged so the rows the camera uncovers above them are real picture
(docs/design/battle-animation-rendering.md §5). Still vortex layers 6107/6213 and
6106/6212 have separate image_gen outpaints: wide-prepare and wide-compose retain
the exact source RGBA in the centre (docs/design/battle-vortex-wide.md).

    build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd build --output assets/hd-ai/battle-skies/v1
    build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd build --scroll --output assets/hd-ai/battle-skies/scroll-v1
    build/esrgan-venv/bin/python -m tools.hd_ai.battle_sky_hd build --missing assets/hd-ai/backgrounds/whole-v7 \\
        --output assets/hd-ai/battle-skies/missing-v1
    .venv/bin/python -m tools.hd_ai.battle_sky_hd merge --skies assets/hd-ai/battle-skies/v1 \\
        --base assets/hd-ai/backgrounds/whole-v3 --output assets/hd-ai/backgrounds/whole-v4
    .venv/bin/python -m tools.hd_ai.battle_sky_hd sheet --skies assets/hd-ai/battle-skies/v1
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402

CATALOG = ROOT / 'assets/hd-ai/battle-backgrounds/catalog/catalog.json'
MODELS = ROOT / 'build/esrgan-models'
SCALE = 4
SOURCE = (320, 240)
RECORD_TABLE, RECORD_SIZE, RECORDS = 0x5BC30, 0x26, 2366
SKY_IMAGES = range(6067, 6116)
LIMBS = {6068, 6073}         # the Earth and the Moon: a curved edge against the stars
GRAIN = 0.25                 # the original's nearest enlargement over the model's


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rom_skies(rom: bytes) -> set[tuple[int, int]]:
    """(image, palette) of both sky layers of every record of ROM 0x5BC30."""
    found = set()
    for index in range(RECORDS):
        record = rom[RECORD_TABLE + index * RECORD_SIZE:RECORD_TABLE + (index + 1) * RECORD_SIZE]
        if record[0] > 1:
            continue
        for offset in (0x1A, 0x20):
            image, palette = struct.unpack_from('>2H', record, offset + 2)
            if record[offset] and image in SKY_IMAGES:
                found.add((image, palette))
    return found


def skies(rom: bytes) -> dict[tuple[int, int], set[str]]:
    """(image, palette) of every sky layer, with its terrains where the battle scene catalogue
    has them: the catalogue keeps one record per set of models and first sky, so it does not
    see every palette or second layer (the Earth 6068 and the Moon 6073); the ROM's table does."""
    found: dict[tuple[int, int], set[str]] = {sky: set() for sky in rom_skies(rom)}
    for scene in json.loads(CATALOG.read_text())['sets']:
        for key in ('sky', 'sky2'):
            _, image, palette = scene[key]
            if image:
                found.setdefault((image, palette), set()).add(scene['category'])
    return found


def decode(table: ResourceTable, image: int, palette: int, any_size: bool = False) -> Image.Image:
    data = table.extract(image)[0]
    kind, width, height, _ = struct.unpack_from('>4H', data)
    if kind != 5 or (not any_size and (width, height) != SOURCE):
        raise ValueError(f'resource {image} is not a 320x240 CI4 picture')
    raw = table.extract(palette)[0][8:8 + 32]
    colours = [tuple(round(((v >> s) & 31) * 255 / 31) for s in (11, 6, 1)) for (v,) in struct.iter_unpack('>H', raw)]
    out = bytearray()
    for byte in data[8:8 + width * height // 2]:
        for index in (byte >> 4, byte & 15):
            out += bytes(colours[index]) + bytes([0 if index == 0 else 255])
    return Image.frombytes('RGBA', (width, height), bytes(out))


# The scrolling effect layer (slot 0x19, a special move's streaming aura or nebula behind
# the units): 801C3610(kind, speed) takes its picture and palette from D_80222D98, 8 bytes a
# kind, in overlay load_00121560. Its pictures are 320 or 352 by 256; native_background.cpp
# replaces them like a sky drawn tile by tile, through the extent.
SCROLL_TABLE = 0x121560 + (0x80222D98 - 0x801C2600)
SCROLL_KINDS = 6


def scroll_layers(rom: bytes) -> list[tuple[int, int]]:
    return [struct.unpack_from('>2H', rom, SCROLL_TABLE + 8 * kind) for kind in range(SCROLL_KINDS)]


def build(args: argparse.Namespace) -> None:
    import numpy as np
    import spandrel
    import torch
    from tools.hd_ai.esrgan_pose import fill_transparent, upscale

    def calm_blur(picture: Image.Image, radius=0.5, low=10, high=25) -> Image.Image:
        """Blurred where the picture is calm (dither between near colours), sharp at edges."""
        a = np.asarray(picture).astype(np.float32)
        soft = np.asarray(picture.filter(ImageFilter.GaussianBlur(radius))).astype(np.float32)
        wide = np.asarray(picture.filter(ImageFilter.GaussianBlur(1.5))).astype(np.float32)
        gy, gx = np.gradient(wide.mean(2))
        edge = np.clip((np.hypot(gx, gy) * 3 - low) / (high - low), 0, 1)[..., None]
        return Image.fromarray((a * edge + soft * (1 - edge)).round().astype(np.uint8))

    device = torch.device('mps' if torch.backends.mps.is_available() else 'cpu')
    model = spandrel.ModelLoader().load_from_file(str(MODELS / '4x-PixelPerfectV4.pth')).model.eval().to(device)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    args.output.mkdir(parents=True, exist_ok=True)

    def limb(source: Image.Image) -> np.ndarray:
        """The alpha of a planet: its stepped edge as one smooth curve, a pixel or two soft."""
        width, height = source.size
        top = (np.asarray(source.getchannel('A')) > 0).argmax(0).astype(np.float64)
        columns = np.arange(width) + 0.5
        fit = np.polyfit(columns, top, 6)
        print(f'limb: worst step off the curve {np.abs(np.polyval(fit, columns) - top).max():.2f} px', flush=True)
        edge = np.polyval(fit, (np.arange(width * SCALE) + 0.5) / SCALE) * SCALE
        rows = np.arange(height * SCALE)[:, None] + 0.5
        return (np.clip((rows - edge[None, :]) / 2 + 0.5, 0, 1) * 255).round().astype(np.uint8)

    def enlarge(source: Image.Image, path: Path, smooth_edge: bool = False) -> None:
        width, height = source.size
        rgb = fill_transparent(source)
        strip = Image.new('RGB', (3 * width, height))
        for i in range(3):
            strip.paste(calm_blur(rgb), (i * width, 0))
        hd = np.asarray(upscale(model, strip, device)).astype(np.float32)[:, width * SCALE:2 * width * SCALE]
        grain = np.asarray(rgb.resize((width * SCALE, height * SCALE), Image.NEAREST)).astype(np.float32)
        colour = np.clip(hd * (1 - GRAIN) + grain * GRAIN, 0, 255).round().astype(np.uint8)
        alpha = limb(source) if smooth_edge else np.asarray(source.getchannel('A').resize((width * SCALE, height * SCALE), Image.NEAREST))
        Image.fromarray(np.concatenate([colour, alpha[..., None]], -1), 'RGBA').save(path)

    rows = []
    if args.scroll:
        pictures = [((image, palette), None) for image, palette in scroll_layers(rom)]
    else:
        pictures = sorted(skies(rom).items())
        if args.missing:
            have = {(r['image'], r['palette']) for r in json.loads((args.missing / 'backgrounds.json').read_text())['images']}
            pictures = [row for row in pictures if row[0] not in have]
    for (image, palette), terrains in pictures:
        name = f"{'scroll' if args.scroll else 'sky'}-{image}-{palette}.png"
        source = decode(table, image, palette, any_size=args.scroll)
        enlarge(source, args.output / name, not args.scroll and image in LIMBS)
        row = {'image': image, 'palette': palette, 'file': name, 'sha256': sha(args.output / name),
               'model': '4x-PixelPerfectV4', 'size': [source.width * SCALE, source.height * SCALE]}
        if args.scroll:
            row['extent'] = [0, 0, source.width, source.height]
        else:
            row['terrains'] = sorted(terrains)
        rows.append(row)
        print(name, flush=True)
    index = {'schema': 'srw64.battle-sky-images.v1', 'recipe': 'calm-blur 0.5 / PixelPerfectV4 4x wrapped / 25% nearest grain',
             'source_size': list(SOURCE), 'images': rows}
    (args.output / 'skies.json').write_text(json.dumps(index, ensure_ascii=False, indent=2) + '\n')
    print(len(rows), 'pictures in', args.output)


def merge(args: argparse.Namespace) -> None:
    """A background set: the base set's rows (intermission pictures) and the skies."""
    base = json.loads((args.base / 'backgrounds.json').read_text())
    skies_index = json.loads((args.skies / 'skies.json').read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for row in base['images']:
        os.link(args.base / row['file'], args.output / row['file'])
        rows.append(row)
    taken = {(r['image'], r['palette']) for r in rows}
    for row in skies_index['images']:
        if (row['image'], row['palette']) in taken:
            raise ValueError(f"sky {row['image']}/{row['palette']} already in the base set")
        os.link(args.skies / row['file'], args.output / row['file'])
        rows.append({k: row[k] for k in ('image', 'palette', 'file', 'sha256', 'model', 'size', 'extent') if k in row})
    index = {'schema': 'srw64.background-images.v1', 'size': base['size'], 'source_size': base['source_size'], 'images': rows}
    (args.output / 'backgrounds.json').write_text(json.dumps(index, indent=2) + '\n')
    print(len(rows), 'rows in', args.output, 'manifest_sha256', sha(args.output / 'backgrounds.json'))


def sheet(args: argparse.Namespace) -> None:
    """A contact sheet of every sky over a checkerboard, for review."""
    index = json.loads((args.skies / 'skies.json').read_text())
    tw, th, columns = 320, 240, 5
    rows = (len(index['images']) + columns - 1) // columns
    out = Image.new('RGB', (columns * (tw + 8), rows * (th + 18)), 'white')
    draw = ImageDraw.Draw(out)
    board = Image.new('RGBA', (tw, th), (200, 200, 200, 255))
    bd = ImageDraw.Draw(board)
    for y in range(0, th, 12):
        for x in range(0, tw, 12):
            if (x // 12 + y // 12) % 2:
                bd.rectangle([x, y, x + 11, y + 11], fill=(150, 150, 150, 255))
    for i, row in enumerate(index['images']):
        tile = board.copy()
        tile.alpha_composite(Image.open(args.skies / row['file']).resize((tw, th), Image.LANCZOS))
        x, y = (i % columns) * (tw + 8), (i // columns) * (th + 18)
        out.paste(tile.convert('RGB'), (x, y + 16))
        draw.text((x + 2, y + 2), f"{row['image']}/{row['palette']}", fill='black')
    out.save(args.skies / 'sheet.png')
    print(args.skies / 'sheet.png')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('build')
    command.add_argument('--output', type=Path, required=True)
    command.add_argument('--scroll', action='store_true', help='the scrolling effect layer pictures instead of the skies')
    command.add_argument('--missing', type=Path, help='only the skies this background set does not have')
    command = commands.add_parser('merge')
    command.add_argument('--skies', type=Path, required=True)
    command.add_argument('--base', type=Path, required=True)
    command.add_argument('--output', type=Path, required=True)
    command = commands.add_parser('sheet')
    command.add_argument('--skies', type=Path, required=True)
    from tools.hd_ai import battle_sky_wide
    for name in ('wide-prepare', 'wide-compose'):
        command = commands.add_parser(name)
        command.add_argument('--source', type=Path, default=battle_sky_wide.SOURCE)
        command.add_argument('--output', type=Path, required=True)
        if name == 'wide-compose':
            command.add_argument('--generated', type=Path, required=True)
            command.add_argument('--base', type=Path, default=ROOT / 'assets/hd-ai/backgrounds/whole-v4')
            command.add_argument('--feather', type=int, default=48)
            command.add_argument('--transparent-margins', action='store_true',
                                 help='preserve generated alpha in the new sides of an overlay sky')
    args = parser.parse_args()
    {'build': build, 'merge': merge, 'sheet': sheet,
     'wide-prepare': battle_sky_wide.prepare, 'wide-compose': battle_sky_wide.compose}[args.command](args)


if __name__ == '__main__':
    main()
