#!/usr/bin/env python3
"""The HD pack prepared for the Switch's GPU, for the NRO's romfs (src/host/hd_texture.hpp).

Reads a released HD pack folder (tools/release/prepare_hd_bundle.py; build_release.py puts
one at build/release/<version>/pack/hd) and writes build/recomp/switch-romfs/hd/: the pack's
manifests as they are, and beside each picture of the layers the Switch draws in HD its
<stem>.mwtex: premultiplied RGBA with a 2 x 2 box-filtered mip chain (as the layers' own
decode() makes it), each level ASTC-compressed (astcenc, LDR, -medium), all levels in one
zstd frame. The large pictures (backgrounds, maps) take 6 x 6 blocks, the rest 4 x 4: a
quarter of RGBA8's memory or less, with no visible loss at the sizes the game shows them
(docs/design/switch-port.md, S3). The JPEGs and PNGs themselves stay out of the romfs.

  tools/release/switch/prepare_hd.py [--pack DIR] [--layers portraits,backgrounds] [--jobs N]

Needs astcenc (ARM's astc-encoder; $ASTCENC or on PATH) and zstd.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/recomp/switch-romfs/hd"
# Layer: (manifest, GL format, block size).
ASTC_4x4, ASTC_6x6 = 0x93B0, 0x93B4
LAYERS = {
    "portraits": ("srw64-portraits-hd.json", ASTC_4x4, "4x4"),
    "backgrounds": ("srw64-backgrounds-hd.json", ASTC_6x6, "6x6"),
}
MAGIC, VERSION = 0x5854574D, 1


def latest_pack() -> Path:
    packs = sorted((ROOT / "build/release").glob("*/pack/hd"), key=lambda p: p.stat().st_mtime)
    if not packs:
        sys.exit("No HD pack under build/release/*/pack/hd: pass --pack")
    return packs[-1]


def rgba(picture: Path) -> Image.Image:
    """The picture with its alpha, as presentation::load_rgba reads it (a JPEG's alpha beside it)."""
    image = Image.open(picture).convert("RGBA")
    alpha = picture.with_suffix(".alpha.png")
    if alpha.exists():
        mask = Image.open(alpha)
        mask = mask.getchannel("A") if "A" in mask.getbands() else mask.convert("L")
        if mask.size != image.size:
            raise SystemExit(f"{alpha}: {mask.size} for a {image.size} picture")
        image.putalpha(mask)
    return image


def premultiplied_mips(image: Image.Image) -> list[bytes]:
    """Level 0 premultiplied (rounded as decode() does), then 2 x 2 averages down to 1 x 1."""
    w, h = image.size
    px = bytearray(image.tobytes())
    for i in range(0, len(px), 4):
        a = px[i + 3]
        for c in range(3):
            px[i + c] = (px[i + c] * a + 127) // 255
    levels = [bytes(px)]
    while w > 1 or h > 1:
        src = levels[-1]
        dw, dh = max(1, w // 2), max(1, h // 2)
        out = bytearray(dw * dh * 4)
        for y in range(dh):
            y0, y1 = min(2 * y, h - 1), min(2 * y + 1, h - 1)
            for x in range(dw):
                x0, x1 = min(2 * x, w - 1), min(2 * x + 1, w - 1)
                for c in range(4):
                    s = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] + src[(y1 * w + x0) * 4 + c] + src[(y1 * w + x1) * 4 + c]
                    out[(y * dw + x) * 4 + c] = (s + 2) // 4
        levels.append(bytes(out))
        w, h = dw, dh
    return levels


def mips_numpy(image: Image.Image) -> list[bytes]:
    """premultiplied_mips() with numpy (the same arithmetic), when it is there."""
    import numpy as np
    a = np.asarray(image, dtype=np.uint32).copy()
    a[..., :3] = (a[..., :3] * a[..., 3:4] + 127) // 255
    levels = [a.astype(np.uint8).tobytes()]
    while a.shape[0] > 1 or a.shape[1] > 1:
        h, w = a.shape[:2]
        ys0 = np.minimum(np.arange(max(1, h // 2)) * 2, h - 1); ys1 = np.minimum(ys0 + 1, h - 1)
        xs0 = np.minimum(np.arange(max(1, w // 2)) * 2, w - 1); xs1 = np.minimum(xs0 + 1, w - 1)
        a = (a[ys0][:, xs0] + a[ys0][:, xs1] + a[ys1][:, xs0] + a[ys1][:, xs1] + 2) // 4
        levels.append(a.astype(np.uint8).tobytes())
    return levels


def encode(astcenc: str, picture: Path, target: Path, fmt: int, block: str) -> int:
    image = rgba(picture)
    w, h = image.size
    try:
        levels = mips_numpy(image)
    except ImportError:
        levels = premultiplied_mips(image)
    blocks = bytearray()
    with tempfile.TemporaryDirectory() as tmp:
        lw, lh = w, h
        for n, level in enumerate(levels):
            png, astc = Path(tmp) / f"{n}.png", Path(tmp) / f"{n}.astc"
            Image.frombytes("RGBA", (lw, lh), level).save(png)
            subprocess.run([astcenc, "-cl", str(png), str(astc), block, "-medium", "-silent", "-j", "1"], check=True)
            blocks += astc.read_bytes()[16:]   # the .astc header: magic, block and image sizes
            lw, lh = max(1, lw // 2), max(1, lh // 2)
    packed = subprocess.run(["zstd", "-q", "-19", "-c"], input=bytes(blocks), stdout=subprocess.PIPE, check=True).stdout
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(struct.pack("<6I", MAGIC, VERSION, fmt, w, h, len(levels)) + packed)
    return len(blocks)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pack", type=Path, help="released HD pack folder (default: the newest build/release/*/pack/hd)")
    parser.add_argument("--layers", default="portraits,backgrounds", help="comma-separated: " + ",".join(LAYERS))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    pack = args.pack or latest_pack()
    astcenc = os.environ.get("ASTCENC") or shutil.which("astcenc")
    if not astcenc or not shutil.which("zstd"):
        sys.exit("Needs astcenc ($ASTCENC or PATH) and zstd")
    art = pack / "art"
    out_art = OUT / "art"
    out_art.mkdir(parents=True, exist_ok=True)
    for name in ("hd.json", "NOTICE.txt"):
        if (pack / name).exists():
            shutil.copy2(pack / name, OUT / name)
    for manifest in art.glob("*.json"):
        shutil.copy2(manifest, out_art / manifest.name)
    jobs = []
    for layer in [l for l in args.layers.split(",") if l]:
        if layer not in LAYERS:
            sys.exit(f"Unknown layer {layer}")
        spec, fmt, block = LAYERS[layer]
        for row in json.loads((art / spec).read_text())["images"]:
            picture = art / row["file"]
            target = (out_art / row["file"]).with_suffix(".mwtex")
            if target.exists() and target.stat().st_mtime >= picture.stat().st_mtime:
                continue
            jobs.append((picture, target, fmt, block))
    print(f"{len(jobs)} picture(s) to prepare from {pack}", flush=True)
    total = 0
    with ThreadPoolExecutor(args.jobs) as pool:
        for n, raw in enumerate(pool.map(lambda j: encode(astcenc, *j), jobs), 1):
            total += raw
            if n % 50 == 0 or n == len(jobs):
                print(f"  {n}/{len(jobs)}", flush=True)
    size = sum(p.stat().st_size for p in OUT.rglob("*") if p.is_file())
    print(f"{OUT}: {size / 2**20:.1f} MiB ({total / 2**20:.1f} MiB of blocks before zstd this run)")


if __name__ == "__main__":
    main()
