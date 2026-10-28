#!/usr/bin/env python3
"""Small WebP copies of the HD art the Library pages show.

    .venv/bin/python web/scripts/export_images.py

Reads web/.data/library.json (export_library.py) and writes, under web/public/gen
(git-ignored):

  units/<unit id>.webp          HD battle pose, 320 px square
  portraits/<image>.webp        HD portrait, 160 px (shared with the story pages)
  portraits/<image>-s.webp      the grey silhouette variant some records use: the game
                                fills the portrait's shape with one colour at run time
  icons/<resource>.png          the original 16x16 map icon in the player's colours, from
                                rom.z64 (the MOD makers and ID table; drawn pixelated)

People without HD art (portraits borrowed under another palette) get no image; the
pages show a placeholder. Files newer than their source are kept.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "web/.data/library.json"
GEN = ROOT / "web/public/gen"
UNIT_SIZE, PORTRAIT_SIZE = 320, 160


ROM = ROOT / "rom.z64"
ICONS, PLAYER_PALETTE = range(688, 1010), 1010     # map unit icons (CI4), the player's palette


def export_icons() -> int:
    """The original map icons, once: they only change with the ROM."""
    out = GEN / "icons"
    if not ROM.exists() or (out / f"{ICONS[-1]}.png").exists():
        return 0
    sys.path.insert(0, str(ROOT / "src"))
    from srw64_rom.resources import ResourceTable
    resources = ResourceTable(ROM.read_bytes())
    data = resources.extract(PLAYER_PALETTE)[0]
    kind, size, _, _ = struct.unpack(">4H", data[:8])
    palette = [tuple(round(((v >> s) & 31) * 255 / 31) for s in (11, 6, 1)) + (255 * (v & 1),)
               for (v,) in struct.iter_unpack(">H", data[8:8 + size])]
    out.mkdir(parents=True, exist_ok=True)
    for rid in ICONS:
        data = resources.extract(rid)[0]
        kind, width, height, _ = struct.unpack(">4H", data[:8])
        image = Image.new("RGBA", (width, height))
        image.putdata([palette[v] for b in data[8:8 + width * height // 2] for v in (b >> 4, b & 15)])
        image.save(out / f"{rid}.png", optimize=True)
    return len(ICONS)


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
    made += export_icons()
    print(f"wrote {made} images under {GEN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
