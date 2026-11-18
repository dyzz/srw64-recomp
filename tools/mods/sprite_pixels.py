#!/usr/bin/env python3
"""Make a package sprite's original-size picture from a larger one (docs/design/new-unit-ids.md
§11): scaled to the size the game draws, alpha cut at half, and reduced to the 127 colours a
sprite may have (data/new_units.json "sprites", src/host/mod_sprites.hpp: RGBA5551, entry 0
transparent).

    python tools/mods/sprite_pixels.py stand-hd.png stand.png --size 128 96
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image

COLORS = 127


def five(v: int) -> int:
    """A channel as RGBA5551 keeps it, back in eight bits."""
    return (v >> 3) * 255 // 31


def pixels(source: Image.Image, size: tuple[int, int], colors: int = COLORS) -> Image.Image:
    rgba = source.convert("RGBA")
    # Scale with the colour premultiplied, so the transparent surround does not bleed in.
    premultiplied = Image.new("RGBA", rgba.size)
    premultiplied.paste(rgba.convert("RGBa"))
    small = premultiplied.resize(size, Image.LANCZOS)
    small = Image.frombytes("RGBa", size, small.tobytes()).convert("RGBA")
    alpha = small.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    paletted = small.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = paletted.getpalette()[:colors * 3]
    reduced = [five(v) for v in palette]
    out = Image.new("RGBA", size)
    data = []
    index = paletted.tobytes()
    a = alpha.tobytes()
    for i, k in enumerate(index):
        r, g, b = reduced[k * 3:k * 3 + 3]
        data.append((r, g, b, a[i]) if a[i] else (0, 0, 0, 0))
    out.putdata(data)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--size", type=int, nargs=2, required=True, metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--colors", type=int, default=COLORS)
    args = parser.parse_args()
    image = pixels(Image.open(args.source), tuple(args.size), min(args.colors, COLORS))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out, optimize=True)
    raw = image.tobytes()
    opaque = {raw[i:i + 4] for i in range(0, len(raw), 4) if raw[i + 3]}
    print(f"{args.out}: {image.width}x{image.height}, {len(opaque)} colours")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
