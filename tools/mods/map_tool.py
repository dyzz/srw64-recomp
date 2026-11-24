#!/usr/bin/env python3
"""Build a campaign's own tactical map from one painted picture and a terrain grid
(docs/design/level-editor.md §4).

The source is a folder maps/<name>/ with map.json (schema srw64.map.v1):

    {"schema": "srw64.map.v1", "like": 20, "image": "base.png", "scale": 4,
     "terrain": [[99, 99, ...], ...]}

- image: the whole map, `scale` times the game's pixels (a cell is 16 x 16 of those), so
  a 28 x 32 cell map at scale 4 is 1792 x 2048. Scale 1 has no HD picture of its own.
- terrain: a row per cell row, a terrain number (0-98, the game's table 802196D0) per
  cell. The outer two rings are the map's frame (99) whatever is written there.
- like: the original map this one stands in for. The game goes on loading that map's
  number; the host gives it this map's layout, tile atlas and palette instead
  (src/host/map_mod.hpp). Its music, its space rules, its battle backgrounds and its
  palette cycles (water) stay the original's, so pick one of the same kind of place.

Out comes what the original draws (the picture reduced to at most 1,023 16 x 16 tiles in
one 256-colour palette, as the ROM's maps are: layout, atlas, palette) and, at scale 2
and up, what the HD mode draws instead (the picture itself, with the palette index of
every pixel so fades and cycles still move it).

The compiler is the level editor's (web/src/lib/compiler/map.ts, run with node by
web_compiler.py); this file reads the ROM's template maps and writes the files.

    python tools/mods/map_tool.py build <maps/name> -o <out folder> [--rom rom.z64]

writes map.json (srw64.map-image.v1), meta.json, base.png, index.png there, and
preview.png: the map as the original mode will show it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "src"))

SCHEMA = "srw64.map.v1"
IMAGE_SCHEMA = "srw64.map-image.v1"
MAPS = 158
MAP_RECORDS = 0x10267C          # 158 x 12: layout, atlas, palette, aux 1, aux 2 (u16), mode, music (u8)
SPACE_FLAGS = 0x100C40          # 158 bytes (RAM 802180E0)
CELL = 16
TILES = 1023                    # the atlas holds 32 x 32; tile 0 is not drawn (80094950)
FRAME = 2                       # rings of cells no unit enters
FRAME_TERRAIN, WALL = 99, 63
MIN_COLUMNS, MIN_ROWS = 20, 15  # a screen of 320 x 240
MAX_CELLS = 128                 # the layout's header counts 8-pixel units in a byte pair, up to 256
MAX_WIDTH, MAX_HEIGHT = 4240, 3184   # past these the overview's zoom (800943E0) divides by zero
FLIP_H, FLIP_V = 0x4000, 0x8000


class MapError(ValueError):
    pass


def rom_path(given: Path | None = None) -> Path:
    for candidate in (given, os.environ.get("SRW64_ROM"), ROOT / "rom.z64"):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise MapError("新地图要读原版 ROM（模板地图的调色板与水面循环）：用 --rom 或环境变量 SRW64_ROM 指定")


class Templates:
    """The original maps a new one may stand in for, as template() describes one: read from
    the ROM as they are asked for, or given (the web editor's, exported with the site's data
    by web/scripts/export_editor.py, so that no ROM is needed there)."""

    def __init__(self, rom: bytes | None = None, given: dict | None = None):
        self.rom, self.given = rom, given or {}

    def __call__(self, number) -> dict:
        if type(number) is not int or not 0 <= number < MAPS:
            raise MapError(f"like 要是原版地图号 0–{MAPS - 1}")
        if self.rom is not None:
            return template(self.rom, number)
        row = self.given.get(str(number))
        if row is None:
            raise MapError(f"like {number}：这张原版地图不能当模板（带殖民地动画）；换一张")
        return {**row, "palette_bytes": bytes.fromhex(row["palette_hex"]), "cycles": [tuple(c) for c in row["cycles"]]}


def template(rom: bytes, number) -> dict:
    """The original map a new one stands in for: its resources and what cycles in its palette."""
    from srw64_rom.resources import ResourceTable
    if type(number) is not int or not 0 <= number < MAPS:
        raise MapError(f"like 要是原版地图号 0–{MAPS - 1}")
    layout, atlas, palette, aux1, aux2, mode, music = struct.unpack_from(">5H2B", rom, MAP_RECORDS + number * 12)
    if mode != 0:
        raise MapError(f"like {number}：这张原版地图带殖民地动画（模式 {mode}），不能当模板；换一张模式 0 的")
    table = ResourceTable(rom)
    colours = table.extract(palette)[0]
    if struct.unpack_from(">4H", colours) != (3, 512, 0, 0):
        raise MapError(f"like {number}：调色板资源 {palette} 不是 256 色")
    cycles = []
    for aux in (aux1, aux2):
        if aux:
            # A cycle resource starts with its kind, the first palette index and how many.
            data = table.extract(aux)[0]
            cycles.append((data[1], data[2]))
    return {"map": number, "layout": layout, "atlas": atlas, "palette": palette, "palette_bytes": colours,
            "cycles": cycles, "space": rom[SPACE_FLAGS + number], "music": music}


def build(source: Path, templates) -> dict:
    """The map's resources and pictures: {"image": the srw64.map-image.v1 document,
    "base": the HD picture or None, "index": its palette indices or None, "meta": the HD
    mode's or None, "preview": the map as the original mode draws it}. The compiler is the
    level editor's (web/src/lib/compiler/map.ts, run by web_compiler.py); this reads the
    files and the template map and writes the pictures it hands back."""
    import tempfile
    import web_compiler
    sys.path.insert(0, str(ROOT / "src"))
    from srw64_native.original_images import decode_indexed, decode_map
    try:
        document = json.loads((source / "map.json").read_text(encoding="utf-8"))
    except OSError:
        raise MapError(f"{source.name}：找不到 map.json") from None
    except ValueError as error:
        raise MapError(f"{source.name}/map.json：JSON 无效（{error}）") from None
    if not isinstance(document, dict) or document.get("schema") != SCHEMA:
        raise MapError(f"{source.name}/map.json：schema 要是 {SCHEMA}")
    unknown = set(document) - {"schema", "like", "image", "scale", "terrain", "note"}
    if unknown:
        raise MapError(f"{source.name}/map.json：不认识的字段 {sorted(unknown)}")
    if isinstance(templates, (bytes, bytearray)):
        templates = Templates(rom=bytes(templates))
    like = templates(document.get("like"))
    name = document.get("image", "base.png")
    if not isinstance(name, str) or "/" in name or "\\" in name or not (source / name).is_file():
        raise MapError(f"{source.name}：找不到底图 {name!r}（和 map.json 放在一起）")
    with Image.open(source / name) as opened:
        painted = opened.convert("RGB")
    row = {"map": like["map"], "layout": like["layout"], "atlas": like["atlas"], "palette": like["palette"],
           "palette_hex": bytes(like["palette_bytes"]).hex(), "cycles": [list(c) for c in like["cycles"]]}
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        (temp / "picture.rgba").write_bytes(painted.convert("RGBA").tobytes())
        (temp / "templates.json").write_text(json.dumps({str(like["map"]): row}))
        try:
            built = web_compiler.run("build-map.ts", source / "map.json", temp / "picture.rgba", painted.width, painted.height,
                                     temp / "templates.json", source.name)
        except web_compiler.CompileError as error:
            raise MapError(str(error)) from None
    image = built["image"]
    layout, atlas, palette = (bytes.fromhex(image[f"{k}_hex"]) for k in ("layout", "atlas", "palette"))
    preview, _ = decode_map(layout, decode_indexed(atlas, palette))
    out = {"image": image, "preview": preview.convert("RGB"), "base": None, "index": None, "meta": built["meta"]}
    if built["meta"]:
        width, height = image["columns"] * CELL, image["rows"] * CELL
        index = Image.frombytes("L", (width, height), bytes.fromhex(built["indices_hex"])).resize(painted.size, Image.Resampling.NEAREST)
        out.update(base=painted, index=index)
    return out


def write(built: dict, out: Path, preview: bool = False) -> dict:
    """The built map's files in `out` (a package's campaign/maps/<name>/)."""
    out.mkdir(parents=True, exist_ok=True)
    for stale in ("meta.json", "base.png", "index.png", "preview.png"):
        if (out / stale).exists():
            (out / stale).unlink()
    if built["meta"]:
        built["base"].save(out / "base.png", optimize=True)
        built["index"].save(out / "index.png", optimize=True)
        meta = {**built["meta"], "files": {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in ("base.png", "index.png")}}
        (out / "meta.json").write_text(json.dumps(meta) + "\n", encoding="utf-8")
    (out / "map.json").write_text(json.dumps(built["image"]) + "\n", encoding="utf-8")
    if preview:
        built["preview"].save(out / "preview.png")
    return built["image"]


def summary(image: dict) -> str:
    merged = f"，{image['kinds']} 种图块合并成 {image['tiles']} 种" if image["kinds"] > image["tiles"] else f"，{image['tiles']} 种图块"
    quality = "与底图一致" if image["psnr"] is None else f"相对底图 PSNR {image['psnr']} dB"
    return f"{image['name']}: {image['columns']}×{image['rows']} 格，模板地图 {image['like']}{merged}，原版模式{quality}" + \
        ("" if image["hd"] else "，没有 HD 图（scale 1）")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("source", type=Path, help="the map's folder: map.json and its picture")
    b.add_argument("-o", "--out", type=Path, required=True)
    b.add_argument("--rom", type=Path)
    args = parser.parse_args(argv)
    try:
        image = write(build(args.source, rom_path(args.rom).read_bytes()), args.out, preview=True)
    except MapError as error:
        print(f"{args.source}: {error}", file=sys.stderr)
        return 1
    print(summary(image))
    print(f"{args.out / 'preview.png'}: 原版模式下的样子")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
