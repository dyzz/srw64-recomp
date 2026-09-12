"""Extract the eight opening portraits from the pinned ROM's own face table."""
import json
import struct
from pathlib import Path

from PIL import Image
from srw64_rom.resources import ResourceTable
from .assets import inside
from .catalog import sha


def prepare_name_assets(root: Path, rom: bytes, output: Path, *, include_hd: bool = True) -> dict:
    table = ResourceTable(rom)
    # 801C3398 selects these face-table indices for the four protagonist routes.
    route_offset = 0x1090A0 + 0x801C6BF0 - 0x801C2600
    faces = struct.unpack_from(">8H", rom, route_offset)
    if faces != (27, 28, 25, 26, 31, 32, 29, 30):
        raise ValueError("Opening character portrait table changed")
    spec_path = root / "content/ui/name-entry.json"
    hd_sources = {}
    spec_sha = None
    if include_hd:
        spec_bytes = spec_path.read_bytes()
        spec = json.loads(spec_bytes)
        spec_sha = sha(spec_bytes)
        if spec.get("schema") != "srw64.name-entry-art.v1":
            raise ValueError("Unsupported name-entry art")
        # Validate before creating output so missing optional art can fall back
        # to a fresh ROM-only extraction without leaving partial portraits.
        for key, override in spec["hd_portraits"].items():
            pixels = inside(root, override["path"]).read_bytes()
            if sha(pixels) != override["sha256"]:
                raise ValueError("Reviewed name-entry portrait changed")
            hd_sources[key] = pixels
    output.mkdir(parents=True, exist_ok=False)
    portraits = {}
    for face in faces:
        image_id, palette_id = struct.unpack_from(">2H", rom, 0x84220 + 4*face)
        image_data = table.extract(image_id)[0]
        palette_data = table.extract(palette_id)[0]
        if struct.unpack_from(">4H", image_data) != (15, 96, 96, 0) or len(image_data) != 9224:
            raise ValueError("Unexpected opening portrait shape")
        if palette_data[:8] != bytes.fromhex("0003008000000000"):
            raise ValueError("Unexpected portrait palette shape")
        colors = [tuple(round(((v >> shift) & 31)*255/31) for shift in (11, 6, 1)) + (255*(v & 1),)
                  for (v,) in struct.iter_unpack(">H", palette_data[8:])]
        rgba = bytes(c for index in image_data[8:] for c in colors[index])
        path = output / f"face-{face}.png"
        Image.frombytes("RGBA", (96, 96), rgba).save(path)
        row = {"original": str(path), "resource_id": image_id, "palette_id": palette_id,
               "original_sha256": sha(path.read_bytes())}
        if (pixels := hd_sources.get(str(image_id))) is not None:
            hd = output / f"face-{face}-hd.png"
            hd.write_bytes(pixels)
            row.update(hd=str(hd), hd_sha256=sha(hd.read_bytes()))
        portraits[str(face)] = row
    result = {"schema": "srw64.name-entry-assets.v1", "portraits": portraits,
              "route_faces": [list(faces[:4]), list(faces[4:])],
              "source_sha256": spec_sha}
    (output / "manifest.json").write_text(json.dumps(result, indent=2)+"\n")
    return result
