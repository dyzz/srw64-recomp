#!/usr/bin/env python3
"""Small WebP copies of the HD art the Library pages show.

    .venv/bin/python web/scripts/export_images.py

Reads web/.data/library.json (export_library.py) and writes, under web/public/gen
(git-ignored):

  units/<unit id>.webp          HD battle pose, 320 px square
  portraits/<image>.webp        HD portrait, 160 px (shared with the story pages)
  portraits/<image>-s.webp      the grey silhouette variant some records use: the game
                                fills the portrait's shape with one colour at run time

People without HD art (portraits borrowed under another palette) get no image; the
pages show a placeholder. Files newer than their source are kept.
"""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "web/.data/library.json"
GEN = ROOT / "web/public/gen"
UNIT_SIZE, PORTRAIT_SIZE = 320, 160


def fresh(dst: Path, src: Path) -> bool:
    return dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime


def main() -> int:
    data = json.loads(DATA.read_text())
    (GEN / "units").mkdir(parents=True, exist_ok=True)
    (GEN / "portraits").mkdir(parents=True, exist_ok=True)
    made = 0
    for unit in data["units"]:
        hd = unit.get("image", {}).get("hd")
        if not hd:
            continue
        src, dst = ROOT / hd, GEN / f"units/{unit['id']}.webp"
        if fresh(dst, src):
            continue
        Image.open(src).convert("RGBA").resize((UNIT_SIZE, UNIT_SIZE), Image.LANCZOS).save(dst, "WEBP", quality=82, method=6)
        made += 1
    for person in data["people"]:
        p = person.get("portrait") or {}
        if not p.get("hd"):
            continue
        src = ROOT / p["hd"]
        rgb = p.get("silhouette_rgb")
        dst = GEN / f"portraits/{p['image']}{'-s' if rgb else ''}.webp"
        if fresh(dst, src):
            continue
        im = Image.open(src).convert("RGBA").resize((PORTRAIT_SIZE, PORTRAIT_SIZE), Image.LANCZOS)
        if rgb:
            flat = Image.new("RGBA", im.size, (*rgb, 255))
            flat.putalpha(im.getchannel("A"))
            im = flat
        im.save(dst, "WEBP", quality=82, method=6)
        made += 1
    print(f"wrote {made} images under {GEN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
