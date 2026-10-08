#!/usr/bin/env python3
"""The application icons, from the project's M64 badge (web/public/brand/m64-icon.png).

Writes src/host/windows/Marchwind64.ico (the badge alone, 16 to 256 pixels, compiled into
Marchwind64.exe by marchwind64.rc; SDL gives the window the executable's first icon) and
src/host/macos/Marchwind64.icns (package_macos.py puts it in the app's Resources). A Mac
icon is a rounded square: the badge stands on a dark plate on Apple's 1024 grid (an 824
square, 100 from each edge), so macOS shows it as it is instead of putting an odd shape on
a grey plate. Both outputs are committed; run this again after the badge changes. macOS
only for the .icns (iconutil).
"""
from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
BADGE = ROOT / 'web/public/brand/m64-icon.png'
ICO = ROOT / 'src/host/windows/Marchwind64.ico'
ICNS = ROOT / 'src/host/macos/Marchwind64.icns'
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
# iconutil's names: each size and its @2x.
ICONSET = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)]


def badge() -> Image.Image:
    image = Image.open(BADGE).convert('RGBA')
    return image.crop(image.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())


def square(image: Image.Image) -> Image.Image:
    side = max(image.size)
    canvas = Image.new('RGBA', (side, side))
    canvas.alpha_composite(image, ((side - image.width) // 2, (side - image.height) // 2))
    return canvas


def plate_mask(size: int, inset: float, exponent: float = 5.0) -> Image.Image:
    # A superellipse, close to Apple's continuous corners; drawn 4x and reduced for smooth edges.
    big = size * 4
    lo, hi = inset * big, big - inset * big
    centre, radius = big / 2, (hi - lo) / 2
    mask = Image.new('L', (big, big))
    points = []
    steps = 2048
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        points.append((centre + radius * math.copysign(abs(c) ** (2 / exponent), c),
                       centre + radius * math.copysign(abs(s) ** (2 / exponent), s)))
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def mac_icon(size: int = 1024) -> Image.Image:
    inset = 100 / 1024
    mask = plate_mask(size, inset)
    # The plate: the website's dark navy, lighter at the top, and a thin light rim.
    gradient = Image.linear_gradient('L').resize((size, size))
    top, bottom = (40, 52, 86), (9, 12, 24)
    plate = Image.merge('RGB', [gradient.point(lambda v, a=a, b=b: round(a + (b - a) * v / 255)) for a, b in zip(top, bottom)])
    glow = Image.new('L', (size, size))
    ImageDraw.Draw(glow).ellipse((size * .2, size * .12, size * .8, size * .6), fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(size * .12))
    plate = Image.composite(Image.new('RGB', (size, size), (70, 110, 170)), plate, glow)
    rim = ImageChops.subtract(mask, plate_mask(size, inset + 4 / 1024))
    plate = Image.composite(Image.new('RGB', (size, size), (120, 140, 180)), plate, rim.point(lambda v: v * 45 // 100))
    shadow = mask.filter(ImageFilter.GaussianBlur(size * 10 / 1024)).point(lambda v: v * 50 // 100)
    icon = Image.new('RGBA', (size, size))
    icon.paste((0, 0, 0, 255), (0, round(size * 10 / 1024)), shadow)
    icon.paste(plate.convert('RGBA'), (0, 0), mask)
    # The badge, as large as the plate allows.
    art = badge()
    room = size * (824 - 2 * 56) / 1024
    scale = room / max(art.size)
    art = art.resize((round(art.width * scale), round(art.height * scale)), Image.LANCZOS)
    art = art.filter(ImageFilter.UnsharpMask(radius=1.2, percent=40, threshold=2))
    drop = Image.new('RGBA', art.size, (0, 0, 0, 0))
    drop.putalpha(art.getchannel('A').filter(ImageFilter.GaussianBlur(size * 8 / 1024)).point(lambda v: v * 55 // 100))
    x, y = (size - art.width) // 2, (size - art.height) // 2
    icon.alpha_composite(drop, (x, y + round(size * 8 / 1024)))
    icon.alpha_composite(art, (x, y))
    return icon


def write_ico() -> None:
    art = square(badge())
    ICO.parent.mkdir(parents=True, exist_ok=True)
    art.resize((256, 256), Image.LANCZOS).save(ICO, sizes=[(s, s) for s in ICO_SIZES])


def write_icns() -> None:
    if not shutil.which('iconutil'):
        raise SystemExit('iconutil (macOS) is needed for the .icns')
    master = mac_icon(1024)
    with tempfile.TemporaryDirectory() as work:
        iconset = Path(work) / 'Marchwind64.iconset'
        iconset.mkdir()
        for points, factor in ICONSET:
            name = f'icon_{points}x{points}' + ('@2x' if factor == 2 else '') + '.png'
            master.resize((points * factor,) * 2, Image.LANCZOS).save(iconset / name)
        subprocess.run(['iconutil', '-c', 'icns', '-o', str(ICNS), str(iconset)], check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--preview', type=Path, help='also save the 1024 Mac icon as this PNG')
    args = parser.parse_args()
    write_ico()
    write_icns()
    if args.preview:
        mac_icon(1024).save(args.preview)
    print(ICO.relative_to(ROOT), ICNS.relative_to(ROOT))


if __name__ == '__main__':
    main()
