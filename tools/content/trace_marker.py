#!/usr/bin/env python3
"""Trace a white-on-black icon image into the SVG outline build_symbol_font.py reads.

The line work is thickened to STROKE (a fraction of the icon's height) so a traced icon
matches the weight of the drawn ones, then traced with potrace (pip install potracer).
The SVG is y-up, one even-odd path of M/L/C/Z commands.

    python tools/content/trace_marker.py IMAGE content/fonts/marker-fist.svg 0.075
"""
import argparse
from pathlib import Path

import numpy as np
import potrace
from PIL import Image, ImageFilter


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path)
    parser.add_argument("svg", type=Path)
    parser.add_argument("stroke", type=float, help="line width as a fraction of the icon height")
    args = parser.parse_args()
    mask = Image.open(args.image).convert("L").point(lambda v: 255 if v > 128 else 0)
    mask = mask.crop(mask.getbbox())
    # The line width: the median white run across sampled rows.
    runs = []
    for row in np.array(mask)[::7] > 0:
        edges = np.flatnonzero(np.diff(np.concatenate(([0], row.astype(int), [0]))))
        runs += list(edges[1::2] - edges[::2])
    grow = max(0.0, (args.stroke * mask.height - float(np.median(runs))) / 2)
    pad = int(grow) + 8
    big = Image.new("L", (mask.width + 2 * pad, mask.height + 2 * pad), 0)
    big.paste(mask, (pad, pad))
    if grow > 0:  # a round dilation: blur, then keep where the blurred edge reaches
        big = big.filter(ImageFilter.GaussianBlur(grow)).point(lambda v: 255 if v > 18 else 0)
    path = potrace.Bitmap(np.array(big) == 0).trace(turdsize=20, alphamax=1.0, opticurve=True, opttolerance=0.2)
    height = big.height
    commands = []
    for curve in path:
        start = curve.start_point
        commands.append(f"M{start.x:.2f},{height - start.y:.2f}")
        for s in curve.segments:
            if s.is_corner:
                commands.append(f"L{s.c.x:.2f},{height - s.c.y:.2f}L{s.end_point.x:.2f},{height - s.end_point.y:.2f}")
            else:
                commands.append(f"C{s.c1.x:.2f},{height - s.c1.y:.2f} {s.c2.x:.2f},{height - s.c2.y:.2f} "
                                f"{s.end_point.x:.2f},{height - s.end_point.y:.2f}")
        commands.append("Z")
    args.svg.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {big.width} {height}">\n'
                        f'<!-- traced by tools/content/trace_marker.py from {args.image.name}, stroke {args.stroke} -->\n'
                        f'<path fill-rule="evenodd" d="{" ".join(commands)}"/></svg>\n')
    print(args.svg, len(path), "curves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
