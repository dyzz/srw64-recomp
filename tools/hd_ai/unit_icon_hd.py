"""Map unit icons (16x16 CI4, resources 688-1009) redrawn as 64x64 pixel art in the icon's own palette.

The chain settled on 2026-09-26 (docs/design/unit-icon-hd.md), every step
local and every output pixel one of the icon's own palette indices:

    MMPX x2 (index only) -> 4x-PixelPerfectV4 (local ESRGAN, smooth 128px)
    -> box to 32x32, each pixel snapped (CIELAB nearest) to a palette index the
       original uses, isolated single pixels removed
    -> MMPX x2 again -> 64x64 index image

No blur, no anti-aliasing, no dither. MMPX before the model straightens the
stair-steps and shrinks the checkerboard dither to 2px blocks, which the model
turns into directional shading instead of smearing it; MMPX after the hard 32
only rounds the stair-steps of the 32px result. The result is an index image,
so the four faction palettes (1010 blue, 1011 red, 1012 yellow, 1013 grey)
render from the same file, exactly as the game does with the 16x16 original.
At the play profile's resolution scale 4 the 64px icon draws 1:1.

    build/esrgan-venv/bin/python tools/hd_ai/unit_icon_hd.py export --output assets/hd-ai/unit-icons/v2
    build/esrgan-venv/bin/python tools/hd_ai/unit_icon_hd.py run --output assets/hd-ai/unit-icons/v2 \
        [--models build/esrgan-models] [--model 4x-PixelPerfectV4] [--pre 2] [--size 32] [--post 2] [--only 737 944]

`--pre 0`/`--post 0` skip the MMPX passes; `--blur` (default 0) is the
Gaussian radius of the abandoned first recipe (smooth 16 -> blur 0.7 -> 32).

`export` writes idx/<rid>.png (L, index per pixel), rgb/<rid>-<palette>.png,
palettes.json and names.json (icon resource -> units). `run` writes
hd/<rid>.idx.png and hd/<rid>-<palette>.png, a full contact sheet and a
review sheet of a few icons (original, smooth, result, red, grey). The
model output is ROM-derived, so nothing here goes into the public HD package.
Needs the ESRGAN venv (torch, spandrel, numpy, pillow); the game venv has no numpy.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "src"))

ICON_TABLE = (1052024, 2, 363)          # layout images.units: u16 icon resource per unit
PALETTES = (1010, 1011, 1012, 1013)     # blue, red, yellow, grey; 1015 is another scheme
REVIEW = (737, 944, 853, 929, 779, 733, 878, 838, 692)


def rgba5551(value: int) -> tuple[int, int, int, int]:
    return tuple(round(((value >> shift) & 31) * 255 / 31) for shift in (11, 6, 1)) + (255 * (value & 1),)


def read_palette(data: bytes) -> list[tuple[int, int, int, int]]:
    kind, size, _, _ = struct.unpack(">4H", data[:8])
    assert kind == 3
    return [rgba5551(v) for (v,) in struct.iter_unpack(">H", data[8:8 + size])]


def read_ci4(data: bytes) -> Image.Image:
    kind, width, height, _ = struct.unpack(">4H", data[:8])
    assert kind in (5, 14)
    image = Image.new("L", (width, height))
    image.putdata([v for b in data[8:] for v in (b >> 4, b & 15)])
    return image


def render(index: Image.Image, palette: list[tuple[int, int, int, int]]) -> Image.Image:
    out = Image.new("RGBA", index.size)
    out.putdata([palette[v] for v in index.getdata()])
    return out


def export(args: argparse.Namespace) -> None:
    from srw64_rom.resources import ResourceTable
    from srw64_native.catalog import source_catalog
    from srw64_native.original_data import extract_gameplay

    rom = args.rom.read_bytes()
    resources = ResourceTable(rom)
    layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
    sources, _, _ = source_catalog(ROOT, args.rom)
    units = extract_gameplay(rom, layout, sources)["units"]
    offset, stride, count = ICON_TABLE
    names: dict[int, list[str]] = {}
    for uid in range(count):
        rid = struct.unpack_from(">H", rom, offset + uid * stride)[0]
        names.setdefault(rid, []).append(f"{uid}:{units[uid]['label']}")
    palettes = {p: read_palette(resources.extract(p)[0]) for p in PALETTES}
    out = args.output
    (out / "idx").mkdir(parents=True, exist_ok=True)
    (out / "rgb").mkdir(exist_ok=True)
    for rid in sorted(names):
        index = read_ci4(resources.extract(rid)[0])
        index.save(out / "idx" / f"{rid}.png")
        for p, palette in palettes.items():
            render(index, palette).save(out / "rgb" / f"{rid}-{p}.png")
    (out / "palettes.json").write_text(json.dumps(palettes))
    (out / "names.json").write_text(json.dumps(names, ensure_ascii=False, indent=0))
    print(f"{len(names)} icons -> {out}")


def to_lab(rgb: np.ndarray) -> np.ndarray:
    rgb = np.asarray(rgb, np.float64) / 255
    rgb = np.where(rgb > 0.04045, ((rgb + 0.055) / 1.055) ** 2.4, rgb / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = rgb @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def repixelize(smooth: Image.Image, index: Image.Image, palette: list, size: int) -> np.ndarray:
    """Box-downsample the smooth RGBA to size x size and snap every opaque pixel to
    the nearest (CIELAB) palette entry among those the original icon uses."""
    used = sorted(set(index.getdata()) - {0})
    lab_palette = to_lab(np.array([palette[i][:3] for i in used], np.float64))
    small = np.asarray(smooth.resize((size, size), Image.BOX))
    dist = ((to_lab(small[..., :3])[..., None, :] - lab_palette[None, None]) ** 2).sum(-1)
    out = np.array(used, np.uint8)[dist.argmin(-1)]
    out[small[..., 3] < 128] = 0
    return out


def remove_isolated(q: np.ndarray, passes: int = 2) -> np.ndarray:
    """A pixel with no 4-neighbour of its own index takes the majority index when 3+ neighbours agree."""
    out = q.astype(np.int16)
    for _ in range(passes):
        p = np.pad(out, 1, mode="edge")
        n = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
        for y, x in zip(*np.where((n == out[None]).sum(0) == 0)):
            values, counts = np.unique(n[:, y, x], return_counts=True)
            if counts.max() >= 3:
                out[y, x] = values[counts.argmax()]
    return out.astype(np.uint8)


def smooth_upscale(model, image: Image.Image, device, blur: float) -> Image.Image:
    from tools.hd_ai.esrgan_pose import fill_transparent, upscale
    colour, alpha = upscale(model, fill_transparent(image), device), upscale(model, image.getchannel("A").convert("RGB"), device)
    result = colour.copy()
    result.putalpha(alpha.convert("L"))
    if blur > 0:
        rgb = result.convert("RGB").filter(ImageFilter.GaussianBlur(blur))
        rgb.putalpha(result.getchannel("A").filter(ImageFilter.GaussianBlur(blur)))
        result = rgb
    return result


def contact_sheet(files: list[Path], path: Path, zoom: int, columns: int = 20) -> None:
    cell = 16 * zoom * 2 + 4   # zoom is per original pixel; a 64px file shows at zoom/2
    rows = -(-len(files) // columns)
    sheet = Image.new("RGBA", (columns * cell, rows * cell), (60, 60, 70, 255))
    for i, f in enumerate(files):
        image = Image.open(f).convert("RGBA")
        image = image.resize((cell - 4, cell - 4), Image.NEAREST)
        sheet.paste(image, ((i % columns) * cell + 2, (i // columns) * cell + 2), image)
    sheet.save(path)


def review_sheet(out: Path, ids: list[int], size: int) -> None:
    columns = [("original", lambda r: out / "rgb" / f"{r}-1010.png"), ("smooth", lambda r: out / "smooth" / f"{r}.png"),
               (f"{size}px", lambda r: out / "hd" / f"{r}-1010.png"), ("red", lambda r: out / "hd" / f"{r}-1011.png"),
               ("grey", lambda r: out / "hd" / f"{r}-1013.png")]
    cell, pad = 128, 8
    page = Image.new("RGB", (pad + len(columns) * (cell + pad), 24 + len(ids) * (cell + pad)), (40, 44, 60))
    draw = ImageDraw.Draw(page)
    for c, (label, where) in enumerate(columns):
        draw.text((pad + c * (cell + pad), 6), label, fill=(230, 230, 230))
        for r, rid in enumerate(ids):
            f = where(rid)
            if not f.exists():
                continue
            image = Image.open(f).convert("RGBA").resize((cell, cell), Image.NEAREST)
            tile = Image.new("RGBA", (cell, cell), (70, 76, 96, 255))
            tile.alpha_composite(image)
            page.paste(tile.convert("RGB"), (pad + c * (cell + pad), 24 + r * (cell + pad)))
    page.save(out / "review.png")


def run(args: argparse.Namespace) -> None:
    import spandrel
    import torch
    from tools.hd_ai.pixel_scale import magnify

    out = args.output
    palettes = {int(k): [tuple(c) for c in v] for k, v in json.loads((out / "palettes.json").read_text()).items()}
    device = torch.device("mps" if torch.backends.mps.is_available() else "cpu")
    model = spandrel.ModelLoader().load_from_file(str(next(args.models.glob(args.model + ".*")))).to(device).eval()
    (out / "smooth").mkdir(exist_ok=True)
    (out / "hd").mkdir(exist_ok=True)
    files = sorted((out / "idx").glob("*.png"), key=lambda p: int(p.stem))
    if args.only:
        files = [f for f in files if int(f.stem) in args.only]
    started = time.monotonic()
    for f in files:
        rid = int(f.stem)
        index = Image.open(f)
        pre = magnify(index, palettes[1010], args.pre, "mmpx") if args.pre > 1 else index
        smooth = smooth_upscale(model, render(pre, palettes[1010]), device, args.blur)
        smooth.save(out / "smooth" / f"{rid}.png")
        q = Image.fromarray(remove_isolated(repixelize(smooth, index, palettes[1010], args.size)), "L")
        if args.post > 1:
            q = magnify(q, palettes[1010], args.post, "mmpx")
        q.save(out / "hd" / f"{rid}.idx.png")
        for p, palette in palettes.items():
            render(q, palette).save(out / "hd" / f"{rid}-{p}.png")
    contact_sheet([out / "hd" / f"{f.stem}-1010.png" for f in files], out / "sheet.png", zoom=4)
    review_sheet(out, [r for r in REVIEW if (out / "hd" / f"{r}-1010.png").exists()], args.size * max(args.post, 1))
    (out / "run.json").write_text(json.dumps({"schema": "srw64.unit-icon-hd.v1", "model": args.model, "pre_mmpx": args.pre,
                                              "size": args.size, "post_mmpx": args.post, "blur": args.blur, "count": len(files),
                                              "seconds": round(time.monotonic() - started, 1)}, indent=1))
    print(f"{len(files)} icons in {time.monotonic() - started:.1f}s -> {out / 'hd'}")


def pack(args: argparse.Namespace) -> None:
    from srw64_rom.resources import ResourceTable
    from srw64_native.catalog import sha
    from tools.hd_ai.esrgan_pose import fill_transparent
    from tools.hd_ai.rt64_hash import hasher
    from tools.hd_ai.worldmap_space import ci4_hash

    out, target = args.output, args.pack
    resources = ResourceTable(args.rom.read_bytes())
    palettes = {p: resources.extract(p)[0][8:40] for p in PALETTES}
    xxh = hasher()
    database = json.loads((target / "rt64.json").read_text())
    entries = {e["hashes"]["rt64"]: e for e in database["textures"]}
    added, keys = {}, []
    for f in sorted((out / "hd").glob("*.idx.png"), key=lambda p: int(p.stem.split(".")[0])):
        rid = int(f.stem.split(".")[0])
        pixels = resources.extract(rid)[0][8:]
        for p in PALETTES:
            digest = ci4_hash(pixels, palettes[p], (16, 16), xxh)
            if digest in entries and not entries[digest]["path"].startswith("icon-"):
                raise ValueError(f"{digest} already names another texture")
            image = Image.open(out / "hd" / f"{rid}-{p}.png").convert("RGBA")
            filled = fill_transparent(image)
            filled.putalpha(image.getchannel("A"))
            name = f"icon-{digest}.png"
            filled.save(target / name)
            entries[digest] = {"hashes": {"rt64": digest}, "path": name}
            added[digest] = sha((target / name).read_bytes())
            keys.append({"icon": rid, "palette": p, "hash": digest})
    database["textures"] = sorted(entries.values(), key=lambda e: e["hashes"]["rt64"])
    (target / "rt64.json").write_text(json.dumps(database, indent=2) + "\n")
    (out / "pack-keys.json").write_text(json.dumps({"schema": "srw64.unit-icon-keys.v1", "pack": str(target), "keys": keys}, indent=1))
    print(f"{len(added)} icon textures -> {target}")
    if args.dump:
        seen = {p.name.split(".")[0] for p in args.dump.glob("*.rice.json")}
        small = {}
        for p in args.dump.glob("*.tile.json"):
            tile = json.loads(p.read_text())
            if (tile["width"], tile["height"]) == (16, 16):
                small[p.name.split(".")[0]] = (tile["tile"]["line"], tile["tile"]["fmt"], tile["tile"]["siz"])
        hit = [k for k in keys if k["hash"] in seen]
        print(f"dump: {len(seen)} textures, {len(small)} of 16x16 {sorted(set(small.values()))}, {len(hit)} icon keys matched")
        if small and not hit:
            raise SystemExit("no icon hash matched the dumped 16x16 textures: check the TMEM layout assumptions")
    if args.bind:
        path = ROOT / "content/art/stage1-hd.json"
        manifest = json.loads(path.read_text())
        rows = [r for r in manifest["textures"] if r["kind"] != "icon"]
        rows += [{"hash": h, "kind": "icon", "sha256": v} for h, v in sorted(added.items())]
        manifest["textures"] = rows
        manifest["source"] = {"path": str(target.resolve().relative_to(ROOT)), "manifest_sha256": sha((target / "rt64.json").read_bytes())}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print("bound", path.relative_to(ROOT), len(rows), "textures")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    e = sub.add_parser("export", help="decode the icons and the four faction palettes from the ROM")
    e.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    e.add_argument("--output", type=Path, required=True)
    e.set_defaults(func=export)
    r = sub.add_parser("run", help="smooth upscale, then re-pixelize into the icon's palette")
    r.add_argument("--output", type=Path, required=True, help="an export output; hd/ and sheets are written beside it")
    r.add_argument("--models", type=Path, default=ROOT / "build/esrgan-models")
    r.add_argument("--model", default="4x-PixelPerfectV4")
    r.add_argument("--pre", type=int, default=2, choices=(0, 1, 2, 4, 8), help="MMPX factor before the model")
    r.add_argument("--size", type=int, default=32, help="hard-quantized size before the final MMPX")
    r.add_argument("--post", type=int, default=2, choices=(0, 1, 2, 4), help="MMPX factor after the hard quantization")
    r.add_argument("--blur", type=float, default=0.0)
    r.add_argument("--only", type=int, nargs="*", help="icon resource ids")
    r.set_defaults(func=run)
    k = sub.add_parser("pack", help="key the icons by RT64 hash and add them to an RT64 replacement pack")
    k.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    k.add_argument("--output", type=Path, required=True, help="a run output with hd/")
    k.add_argument("--pack", type=Path, required=True, help="the RT64 pack directory (rt64.json)")
    k.add_argument("--bind", action="store_true", help="list the textures as kind icon in content/art/stage1-hd.json")
    k.add_argument("--dump", type=Path, help="an SRW64_TEXTURE_DUMP directory to check the hashes against")
    k.set_defaults(func=pack)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
