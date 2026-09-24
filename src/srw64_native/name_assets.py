"""Extract the native pages' portraits from the pinned ROM's own face table.

The eight opening portraits for the name pages, and the Link Battler pilots for
the link page (src/host/link_page.hpp). The face table is indexed by actor.
"""
import json
import struct
from pathlib import Path

from PIL import Image
from srw64_rom.resources import ResourceTable
from .catalog import sha

# Link Battler series in link::series order, the lead pilot first: シーブック セシリー;
# 真吾 キリー レミー; 勝平 宇宙太 恵子.
LINK_FACES = ((41, 230), (133, 131, 132), (152, 151, 153))


def prepare_name_assets(rom: bytes, output: Path, *, hd_portrait=None) -> dict:
    """Extract the portraits; `hd_portrait(image, palette)` names the whole HD portrait, if any."""
    table = ResourceTable(rom)
    # 801C3398 selects these face-table indices for the four protagonist routes.
    route_offset = 0x1090A0 + 0x801C6BF0 - 0x801C2600
    faces = struct.unpack_from(">8H", rom, route_offset)
    if faces != (27, 28, 25, 26, 31, 32, 29, 30):
        raise ValueError("Opening character portrait table changed")
    output.mkdir(parents=True, exist_ok=False)
    portraits = {}
    for face in faces + tuple(face for group in LINK_FACES for face in group):
        image_id, palette_id = struct.unpack_from(">2H", rom, 0x84220 + 4*face)
        image_data = table.extract(image_id)[0]
        palette_data = table.extract(palette_id)[0]
        # One byte per pixel, 96 or (シーブック, セシリー) 97 pixels square.
        kind, width, height, zero = struct.unpack_from(">4H", image_data)
        if kind not in (6, 15) or width != height or width not in (96, 97) or zero or len(image_data) != 8 + width*height:
            raise ValueError("Unexpected opening portrait shape")
        if palette_data[:8] != bytes.fromhex("0003008000000000"):
            raise ValueError("Unexpected portrait palette shape")
        colors = [tuple(round(((v >> shift) & 31)*255/31) for shift in (11, 6, 1)) + (255*(v & 1),)
                  for (v,) in struct.iter_unpack(">H", palette_data[8:])]
        rgba = bytes(c for index in image_data[8:] for c in colors[index])
        path = output / f"face-{face}.png"
        Image.frombytes("RGBA", (width, height), rgba).save(path)
        row = {"original": str(path), "resource_id": image_id, "palette_id": palette_id,
               "original_sha256": sha(path.read_bytes())}
        if hd_portrait and (whole := hd_portrait(image_id, palette_id)):
            row["hd"] = whole  # the whole HD portrait set (native_portrait.cpp)
        portraits[str(face)] = row
    result = {"schema": "srw64.name-entry-assets.v1", "portraits": portraits,
              "route_faces": [list(faces[:4]), list(faces[4:])],
              "link_faces": [list(group) for group in LINK_FACES],
              "source_sha256": None}
    (output / "manifest.json").write_text(json.dumps(result, indent=2)+"\n")
    return result
