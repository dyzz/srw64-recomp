#!/usr/bin/env python3
"""Steam library artwork for the Linux package: the HD title logo over the title flames,
with the project's MARCHWIND64 logo under it.

Writes capsule.png (600x900), wide.png (920x430), hero.png (3840x1240), logo.png
(Steam draws it over the hero) and icon.png (256x256) to --output; add_to_steam.py
copies them into Steam's grid folder under the shortcut's app id. The sources are the
HD pack's title images (content/art/stage1-hd.json scene_images) and the project's
brand images in web/public/brand (the English title logo and the M64 icon). The art is
the same in every language; the Steam name follows the game's.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[3]
FLAME = 5  # one frame, tiled: the flame frames repeat seamlessly side by side


def sources(root: Path) -> Path:
    manifest = json.loads((root / 'content/art/stage1-hd.json').read_text(encoding='utf-8'))
    return root / manifest['scene_images']['path']


def background(width: int, height: int) -> Image.Image:
    # Near black, warming towards the flames.
    top, bottom = (10, 11, 16), (40, 14, 6)
    column = Image.new('RGB', (1, height))
    column.putdata([tuple(round(a + (b - a) * (y / max(1, height - 1)) ** 2.2) for a, b in zip(top, bottom))
                    for y in range(height)])
    return column.resize((width, height), Image.NEAREST).convert('RGBA')


def flames(canvas: Image.Image, frame: Image.Image, height: int) -> None:
    width = round(frame.width * height / frame.height)
    tile = frame.resize((width, height), Image.LANCZOS)
    x = (canvas.width // 2 - width // 2) % width - width
    while x < canvas.width:
        canvas.alpha_composite(tile, (x, canvas.height - height))
        x += width


def scaled(image: Image.Image, width: int) -> Image.Image:
    return image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)


def place(canvas: Image.Image, image: Image.Image, cx: int, cy: int, blur: int = 12) -> None:
    # A soft drop shadow lifts the logo off the flames.
    pad = blur * 3
    alpha = Image.new('L', (image.width + 2 * pad, image.height + 2 * pad))
    alpha.paste(image.getchannel('A'), (pad, pad))
    shadow = Image.new('RGBA', alpha.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: v * 200 // 255))
    x, y = cx - image.width // 2, cy - image.height // 2
    canvas.alpha_composite(shadow, (x - pad, y - pad + blur // 2))
    canvas.alpha_composite(image, (x, y))


def build(output: Path, root: Path = ROOT) -> list[Path]:
    images = sources(root)
    logo = Image.open(images / 'title-logo.png').convert('RGBA')
    brand = Image.open(root / 'web/public/brand/title-en.webp').convert('RGBA')
    badge = Image.open(root / 'web/public/brand/m64-icon.png').convert('RGBA')
    frame = Image.open(images / f'title-flame-{FLAME:02d}.png').convert('RGBA')
    output.mkdir(parents=True, exist_ok=True)
    written = []

    def save(name: str, image: Image.Image) -> None:
        image.save(output / name)
        written.append(output / name)

    capsule = background(600, 900)
    flames(capsule, frame, 560)
    place(capsule, scaled(logo, 560), 300, 290)
    place(capsule, scaled(brand, 400), 300, 455, blur=8)
    save('capsule.png', capsule.convert('RGB'))
    wide = background(920, 430)
    flames(wide, frame, 300)
    place(wide, scaled(logo, 760), 460, 150)
    place(wide, scaled(brand, 330), 460, 318, blur=6)
    save('wide.png', wide.convert('RGB'))
    hero = background(3840, 1240)
    flames(hero, frame, 900)
    save('hero.png', hero.convert('RGB'))
    save('logo.png', scaled(logo, 1280))
    # The icon: the project's M64 badge, as on the website.
    save('icon.png', badge.resize((256, 256), Image.LANCZOS))
    return written


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    for path in build(args.output):
        print(path)


if __name__ == '__main__':
    main()
