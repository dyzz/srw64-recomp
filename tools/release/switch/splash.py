#!/usr/bin/env python3
"""The Switch's loading screen (src/host/switch/switch_platform.cpp, the boot splash), made when
the folder is staged (package.py): per language a 1280 x 720 picture, the Steam art's navy with
its diagonal lines and the language's logo, and per start step a strip with what is loading,
drawn under the logo while the game starts (the console picks the game's language):

  splash/base-<locale>.png       RGB, 1280 x 720
  splash/step-<locale>-<n>.png   RGBA, 1280 x 64, white text centred

The console reads them before the game's window exists and blits them into its framebuffer.

  tools/release/switch/splash.py OUT_DIR        (package.py calls make())
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
ART = ROOT / 'tools/release/linux/steam-art'
WIDTH, HEIGHT = 1280, 720
# What app::boot_step names on the Switch, in order (run_prepared, host.cpp), and what the
# screen says meanwhile.
STEPS = {
    'zh-Hans': ['正在启动', '正在载入剧情与文本', '正在准备画面', '正在准备界面', '正在启动游戏'],
    'ja': ['起動しています', 'シナリオとテキストを読み込んでいます', '画面を準備しています', 'インターフェースを準備しています', 'ゲームを起動しています'],
    'en': ['Starting', 'Loading the story and text', 'Preparing the screens', 'Preparing the interface', 'Starting the game'],
}


def base(locale: str) -> Image.Image:
    """The navy of the Steam hero, darker on the left, with its faint diagonals; the language's
    logo above the middle."""
    picture = Image.new('RGB', (WIDTH, HEIGHT))
    pixels = picture.load()
    left, right = (10, 14, 34), (30, 86, 160)
    for x in range(WIDTH):
        for y in range(HEIGHT):
            t = min(1.0, max(0.0, x / WIDTH * 0.85 + (1 - y / HEIGHT) * 0.15))
            pixels[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(left, right))
    lines = Image.new('RGBA', (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(lines)
    for x in range(-HEIGHT, WIDTH + HEIGHT, 170):
        draw.line([(x, HEIGHT), (x + HEIGHT * 0.45, 0)], fill=(255, 255, 255, 22), width=2)
    picture = Image.alpha_composite(picture.convert('RGBA'), lines)
    logo = Image.open(ART / f'logo-{locale}.png').convert('RGBA')
    width = 820
    logo = logo.resize((width, round(logo.height * width / logo.width)), Image.LANCZOS)
    picture.alpha_composite(logo, ((WIDTH - logo.width) // 2, 300 - logo.height // 2))
    return picture.convert('RGB')


def strip(text: str, font: ImageFont.FreeTypeFont) -> Image.Image:
    picture = Image.new('RGBA', (WIDTH, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(picture)
    box = draw.textbbox((0, 0), text, font=font)
    x = (WIDTH - (box[2] - box[0])) // 2 - box[0]
    y = (64 - (box[3] - box[1])) // 2 - box[1]
    draw.text((x + 2, y + 2), text, font=font, fill=(0, 0, 0, 140))
    draw.text((x, y), text, font=font, fill=(235, 240, 250, 255))
    return picture


def make(out: Path, fonts: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(fonts / 'HarmonyOS_Sans_SC.ttf'), 30)
    for locale, steps in STEPS.items():
        base(locale).save(out / f'base-{locale}.png', optimize=True)
        for n, text in enumerate(steps):
            strip(text, font).save(out / f'step-{locale}-{n}.png', optimize=True)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    make(Path(sys.argv[1]), ROOT / 'build/fonts')
