"""Decode original portraits, tactical icons and static map backgrounds.

Bindings come from pinned ROM tables, never from adjacent resource numbering.
The map decoder follows resident 800945D4's grouped 16x16 draw records; the
preceding terrain grid is preserved by the catalog, not interpreted as pixels.
"""
from __future__ import annotations

from io import BytesIO
import struct

from PIL import Image, ImageOps

from .catalog import sha
from .original_data import checked_slice, link


def decode_indexed(image: bytes, palette: bytes) -> Image.Image:
    kind, width, height, reserved = struct.unpack(">4H", checked_slice(image, 0, 8))
    palette_kind, size, _, _ = struct.unpack(">4H", checked_slice(palette, 0, 8))
    if palette_kind != 3 or size % 2 or not 0 < size <= 512 or len(palette) != 8 + size:
        raise ValueError("Invalid RGBA16 palette")
    if kind not in (5, 6, 7, 8, 14, 15) or reserved or not (0 < width <= 2048 and 0 < height <= 2048):
        raise ValueError("Unsupported indexed image header")
    count = width * height
    ci4 = kind in (5, 14)
    if len(image) != 8 + ((count + 1) // 2 if ci4 else count):
        raise ValueError("Indexed image size mismatch")
    colors = []
    for (value,) in struct.iter_unpack(">H", palette[8:]):
        colors.append(bytes([round(((value >> shift) & 31) * 255 / 31)
                             for shift in (11, 6, 1)] + [255 * (value & 1)]))
    indices = image[8:]
    if ci4:
        indices = [n for byte in indices for n in (byte >> 4, byte & 15)][:count]
    if max(indices) >= len(colors):
        raise ValueError("Pixel index exceeds palette")
    return Image.frombytes("RGBA", (width, height), b"".join(colors[i] for i in indices))


def decode_map(layout: bytes, atlas: Image.Image) -> tuple[Image.Image, dict]:
    kind, groups, width, height = struct.unpack(">4H", checked_slice(layout, 0, 8))
    if kind != 7 or not groups or width % 2 or height % 2 or not (0 < width <= 256 and 0 < height <= 256):
        raise ValueError("Unsupported map layout header")
    # width/2 * height/2 terrain records, four bytes each, before the draw groups.
    group_start = 8 + width * height
    checked_slice(layout, group_start, groups * 6)
    image = Image.new("RGBA", (width * 8, height * 8))
    positions = set()
    flags_seen = set()
    next_positions = group_start + groups * 6
    for group in range(groups):
        tile, count, offset = struct.unpack_from(">3H", layout, group_start + group * 6)
        if offset != next_positions:
            raise ValueError("Non-contiguous map placement lists")
        checked_slice(layout, offset, count * 6)
        next_positions += count * 6
        sx = (tile & 15) * 8 + ((tile & 0x300) >> 1)
        sy = ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3)
        if tile >= 0x1000 or sx + 16 > atlas.width or sy + 16 > atlas.height:
            raise ValueError("Map tile exceeds atlas")
        base = atlas.crop((sx, sy, sx + 16, sy + 16))
        for index in range(count):
            flags, x, y = struct.unpack_from(">3H", layout, offset + index * 6)
            if x % 16 or y % 16 or x + 16 > image.width or y + 16 > image.height:
                raise ValueError("Map placement exceeds canvas or tile grid")
            if (x, y) in positions:
                raise ValueError("Overlapping map placements")
            positions.add((x, y))
            flags_seen.add(flags)
            # The renderer only tests 0x4000 / 0x8000 here. Preserve other bits
            # as metadata, without assigning terrain or animation semantics.
            patch = ImageOps.mirror(base) if flags & 0x4000 else base
            if flags & 0x8000:
                patch = ImageOps.flip(patch)
            if tile:  # mode 0 skips tile zero in 80094950..80094978.
                image.paste(patch, (x, y))
    if next_positions != len(layout) or len(positions) != width * height // 4:
        raise ValueError("Incomplete map placement coverage")
    return image, {"draw_groups": groups, "placements": len(positions),
                   "grid_columns": width // 2, "grid_rows": height // 2,
                   "placement_flags": sorted(flags_seen), "rendered_flip_mask": 0xC000,
                   "scope": "static base map; excludes units, events, auxiliary layers and runtime effects"}


def png_bytes(image: Image.Image) -> bytes:
    stream = BytesIO()
    image.save(stream, format="PNG")
    return stream.getvalue()


