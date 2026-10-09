#!/usr/bin/env python3
"""Shrink the compiled HD art of a release pack in place (prepare_hd_bundle.py).

The whole images are AI paintings kept as PNG while they are made; at 768 px
(portraits), 8x unit poses and 1920x1440 (backgrounds) that is most of the pack.
The release stores them as JPEG, which the game's loaders already read:

- backgrounds are opaque: one JPEG each;
- a portrait keeps its colour as JPEG (the painted colour under transparent
  pixels too, so edges do not ring) and its alpha beside it as
  "portrait-N.alpha.png", a grey+alpha PNG whose grey is the silhouette colour.
  The game merges the two (src/native/presentation/rgba_file.hpp), and the pages
  show that PNG itself as the silhouette portrait;
- a unit pose is split the same way ("unit-….jpg" + "unit-….alpha.png"), and is
  first scaled from 8x to 6x the ROM pixels, at most 1024 px on its longer side.
  The pre-battle page, the largest place it is drawn, shows a pose at most
  360 dp x 1.15 (the LL share) x the screen density: about 830 px at density 2
  and 980 px at 3. 6x covers that for every size class, and the page draws the
  file without mipmaps, so a larger file only aliases.

Portraits and tactical maps keep their resolution: a portrait is 96 ROM pixels
on screen (about 720-785 px on a full-screen Retina Mac, 864 px at 4K), and the
4x maps are already below a full-screen window's scale.

Quality 92 with 4:2:0 chroma: on 2026-09-28 samples that measured median PSNR
44.2 dB (unit poses), 42.2 (portraits) and 41.2 (tactical map bases), at
21 % / 66 % / 70 % of the size of the PNG or the quality-95 4:4:4 JPEG before it.
The alpha stays exact. The HD tactical maps' painted bases become JPEG too
(meta.json names the file); their palette-index maps stay PNG. RT64 only reads
PNG and DDS, so its textures stay PNG, re-saved without alpha where they are
opaque. BC7 DDS would halve the world map again but measured median 40.7 dB,
min 36.7, and the world map close-ups magnify a texel 19-30 times, so it is not
used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image

QUALITY = 92
SUBSAMPLING = "4:2:0"
UNIT_SCALE = 6  # the pack's unit poses, in ROM pixels (the masters are 8x)
UNIT_LIMIT = 1024  # and at most this on the longer side


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def jpeg(image: Image.Image, path: Path) -> None:
    image.convert("RGB").save(path, "JPEG", quality=QUALITY, subsampling=SUBSAMPLING, optimize=True)


def compress_backgrounds(art: Path) -> int:
    index_path = art / "srw64-backgrounds-hd.json"
    index = json.loads(index_path.read_text())
    for row in index["images"]:
        source = art / row["file"]
        with Image.open(source) as image:
            if image.mode not in ("RGB", "RGBA"):
                raise ValueError(f"Unsupported background mode: {row['file']}")
            target = source.with_suffix(".jpg")
            jpeg(image, target)
            # The battle skies keep their transparent rows (the ground covers them): the
            # alpha beside the colour, as the unit poses (rgba_file.hpp).
            alpha = image.getchannel("A") if image.mode == "RGBA" else None
            if alpha is not None and alpha.getextrema() != (255, 255):
                alpha_file = source.with_suffix(".alpha.png")
                Image.merge("LA", (Image.new("L", image.size, 0), alpha)).save(alpha_file, optimize=True)
                row.update(alpha=alpha_file.relative_to(art).as_posix(), alpha_sha256=sha(alpha_file))
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


def compress_tactical_maps(art: Path) -> int:
    """The HD tactical maps' painted bases are opaque: JPEG, with meta.json naming the file.
    The palette-index maps stay PNG (exact), and so do the translucent colony frames."""
    runtime_path = art / "srw64-tactical-maps.json"
    if not runtime_path.is_file():
        return 0
    root = art / json.loads(runtime_path.read_text())["root"]
    index_path = root / "tactical-maps.json"
    index = json.loads(index_path.read_text())
    for row in index["maps"]:
        folder = root / row["folder"]
        source = folder / "base.png"
        with Image.open(source) as image:
            if image.mode not in ("RGB", "RGBA") or (image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255)):
                raise ValueError(f"Tactical map base is not opaque: {row['folder']}")
            jpeg(image, folder / "base.jpg")
        source.unlink()
        meta = json.loads((folder / "meta.json").read_text())
        meta["base"] = "base.jpg"
        (folder / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n")
        files = {name: digest for name, digest in row["files"].items() if name != "base.png"}
        files.update({"base.jpg": sha(folder / "base.jpg"), "meta.json": sha(folder / "meta.json")})
        row["files"] = files
    index_path.write_text(json.dumps(index, ensure_ascii=False, indent=1) + "\n")
    return len(index["maps"])


def compress_units(art: Path, index_name: str = "srw64-units-hd.json") -> int:
    """Whole HD unit poses (and the frames derived from them, srw64-unit-extras-hd.json):
    scaled to UNIT_SCALE (UNIT_LIMIT at most), then JPEG colour and an alpha PNG beside
    it, as the portraits. The masters fill transparent pixels with the nearest painted
    colour, so colour and alpha scale apart without dark edges."""
    index_path = art / index_name
    if not index_path.is_file():
        return 0
    index = json.loads(index_path.read_text())
    master = index["scale"]
    for row in index["images"]:
        source = art / row["file"]
        with Image.open(source) as image:
            image = image.convert("RGBA")
            factor = min(1.0, UNIT_SCALE / master, UNIT_LIMIT / max(image.size))
            size = (max(1, round(image.width * factor)), max(1, round(image.height * factor)))
            colour, alpha = image.convert("RGB"), image.getchannel("A")
            if size != image.size:
                colour = colour.resize(size, Image.Resampling.LANCZOS)
                alpha = alpha.resize(size, Image.Resampling.LANCZOS)
        target, alpha_file = source.with_suffix(".jpg"), source.with_suffix(".alpha.png")
        jpeg(colour, target)
        Image.merge("LA", (Image.new("L", size, 0), alpha)).save(alpha_file, optimize=True)
        source.unlink()
        row.update(file=target.relative_to(art).as_posix(), sha256=sha(target), width=size[0], height=size[1],
                   alpha=alpha_file.relative_to(art).as_posix(), alpha_sha256=sha(alpha_file))
    index.update(scale=UNIT_SCALE, limit=UNIT_LIMIT)
    index_path.write_text(json.dumps(index, indent=2) + "\n")
    return len(index["images"])


def compress_rt64(art: Path) -> int:
    """RT64 replacement textures stay PNG (RT64 reads PNG and DDS only): an opaque RGBA
    texture is re-saved as RGB, and every RGB(A) one with the smallest zlib stream. Pixels
    and file names do not change, so rt64.json stays as it is."""
    database = art / "rt64.json"
    if not database.is_file():
        return 0
    smaller = 0
    for row in json.loads(database.read_text())["textures"]:
        path = art / row["path"]
        if path.suffix != ".png" or not path.is_file():
            continue
        with Image.open(path) as image:
            if image.mode not in ("RGB", "RGBA"):
                continue
            image.load()
        if image.mode == "RGBA" and image.getchannel("A").getextrema() == (255, 255):
            image = image.convert("RGB")
        trial = path.with_suffix(".tmp.png")
        image.save(trial, optimize=True)
        if trial.stat().st_size < path.stat().st_size:
            trial.replace(path)
            smaller += 1
        else:
            trial.unlink()
    return smaller


def compress(art: Path) -> dict:
    return {"backgrounds": compress_backgrounds(art), "portraits": compress_portraits(art),
            "units": compress_units(art), "unit_extras": compress_units(art, "srw64-unit-extras-hd.json"),
            "battle_sprites": compress_units(art, "srw64-battle-sprites-hd.json"),
            "tactical_maps": compress_tactical_maps(art),
            "rt64_resaved": compress_rt64(art), "jpeg_quality": QUALITY, "jpeg_subsampling": SUBSAMPLING}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("art", type=Path, help="the art/ folder of a prepared HD pack")
    args = parser.parse_args()
    print(json.dumps(compress(args.art)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
