#!/usr/bin/env python3
"""Steam library artwork for the Linux package, from the project's marks
(tools/release/brand): the M64 badge and the Marchwind64 wordmark on the navy plate of
the application icons, lit from behind in the badge's blue.

Writes to --output, for each game language (en, ja, zh-Hans, zh-Hant): capsule-<locale>.png
(600x900, badge over wordmark), wide-<locale>.png (920x430, badge beside wordmark) and
logo-<locale>.png (the wordmark, which Steam draws over the hero); and, the same in
every language, hero.png (3840x1240, the badge on the right, the left kept for the logo)
and icon.png (256x256, app_icons.py's badge on its plate). add_to_steam.py copies them
into Steam's grid folder under the shortcut's app id, in the game's language.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[3]
BRAND = ROOT / 'tools/release/brand'
LOCALES = ('en', 'ja', 'zh-Hans', 'zh-Hant')
TOP, BOTTOM = (34, 46, 80), (7, 9, 18)   # the icon plate's navy (app_icons.mac_icon), deepened
LIGHT = (30, 140, 255)                    # the badge's blue accents


def trimmed(path: Path) -> Image.Image:
    image = Image.open(path).convert('RGBA')
    return image.crop(image.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())


def fit(image: Image.Image, width: float = 1e9, height: float = 1e9) -> Image.Image:
    scale = min(width / image.width, height / image.height)
    return image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)


def plate(width: int, height: int) -> Image.Image:
    # The navy, darker downwards, with faint diagonal seams like the badge's steel plates.
    gradient = Image.linear_gradient('L').resize((width, height))
    canvas = Image.merge('RGB', [gradient.point(lambda v, a=a, b=b: round(a + (b - a) * v / 255))
                                 for a, b in zip(TOP, BOTTOM)]).convert('RGBA')
    seams = Image.new('L', (width, height))
    draw = ImageDraw.Draw(seams)
    for x in range(-height, width + height, max(width, height) // 7):
        draw.line([(x, height), (x + height * 0.55, 0)], fill=28, width=max(2, width // 300))
    return Image.composite(Image.new('RGBA', (width, height), (90, 120, 170, 255)), canvas, seams)


def glow(canvas: Image.Image, cx: int, cy: int, radius: int, strength: int = 150) -> None:
    # Screen-blended light behind the badge.
    mask = Image.new('L', canvas.size)
    ImageDraw.Draw(mask).ellipse((cx - radius, cy - radius * 0.75, cx + radius, cy + radius * 0.75), fill=strength)
    mask = mask.filter(ImageFilter.GaussianBlur(radius * 0.4))
    light = Image.merge('RGB', [mask.point(lambda v, k=k: v * k // 255) for k in LIGHT])
    canvas.paste(ImageChops.screen(canvas.convert('RGB'), light).convert('RGBA'))


def place(canvas: Image.Image, image: Image.Image, cx: int, cy: int, blur: int = 10) -> None:
    # A soft drop shadow lifts the mark off the plate.
    pad = blur * 3
    alpha = Image.new('L', (image.width + 2 * pad, image.height + 2 * pad))
    alpha.paste(image.getchannel('A'), (pad, pad))
    shadow = Image.new('RGBA', alpha.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: v * 200 // 255))
    x, y = cx - image.width // 2, cy - image.height // 2
    canvas.alpha_composite(shadow, (x - pad, y - pad + blur // 2))
    canvas.alpha_composite(image, (x, y))


def steam_icon(size: int = 256) -> Image.Image:
    # The Mac icon's plate with a narrow margin, since Steam shows it small.
    sys.path.insert(0, str(ROOT / 'tools/release'))
    from app_icons import mac_icon
    return mac_icon(1024, inset=24 / 1024).resize((size, size), Image.LANCZOS)


def build(output: Path) -> list[Path]:
    badge = trimmed(BRAND / 'm64.webp')
    output.mkdir(parents=True, exist_ok=True)
    written = []

    def save(name: str, image: Image.Image) -> None:
        image.save(output / name)
        written.append(output / name)

    for locale in LOCALES:
        word = trimmed(BRAND / f'marchwind64-{locale}.webp')
        capsule = plate(600, 900)
        glow(capsule, 300, 360, 330)
        place(capsule, fit(badge, 430, 430), 300, 360, 14)
        place(capsule, fit(word, 540), 300, 720)
        save(f'capsule-{locale}.png', capsule.convert('RGB'))
        wide = plate(920, 430)
        glow(wide, 200, 215, 240)
        place(wide, fit(badge, 300, 300), 200, 215, 12)
        place(wide, fit(word, 500, 190), 620, 215)
        save(f'wide-{locale}.png', wide.convert('RGB'))
        save(f'logo-{locale}.png', fit(word, 1280))
    hero = plate(3840, 1240)
    glow(hero, 2850, 620, 1000, 170)
    place(hero, fit(badge, 960, 960), 2850, 620, 30)
    save('hero.png', hero.convert('RGB'))
    save('icon.png', steam_icon())
    return written


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    for path in build(args.output):
        print(path)


if __name__ == '__main__':
    main()