def attach_images(categories: dict, rom: bytes, config: dict, read_resource, write) -> dict:
    """Write deduplicated PNGs and attach auditable resource bindings to dossiers."""
    decoded = {}
    previews = {}
    coverage = {"actors": 0, "units": 0, "map_assets": 0, "stage_maps": 0, "scenarios": 0}

    def source(rid: int, role: str) -> dict:
        data = read_resource(rid)
        return {"key": f"base:resources:{rid:04d}", "role": role, "decoded_sha256": sha(data)}

    def texture(rid: int, pid: int) -> Image.Image:
        pair = (rid, pid)
        if pair not in decoded:
            decoded[pair] = decode_indexed(read_resource(rid), read_resource(pid))
        return decoded[pair]

    def save(kind: str, identity: str, image: Image.Image, caption: str, sources: list) -> dict:
        path = f"images/{kind}/{identity}.png"
        if path not in previews:
            write(path, png_bytes(image))
            thumb = path
            if kind == "map":
                small = image.copy()
                small.thumbnail((160, 160), Image.Resampling.NEAREST)
                thumb = f"images/map/{identity}-thumb.png"
                write(thumb, png_bytes(small))
            previews[path] = {"kind": kind, "path": path, "thumbnail": thumb,
                              "width": image.width, "height": image.height,
                              "caption": caption, "sources": sources, "confidence": "code-confirmed"}
        return dict(previews[path])

    def attach(row: dict, preview: dict, evidence: list[str]) -> None:
        row["images"] = [preview]
        row["thumbnail"] = {k: preview[k] for k in ("kind", "thumbnail", "caption")}
        for ref in preview["sources"]:
            if not any(l["key"] == ref["key"] and l["relation"] == ref["role"] for l in row["links"]):
                row["links"].append(link(ref["key"], ref["role"]))
        row["evidence"] = list(dict.fromkeys(row.get("evidence", []) + evidence))

    for category, kind, caption in (("actors", "portrait", "原版人物头像"),
                                     ("units", "unit-icon", "原版地图图标 · 我方配色")):
        spec = config[category]
        raw = checked_slice(rom, spec["rom_offset"], spec["stride"] * spec["count"])
        if sha(raw) != spec["sha256"] or len(categories[category]) != spec["count"]:
            raise ValueError(f"Image binding table changed: {category}")
        for index, row in enumerate(categories[category]):
            offset = index * spec["stride"]
            rid = struct.unpack_from(">H", raw, offset)[0]
            pid = (struct.unpack_from(">H", raw, offset + 2)[0] if category == "actors"
                   else spec["palette_resource"])
            preview = save(kind, f"{rid:04d}-{pid:04d}", texture(rid, pid), caption,
                           [source(rid, "图像索引像素"), source(pid, "RGBA16 调色板")])
            preview["binding"] = {"rom_offset": spec["rom_offset"] + offset,
                                  "raw_hex": raw[offset:offset + spec["stride"]].hex(),
                                  "table_sha256": spec["sha256"]}
            attach(row, preview, spec["evidence"])
            coverage[category] += 1

    for row in categories["map_assets"]:
        rid, tid, pid = struct.unpack_from(">3H", bytes.fromhex(row["raw_hex"]))
        image, layout_info = decode_map(read_resource(rid), texture(tid, pid))
        preview = save("map", f"{rid:04d}-{tid:04d}-{pid:04d}", image,
                       "原版战场底图 · 不含机体与事件叠加层",
                       [source(rid, "地图布局"), source(tid, "地图图集"), source(pid, "地图调色板")])
        preview["layout"] = layout_info
        attach(row, preview, config["map_evidence"])
        coverage["map_assets"] += 1
    for row in categories["stage_maps"]:
        mid = bytes.fromhex(row["raw_hex"])[0]
        attach(row, categories["map_assets"][mid]["images"][0], config["map_evidence"])
        coverage["stage_maps"] += 1
    stages = {r["key"]: r for r in categories["stage_maps"]}
    for row in categories.get("scenarios", []):
        attach(row, stages[row["scenario"]["map_key"]]["images"][0], config["map_evidence"])
        coverage["scenarios"] += 1
    coverage["unique_previews"] = len(previews)
    coverage["map_placements"] = sum(r["images"][0]["layout"]["placements"] for r in categories["map_assets"])
    return coverage
