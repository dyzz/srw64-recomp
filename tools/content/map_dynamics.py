#!/usr/bin/env python3
"""List every tactical map with its runtime dynamics for HD planning.

ROM facts: the 158 map records (layout, atlas, palette, two palette-cycle
resources, mode byte), each layout composed as palette indices, the cycle
resources' colour ranges, the colony frames of mode-1 space maps, and which
maps share a layout. Stage links (initial map per scene, script 3D34 switches)
come from the extracted catalogue in assets/original-data when it exists.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import glob
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageOps

from srw64_rom.resources import ResourceTable

ROOT = Path(__file__).resolve().parents[2]
ROM_SHA256 = "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e"
SWITCH_OPCODE = 0x3D34
BORDER_TILES = {1, 2}                  # outer two-cell frame, skipped by the overview path
COLONY_ATLAS, COLONY_RECT = 6229, (208, 0, 272, 48)   # 64x48 region rewritten every 27 ticks
# Palette-cycle resources by colour family. Only the water family is confirmed on screen
# (map 20's river); the others are named from their colours and the maps that use them.
CYCLE_NAMES = {
    **{rid: "水面" for rid in range(6419, 6426)},
    6426: "暗绿循环", 6427: "绿色慢循环", 6428: "橙褐循环", 6429: "红色脉动", 6430: "红色脉动",
    6431: "深蓝快闪", 6432: "黄色循环", 6433: "红褐快循环", 6434: "红褐循环", 6435: "橙色循环",
}


def table(name: str) -> dict:
    spec = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
    return next(t for t in spec["tables"] if t["id"] == name)


def cycle_info(data: bytes) -> dict:
    frames, start, count = data[0], data[1], data[2]
    ticks = list(data[3:3 + frames])
    if len(data) < 3 + frames + count * frames * 2:
        raise ValueError("palette-cycle resource shorter than its header")
    return {"first_index": start, "colors": count, "frames": frames, "cycle_ticks": sum(ticks)}


def index_atlas(data: bytes) -> Image.Image:
    kind, width, height, _ = struct.unpack(">4H", data[:8])
    if kind not in (6, 7, 8, 15) or len(data) != 8 + width * height:
        raise ValueError("map atlas is not an 8-bit indexed image")
    return Image.frombytes("L", (width, height), data[8:])


def compose(layout: bytes, atlas: Image.Image) -> tuple[Image.Image, Counter, Counter]:
    """Place 16x16 atlas cells like 800945D4; returns indices, tile uses and atlas source cells."""
    _, groups, width, height = struct.unpack(">4H", layout[:8])
    image = Image.new("L", (width * 8, height * 8))
    tiles, sources = Counter(), Counter()
    start = 8 + width * height
    for group in range(groups):
        tile, count, offset = struct.unpack_from(">3H", layout, start + group * 6)
        sx = (tile & 15) * 8 + ((tile & 0x300) >> 1)
        sy = ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3)
        cell = atlas.crop((sx, sy, sx + 16, sy + 16))
        for index in range(count):
            flags, x, y = struct.unpack_from(">3H", layout, offset + index * 6)
            patch = ImageOps.mirror(cell) if flags & 0x4000 else cell
            if flags & 0x8000:
                patch = ImageOps.flip(patch)
            if tile:
                image.paste(patch, (x, y))
            tiles[tile] += 1
            sources[(sx, sy)] += 1
    return image, tiles, sources


def cell_grid(layout: bytes) -> tuple[tuple[int, int], list[tuple[int, int]]]:
    _, _, width, height = struct.unpack(">4H", layout[:8])
    cells = [struct.unpack_from(">BBH", layout, 8 + i * 4) for i in range(width * height // 4)]
    return (width, height), [(flip, tile) for flip, _, tile in cells]


def stage_links(catalog: Path) -> tuple[dict, dict[int, list], dict[int, list]]:
    titles, initial, switches = {}, defaultdict(list), defaultdict(list)
    for path in sorted(glob.glob(str(catalog / "details/scenarios/*.json"))):
        for key, record in json.loads(Path(path).read_text()).items():
            titles[int(key.rsplit(":", 1)[1])] = record["label"].split(" · ", 1)[-1]
    for path in sorted(glob.glob(str(catalog / "details/stage_maps/*.json"))):
        for key, record in json.loads(Path(path).read_text()).items():
            scene = int(key.rsplit(":", 1)[1])
            initial[record["fields"][0]["value"]].append(scene)
    for path in sorted(glob.glob(str(catalog / "details/stage_events/*.json"))):
        for record in json.loads(Path(path).read_text()).values():
            script = record["script"]
            for item in script.get("instructions", []):
                if item.get("opcode") == SWITCH_OPCODE:
                    target = next(f["value"] for f in item["fields"] if f.get("role") == "map")
                    kind = item["operands"][3] if len(item["operands"]) > 3 else None
                    switches[target].append({"scenes": script.get("scenes", []), "event": record["key"],
                                             "offset": item["offset"], "kind": kind})
    return titles, initial, switches


def survey(rom: bytes, catalog: Path | None) -> dict:
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("ROM differs from the pinned JP Rev 0 image")
    resources = ResourceTable(rom)
    spec = table("map_assets")
    records = [struct.unpack_from(">5H2B", rom, spec["rom_offset"] + i * spec["stride"]) for i in range(spec["count"])]
    cycles = {rid: cycle_info(resources.extract(rid)[0]) for rid in CYCLE_NAMES}
    atlases: dict[int, Image.Image] = {}
    by_layout = defaultdict(list)
    for index, record in enumerate(records):
        by_layout[record[0]].append(index)
    titles, initial, switches = stage_links(catalog) if catalog and (catalog / "details").is_dir() else ({}, {}, {})
    maps = []
    for index, (layout_id, atlas_id, palette_id, aux1, aux2, mode, param) in enumerate(records):
        layout = resources.extract(layout_id)[0]
        if atlas_id not in atlases:
            atlases[atlas_id] = index_atlas(resources.extract(atlas_id)[0])
        pixels, tiles, sources = compose(layout, atlases[atlas_id])
        histogram = pixels.histogram()
        channels = []
        for rid in (aux1, aux2):
            if not rid:
                continue
            info = cycles[rid]
            span = range(info["first_index"], info["first_index"] + info["colors"])
            channels.append({"resource": rid, "name": CYCLE_NAMES[rid], **info,
                             "pixels": sum(histogram[i] for i in span)})
        colony_cells = sum(n for (sx, sy), n in sources.items()
                           if atlas_id == COLONY_ATLAS and COLONY_RECT[0] <= sx < COLONY_RECT[2]
                           and COLONY_RECT[1] <= sy < COLONY_RECT[3])
        maps.append({
            "map": index, "layout": layout_id, "atlas": atlas_id, "palette": palette_id,
            "mode": mode, "param": param, "width": pixels.width, "height": pixels.height,
            "cells": sum(tiles.values()), "border_cells": sum(tiles[t] for t in BORDER_TILES),
            "cycles": channels,
            "colony": {"active": mode == 1, "instances": colony_cells // 12 if mode == 1 else 0},
            "same_layout": [m for m in by_layout[layout_id] if m != index],
            "initial_for": [{"scene": s, "title": titles.get(s, "")} for s in initial.get(index, [])],
            "switched_to_by": switches.get(index, []),
        })
    grids = {}
    for entry in maps:
        grids[entry["map"]] = cell_grid(resources.extract(entry["layout"])[0])
    for entry in maps:
        for switch in entry["switched_to_by"]:
            sources = [m["map"] for m in maps if any(s in [i["scene"] for i in m["initial_for"]] for s in switch["scenes"])]
            diffs = {}
            for other in sources:
                (size_a, cells_a), (size_b, cells_b) = grids[other], grids[entry["map"]]
                if other != entry["map"] and size_a == size_b:
                    diffs[other] = sum(a != b for a, b in zip(cells_a, cells_b))
            switch["cell_diff_from_scene_initial"] = diffs
    return {"schema": "srw64.map-dynamics.v0", "rom_sha256": ROM_SHA256, "cycles": {
        str(rid): {"name": CYCLE_NAMES[rid], **info} for rid, info in cycles.items()}, "maps": maps}


def tags(entry: dict) -> list[str]:
    out = [f"{c['name']} {c['pixels']:,} px" for c in entry["cycles"] if c["pixels"]]
    dead = [str(c["resource"]) for c in entry["cycles"] if not c["pixels"]]
    if dead:
        out.append("无效循环 " + "/".join(dead))
    if entry["colony"]["active"]:
        out.append(f"殖民地 ×{entry['colony']['instances']}" if entry["colony"]["instances"] else "殖民地（无实例）")
    if entry["same_layout"]:
        out.append("同布局 " + "/".join(map(str, entry["same_layout"])))
    if entry["switched_to_by"]:
        out.append("3D34 切入")
    return out


def markdown(result: dict) -> str:
    rows = ["| 地图 | 尺寸 | 图集 | 调色板 | 初始场景 | 动态 |", "| ---: | --- | --- | --- | --- | --- |"]
    for entry in result["maps"]:
        scenes = entry["initial_for"]
        first = f"{scenes[0]['scene']} {scenes[0]['title']}" if scenes else ""
        more = f" 等 {len(scenes)} 个" if len(scenes) > 1 else ""
        switched = sorted({s for sw in entry["switched_to_by"] for s in sw["scenes"]})
        dynamics = tags(entry)
        if switched:
            dynamics[-1] = "3D34 切入（场景 " + "/".join(map(str, switched)) + "）"
        if not scenes and not switched:
            dynamics.append("未找到静态引用")
        rows.append(f"| {entry['map']} | {entry['width']}×{entry['height']} | {entry['atlas']} | {entry['palette']} "
                    f"| {first}{more} | {'；'.join(dynamics) or '静态'} |")
    return "\n".join(rows) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--catalog", type=Path, default=ROOT / "assets/original-data")
    parser.add_argument("--output", type=Path, default=ROOT / "build/content/map-dynamics.json")
    parser.add_argument("--markdown", type=Path, help="also write the per-map table as Markdown")
    args = parser.parse_args()
    result = survey(args.rom.read_bytes(), args.catalog)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=1) + "\n")
    if args.markdown:
        args.markdown.write_text(markdown(result))
    for entry in result["maps"]:
        scenes = ",".join(str(s["scene"]) for s in entry["initial_for"])
        print(f"{entry['map']:3d} {entry['width']}x{entry['height']} atlas {entry['atlas']} pal {entry['palette']} "
              f"scenes[{scenes}] " + "; ".join(tags(entry)))
    print(f"wrote {args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output}")


if __name__ == "__main__":
    main()
