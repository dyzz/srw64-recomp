#!/usr/bin/env python3
"""RT64's pictures beside the Vita renderer's replay of the same display lists
(docs/design/psv-port.md, V1).

For each <n>.rt64.png / <n>.gl.png pair in the dump folders (check_gbi_usage.py --dump, then
srw64-gl-replay), the central 4:3 of RT64's window shot and the GL picture are scaled to
320x240 and compared: the mean absolute difference per channel and the share of pixels
that differ by more than 48 in any channel. Writes <n>.compare.png (RT64 | GL | difference)
and prints one line per frame, worst first, with a summary per folder.

  compare_gl_replay.py <dump folder>... [--top N]
"""
import argparse
from pathlib import Path
import sys

from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("folders", nargs="+")
parser.add_argument("--top", type=int, default=10)
args = parser.parse_args()

SIZE = (320, 240)
rows = []
for folder in map(Path, args.folders):
    scores = []
    for gl_path in sorted(folder.glob("*.gl.png")):
        name = gl_path.name.split(".")[0]
        rt_path = folder / f"{name}.rt64.png"
        if not rt_path.exists():
            continue
        rt = Image.open(rt_path).convert("RGB")
        w, h = rt.size
        picture = round(h * 4 / 3)
        rt = rt.crop(((w - picture) // 2, 0, (w - picture) // 2 + picture, h)).resize(SIZE, Image.BILINEAR)
        gl = Image.open(gl_path).convert("RGB").resize(SIZE, Image.BILINEAR)
        diff = ImageChops.difference(rt, gl)
        pixels = list(diff.getdata())
        mean = sum(sum(p) for p in pixels) / (len(pixels) * 3)
        far = sum(1 for p in pixels if max(p) > 48) / len(pixels)
        sheet = Image.new("RGB", (SIZE[0] * 3, SIZE[1]))
        sheet.paste(rt, (0, 0))
        sheet.paste(gl, (SIZE[0], 0))
        sheet.paste(diff.point(lambda v: min(255, v * 3)), (SIZE[0] * 2, 0))
        sheet.save(folder / f"{name}.compare.png")
        scores.append((far, mean, f"{folder.name}/{name}"))
    if scores:
        print(f"{folder.name}: {len(scores)} frames, mean difference {sum(s[1] for s in scores) / len(scores):.1f}, "
              f"pixels off {100 * sum(s[0] for s in scores) / len(scores):.1f}%")
    rows += scores
for far, mean, name in sorted(rows, reverse=True)[: args.top]:
    print(f"  {name}: {100 * far:.1f}% off, mean {mean:.1f}")
sys.exit(0)
