#!/usr/bin/env python3
"""Shrink the compiled HD art of a release pack in place (prepare_hd_bundle.py).

The whole images are AI paintings kept as PNG while they are made; at 768 px
(portraits) and 1920x1440 (backgrounds) that is most of the pack. The release
stores them as JPEG, which the game's loaders already read:

- backgrounds are opaque: one JPEG each;
- a portrait keeps its colour as JPEG (the painted colour under transparent
  pixels too, so edges do not ring) and its alpha beside it as
  "portrait-N.alpha.png", a grey+alpha PNG whose grey is the silhouette colour.
  The game merges the two (src/native/presentation/rgba_file.hpp), and the pages
  show that PNG itself as the silhouette portrait.

Quality 95 without chroma subsampling measured PSNR >= 46.9 dB on the
backgrounds; the alpha stays exact. RT64 textures and the title frames stay PNG.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

QUALITY = 95


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def jpeg(image: Image.Image, path: Path) -> None:
    image.convert("RGB").save(path, "JPEG", quality=QUALITY, subsampling=0, optimize=True)


def compress_backgrounds(art: Path) -> int:
    index_path = art / "srw64-backgrounds-hd.json"
    index = json.loads(index_path.read_text())
    for row in index["images"]:
        source = art / row["file"]
        with Image.open(source) as image:
            if image.mode not in ("RGB", "RGBA") or (image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255)):
                raise ValueError(f"Background is not opaque: {row['file']}")
            target = source.with_suffix(".jpg")
            jpeg(image, target)
        source.unlink()
        row.update(file=target.relative_to(art).as_posix(), sha256=sha(target))
    index_path.write_text(json.dumps(index, indent=2) + "\n")
    return len(index["images"])


def compress_portraits(art: Path) -> int:
    index_path = art / "srw64-portraits-hd.json"
    index = json.loads(index_path.read_text())
    grey = index["silhouette"]["rgb"]
    if len(set(grey)) != 1:
        raise ValueError("The silhouette colour must be a grey to live in a grey+alpha PNG")
    for row in index["images"]:
        source = art / row["file"]
        with Image.open(source) as image:
            image = image.convert("RGBA")
            target, alpha = source.with_suffix(".jpg"), source.with_suffix(".alpha.png")
            jpeg(image, target)
            Image.merge("LA", (Image.new("L", image.size, grey[0]), image.getchannel("A"))).save(alpha, optimize=True)
        source.unlink()
        row.update(file=target.relative_to(art).as_posix(), sha256=sha(target),
                   alpha=alpha.relative_to(art).as_posix(), alpha_sha256=sha(alpha))
    index_path.write_text(json.dumps(index, indent=2) + "\n")
    return len(index["images"])


def compress(art: Path) -> dict:
    return {"backgrounds": compress_backgrounds(art), "portraits": compress_portraits(art), "jpeg_quality": QUALITY}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("art", type=Path, help="the art/ folder of a prepared HD pack")
    args = parser.parse_args()
    print(json.dumps(compress(args.art)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
