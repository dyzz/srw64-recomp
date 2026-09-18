#!/usr/bin/env python3
"""Build outline-font replacements and optional layout changes for a captured frame.

Uses actual RT64 texture hashes/load coordinates, verified font resources, and
the repository glyph map. The generated pack does not modify the input ROM.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont
from srw64_rom.resources import ResourceTable
from srw64_rom.glyphs import glyph_map_path, load_glyph_map

ROOT = Path(__file__).resolve().parents[3]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def glyph_id(resource: int, x: int, y: int) -> int:
    if resource == 0 and y < 70:
        if x % 8 or y % 14:
            raise RuntimeError("misaligned narrow font tile")
        return y // 14 * 63 + x // 8
    if resource == 0:
        y -= 70
        base = 0x13B
    else:
        base = 0x597
    if x % 14 or y % 14:
        raise RuntimeError("misaligned wide font tile")
    return base + y // 14 * 36 + x // 14


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--textures", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cell-height", type=int, default=14, help="logical height, original is 14")
    parser.add_argument("--pixel-scale", type=int, default=3, help="drawable pixels per logical pixel")
    parser.add_argument("--wrap-right", type=int, default=201, help="right edge of this captured dialogue fixture")
    args = parser.parse_args()
    if not 10 <= args.cell_height <= 28 or not 1 <= args.pixel_scale <= 8:
        parser.error("unsupported font-probe dimensions")
    args.output.mkdir(parents=True, exist_ok=False)
    data = bytearray((args.source / "latest-gfx-rdram.bin").read_bytes())
    rom_path = ROOT / "rom.z64"
    if sha(rom_path) != "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e":
        raise RuntimeError("font experiment requires the reviewed JP Rev0 ROM")
    resources = ResourceTable(rom_path.read_bytes())
    addresses = {}
    for resource in (0, 1):
        decoded, _ = resources.extract(resource)
        address = data.find(decoded)
        if address < 0 or data.find(decoded, address + 1) >= 0:
            raise RuntimeError("font resource not uniquely present in the snapshot")
        addresses[address + 8] = resource
    mapping_path = glyph_map_path(ROOT)
    mapping = load_glyph_map(ROOT)
    pixel_height = args.cell_height * args.pixel_scale
    font = ImageFont.truetype(str(args.font), pixel_height - 2)
    pack = args.output / "pack"
    pack.mkdir()
    textures = []
    glyphs = []
    for path in sorted(args.textures.glob("*.rice.json")):
        load = json.loads(path.read_text())
        address = load["texture"]["address"]
        if address not in addresses or load["type"] != "Tile":
            continue
        tile_path = path.with_name(path.name.replace(".rice.json", ".tile.json"))
        tile = json.loads(tile_path.read_text())
        if tile["height"] != 14 or tile["width"] not in (8, 14):
            raise RuntimeError("unexpected sampled font dimensions")
        code = glyph_id(addresses[address], load["tile"]["uls"] // 2, load["tile"]["ult"] // 4)
        character = mapping.get(code)
        if not character or len(character) != 1:
            continue  # Keep explicit special/UI cells in their original form.
        palette_path = path.with_name(path.name.replace(".rice.json", ".rice.palette.rdram"))
        raw = palette_path.read_bytes()
        raw = bytes(raw[i ^ 3] for i in range(len(raw)))
        foreground = struct.unpack_from(">H", raw, 2)[0]
        color = tuple(round(((foreground >> shift) & 31) * 255 / 31) for shift in (11, 6, 1))
        width = round(tile["width"] * pixel_height / 14)
        box = font.getbbox(character)
        ink = Image.new("L", (max(1, box[2] - box[0]), max(1, box[3] - box[1])), 0)
        ImageDraw.Draw(ink).text((-box[0], -box[1]), character, font=font, fill=255)
        # Preserve the original narrow/wide advance ratio in this fixture.
        # Rasterize outlines first; never scale up the ROM's bitmap glyphs.
        if ink.width > width - 2 or ink.height > pixel_height - 2:
            ink = ink.resize((min(ink.width, width - 2), min(ink.height, pixel_height - 2)), Image.Resampling.LANCZOS)
        mask = Image.new("L", (width, pixel_height), 0)
        mask.paste(ink, ((width - ink.width) // 2, (pixel_height - ink.height) // 2))
        image = Image.new("RGBA", mask.size, color + (0,))
        image.putalpha(mask)
        texture_hash = path.name.split(".")[0]
        filename = f"glyph-{code:04x}-{texture_hash}.png"
        image.save(pack / filename)
        textures.append({"hashes": {"rt64": texture_hash}, "path": filename})
        glyphs.append({"glyph_id": code, "character": character, "texture_hash": texture_hash,
                       "dimensions": list(image.size), "color": list(color), "sha256": sha(pack / filename)})
    if not glyphs:
        raise RuntimeError("no mapped glyph textures in capture")
    (pack / "rt64.json").write_text(json.dumps({"configuration": {"autoPath": "rt64", "configurationVersion": 3,
        "hashVersion": 5, "defaultOperation": "preload", "defaultShift": "none"}, "textures": textures}, indent=2) + "\n")
    task_path = args.source / "latest-gfx-task.bin"
    task = struct.unpack("<16I", task_path.read_bytes())
    start, size = task[12] & 0x1FFFFFFF, task[13]
    active_font = False
    original_y = None
    left = top = next_x = next_y = 0.0
    changes = []
    scale = args.cell_height / 14
    if args.cell_height != 14:
        rectangles = []
        for offset in range(start, start + size, 8):
            first, second = struct.unpack_from(">II", data, offset)
            opcode = first >> 24
            if opcode == 0xFD:
                active_font = (second & 0x1FFFFFFF) in addresses
            if opcode != 0xE4 or not active_font:
                continue
            old = [(second >> 12) & 0xFFF, second & 0xFFF, (first >> 12) & 0xFFF, first & 0xFFF]
            rectangles.append((old[1], old[0], offset, first, second, old))
        # The game draws the body before the speaker label. Layout must follow
        # visual rows, while retaining the original display-list draw order.
        rows: dict[int, list] = {}
        for rectangle in sorted(rectangles):
            rows.setdefault(rectangle[0], []).append(rectangle)
        balanced_breaks = set()
        for row in rows.values():
            widths = [(item[5][2] - item[5][0]) / 4 * scale for item in row]
            total = sum(widths)
            available = args.wrap_right - row[0][1] / 4
            if available < total <= 2 * available:
                prefix = 0.0
                choices = []
                for index, width in enumerate(widths[:-1], 1):
                    prefix += width
                    if max(prefix, total - prefix) <= available:
                        choices.append((abs(prefix - total / 2), row[index][2]))
                if choices:
                    balanced_breaks.add(min(choices)[1])
        for _, _, offset, first, second, old in sorted(rectangles):
            x, y, right, bottom = [v / 4 for v in old]
            if original_y is None:
                left, top, next_x, next_y = x, y, x, y
            elif y != original_y:
                next_x, next_y = left, next_y + args.cell_height
            width, height = (right - x) * scale, (bottom - y) * scale
            if offset in balanced_breaks or next_x + width > args.wrap_right:
                next_x, next_y = left, next_y + args.cell_height
            if next_y + height > 240:
                raise RuntimeError("font layout exceeds the captured screen")
            new = [round(v * 4) for v in (next_x, next_y, next_x + width, next_y + height)]
            struct.pack_into(">II", data, offset, (first & 0xFF000000) | (new[2] << 12) | new[3],
                             (second & 0xFF000000) | (new[0] << 12) | new[1])
            step_command, step = struct.unpack_from(">II", data, offset + 16)
            if step_command >> 24 != 0xF1:
                raise RuntimeError("unexpected texture rectangle command sequence")
            sx, sy = struct.unpack(">hh", struct.pack(">I", step))
            struct.pack_into(">hh", data, offset + 20, round(sx / scale), round(sy / scale))
            changes.append({"command_offset": offset, "old_quarter_pixels": old, "new_quarter_pixels": new})
            original_y, next_x = y, next_x + width
    replay = args.output / "source"
    replay.mkdir()
    (replay / "latest-gfx-rdram.bin").write_bytes(data)
    (replay / "latest-gfx-task.bin").write_bytes(task_path.read_bytes())
    report = {"schema": "srw64.native-font-probe.v1", "evidence_scope": "captured-frame-outline-font-and-layout-experiment",
              "rom_sha256": sha(rom_path), "font_path": str(args.font.resolve()), "font_sha256": sha(args.font),
              "mapping_sha256": sha(mapping_path), "source_rdram_sha256": sha(args.source / "latest-gfx-rdram.bin"),
              "output_rdram_sha256": sha(replay / "latest-gfx-rdram.bin"), "cell_height": args.cell_height,
              "pixel_scale": args.pixel_scale, "glyphs": glyphs, "geometry_changes": changes,
              "outline_font_pixels": pixel_height - 2,
              "limitations": ["One captured dialogue frame; not a replacement for gameplay validation",
                              "Preserves the game's narrow/wide advance ratio", "Uses an explicit right edge for this fixture"]}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"glyph_textures": len(glyphs), "geometry_changes": len(changes), "pixel_height": pixel_height}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
