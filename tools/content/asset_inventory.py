#!/usr/bin/env python3
"""Inventory every ROM resource for HD planning: header kind, size and category.

Header kinds are read from the decoded resource. Categories come from resource
ranges: binding tables where the docs name them, contact-sheet inspection for
the rest (marked "inspected"). A range covers images, their palettes and the
scene/descriptor data between them; each resource keeps its own header kind.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from srw64_rom.resources import ResourceTable

ROOT = Path(__file__).resolve().parents[2]
ROM_SHA256 = "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e"
GEOMETRY_MAGIC = bytes.fromhex("36340038")

# (first, last, category, has_baked_text, source)
# source: "table" = a pinned binding table or extractor names the range;
#         "inspected" = identified from rendered contact sheets only.
RANGES = [
    (0, 8, "font", False, "table"),
    (9, 609, "portrait", False, "table"),
    (610, 614, "title.speedlines", False, "inspected"),
    (615, 616, "system.game-over", True, "inspected"),
    (617, 619, "title.publisher-logo", True, "inspected"),
    (620, 622, "title.copyright", True, "inspected"),
    (623, 655, "title.menu-and-series-text", True, "inspected"),
    (656, 680, "title.robot-name-text", True, "inspected"),
    (681, 683, "title.logo", True, "inspected"),
    (684, 686, "title.fire-background", False, "inspected"),
    (687, 1015, "map.unit-icon", False, "table"),
    (1016, 1158, "data", False, "inspected"),
    (1159, 1160, "battle.hud-labels", True, "inspected"),
    (1161, 1294, "data", False, "inspected"),
    (1295, 1295, "ui.window-and-hud-atlas", True, "inspected"),
    (1296, 1301, "ui.frame-strip", True, "table"),
    (1302, 1307, "ui.cursor-arrows", False, "inspected"),
    (1308, 1336, "portrait.protagonist", False, "inspected"),
    (1337, 1471, "battle.cutin", False, "table"),
    (1472, 1611, "map.movie", False, "table"),
    (1612, 2476, "battle.unit-sprite", False, "table"),
    (2477, 3005, "battle.weapon-sprite", False, "inspected"),
    (3006, 4257, "battle.effect", False, "table"),
    (4258, 4364, "battle.shield", False, "table"),
    (4365, 4987, "battle.effect-extra", False, "inspected"),
    (4988, 5105, "chapter-title", True, "table"),
    (5106, 5334, "data", False, "inspected"),
    (5335, 5374, "effect.fragment-atlas", False, "inspected"),
    (5375, 5469, "data", False, "inspected"),
    (5470, 5493, "intermission.background", False, "table"),
    (5494, 5505, "ui.misc", False, "inspected"),
    (5506, 5543, "intro.text-page", True, "table"),
    (5544, 5569, "ending.staff-roll", True, "inspected"),
    (5570, 5581, "ending.epilogue-page", True, "inspected"),
    (5582, 5583, "ending.starfield", False, "inspected"),
    (5584, 6066, "geometry", False, "table"),
    (6067, 6227, "battle.3d-background-texture", False, "table"),
    (6228, 6263, "map.tileset", False, "table"),
    (6264, 6418, "map.layout", False, "table"),
    (6419, 6435, "map.auxiliary", False, "table"),
]


def classify(data: bytes) -> dict:
    head = data[:8].ljust(8, b"\0")
    kind, a, b, c = struct.unpack(">4H", head)
    if kind == 3 and len(data) == 8 + a:
        return {"kind": "palette", "colors": a // 2}
    if kind in (5, 14) and c == 0 and len(data) == 8 + (a * b + 1) // 2:
        return {"kind": "image4", "type": kind, "width": a, "height": b}
    if kind in (6, 7, 8, 15) and c == 0 and len(data) == 8 + a * b:
        return {"kind": "image8", "type": kind, "width": a, "height": b}
    if kind == 7 and a and 0 < b <= 256 and 0 < c <= 256 and len(data) >= 8 + b * c + a * 6:
        return {"kind": "map-layout", "groups": a, "width": b * 8, "height": c * 8}
    if head[:4] == GEOMETRY_MAGIC:
        return {"kind": "geometry"}
    return {"kind": "data", "header": head[:2].hex()}


def category(resource_id: int) -> tuple[str, bool, str]:
    for first, last, name, text, source in RANGES:
        if first <= resource_id <= last:
            return name, text, source
    raise ValueError(f"resource {resource_id} is outside the category table")


def inventory(rom: bytes) -> list[dict]:
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("ROM differs from the pinned JP Rev 0 image")
    table = ResourceTable(rom)
    if RANGES[0][0] != 0 or RANGES[-1][1] != table.count - 1 or any(
            left[1] + 1 != right[0] for left, right in zip(RANGES, RANGES[1:])):
        raise ValueError("category ranges must tile every resource exactly once")
    rows = []
    for resource_id in range(table.count):
        data, _ = table.extract(resource_id)
        name, text, source = category(resource_id)
        rows.append({"id": resource_id, "category": name, "baked_text": text, "source": source,
                     "size": len(data), "sha256": hashlib.sha256(data).hexdigest(), **classify(data)})
    return rows


def summary(rows: list[dict]) -> list[dict]:
    out = []
    for first, last, name, _, _ in RANGES:
        items = rows[first:last + 1]
        images = [r for r in items if r["kind"] in ("image4", "image8")]
        sizes = Counter(f"{'CI4' if r['kind'] == 'image4' else 'CI8'} {r['width']}x{r['height']}" for r in images)
        out.append({
            "category": name,
            "first": items[0]["id"], "last": items[-1]["id"],
            "images": len(images),
            "unique_images": len({r["sha256"] for r in images}),
            "palettes": sum(r["kind"] == "palette" for r in items),
            "geometry": sum(r["kind"] == "geometry" for r in items),
            "layouts": sum(r["kind"] == "map-layout" for r in items),
            "data": sum(r["kind"] == "data" for r in items),
            "pixels": sum(r["width"] * r["height"] for r in images),
            "common_sizes": [f"{size} ×{count}" for size, count in sizes.most_common(3)],
            "baked_text": items[0]["baked_text"], "source": items[0]["source"],
        })
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=ROOT / "build/content/asset-inventory.json")
    args = parser.parse_args()
    rows = inventory(args.rom.read_bytes())
    groups = summary(rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"schema": "srw64.asset-inventory.v0", "rom_sha256": ROM_SHA256,
                                       "categories": groups, "resources": rows}, ensure_ascii=False, indent=1) + "\n")
    kinds = Counter(r["kind"] for r in rows)
    print(f"{len(rows)} resources: " + ", ".join(f"{k} {n}" for k, n in kinds.most_common()))
    for group in groups:
        if group["images"] or group["geometry"] or group["layouts"]:
            print(f"{group['first']:5d}-{group['last']:5d} {group['category']:32s} images {group['images']:4d} "
                  f"(unique {group['unique_images']:4d}) palettes {group['palettes']:4d} "
                  f"geometry {group['geometry']:3d} layouts {group['layouts']:3d}  {', '.join(group['common_sizes'])}")
    print(f"wrote {args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output}")


if __name__ == "__main__":
    main()
