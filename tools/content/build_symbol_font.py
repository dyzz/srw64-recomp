#!/usr/bin/env python3
"""Build content/fonts/SRW64Symbols.ttf: the four game symbols HarmonyOS Sans lacks.

The glyph map (reference/original-glyph-map.csv) shows these ROM tiles as ▶ ▷ ◀ and 🔧;
HarmonyOS Sans SC and Condensed have none of them, so the text engine falls back to this
font after them (docs/design/dialogue-typesetting.md).

- ▶ ▷ ◀ are DejaVu Sans outlines (Bitstream Vera licence, DejaVu changes in the public
  domain; see content/fonts/LICENSE-SRW64Symbols.txt), scaled to the size and advance the
  current fallback Arial Unicode MS uses, so existing page layouts keep their widths.
- 🔧 is drawn here after the ROM's repair icon (tile 258): a combination wrench on the
  diagonal, open jaw at the top right and ring at the bottom left. Apple Color Emoji is
  the only installed font with this character and cannot be redistributed.

Vertical metrics copy HarmonyOS Sans SC so a fallback never raises a line's ascent.
Needs fontTools (pip install fonttools); the built font is tracked, so this runs only
when the glyphs change.

    python tools/content/build_symbol_font.py --dejavu /path/to/DejaVuSans.ttf
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.misc.timeTools import timestampFromString
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "content/fonts/SRW64Symbols.ttf"
UPEM = 1000
ASCENT, DESCENT = 928, -244          # HarmonyOS Sans SC hhea
TRIANGLE_ADVANCE = 600               # Arial Unicode MS: 1229/2048 em
TRIANGLE_HEIGHT = 422                # Arial Unicode MS: 864/2048 em
TRIANGLE_CENTRE = 375                # Arial Unicode MS: (336+1200)/2 /2048 em
WRENCH_BOX, WRENCH_CENTRE = 800, (500, 360)
TRIANGLES = {0x25B6: "uni25B6", 0x25B7: "uni25B7", 0x25C0: "uni25C0"}
WRENCH = (0x1F527, "u1F527")


def triangle(source: TTFont, name: str) -> tuple:
    glyphs = source.getGlyphSet()
    bounds = BoundsPen(glyphs)
    glyphs[name].draw(bounds)
    x0, y0, x1, y1 = bounds.bounds
    scale = TRIANGLE_HEIGHT / (y1 - y0)
    dx = (TRIANGLE_ADVANCE - (x1 - x0) * scale) / 2 - x0 * scale
    dy = TRIANGLE_CENTRE - (y0 + y1) / 2 * scale
    pen = TTGlyphPen(None)
    glyphs[name].draw(TransformPen(pen, (scale, 0, 0, scale, dx, dy)))
    return pen.glyph(), TRIANGLE_ADVANCE


def arc(cx, cy, r, a0, a1):
    """Quadratic segments along a circle from angle a0 to a1 (either direction)."""
    steps = max(1, math.ceil(abs(a1 - a0) / (math.pi / 4)))
    out = []
    for i in range(steps):
        s, e = a0 + (a1 - a0) * i / steps, a0 + (a1 - a0) * (i + 1) / steps
        m, k = (s + e) / 2, r / math.cos((e - s) / 2)
        out.append(("q", (cx + k * math.cos(m), cy + k * math.sin(m)), (cx + r * math.cos(e), cy + r * math.sin(e))))
    return out


def wrench_contours():
    """Contours in a local frame: ring at the origin, jaw along +x. Clockwise = filled."""
    ring_out, ring_in, length, jaw, mouth, handle = 150, 78, 620, 165, 70, 55
    notch = length - 0.2 * jaw
    contours = []
    contours.append([("m", (ring_out, 0))] + arc(0, 0, ring_out, 0, -2 * math.pi))
    contours.append([("m", (ring_in, 0))] + arc(0, 0, ring_in, 0, 2 * math.pi))       # the ring's hole
    start = ring_in + 0.4 * (ring_out - ring_in)
    end = length - 0.5 * jaw
    contours.append([("m", (start, handle)), ("l", (end, handle)), ("l", (end, -handle)), ("l", (start, -handle))])
    t = math.asin(mouth / jaw)
    jaw_path = [("m", (length + jaw * math.cos(t), -mouth))] + arc(length, 0, jaw, 2 * math.pi - t, t)
    jaw_path += [("l", (notch, mouth)), ("l", (notch, -mouth))]
    contours.append(jaw_path)
    return contours


def wrench() -> tuple:
    angle = math.pi / 4
    c, s = math.cos(angle), math.sin(angle)
    contours = wrench_contours()
    rotated = [[(kind, *[(x * c - y * s, x * s + y * c) for x, y in pts]) for kind, *pts in contour] for contour in contours]
    xs = [x for contour in rotated for _, *pts in contour for x, _ in pts]
    ys = [y for contour in rotated for _, *pts in contour for _, y in pts]
    scale = WRENCH_BOX / max(max(xs) - min(xs), max(ys) - min(ys))
    ox = WRENCH_CENTRE[0] - (max(xs) + min(xs)) / 2 * scale
    oy = WRENCH_CENTRE[1] - (max(ys) + min(ys)) / 2 * scale
    pen = TTGlyphPen(None)
    place = lambda p: (round(ox + p[0] * scale), round(oy + p[1] * scale))  # noqa: E731
    for contour in rotated:
        for kind, *pts in contour:
            if kind == "m":
                pen.moveTo(place(pts[0]))
            elif kind == "l":
                pen.lineTo(place(pts[0]))
            else:
                pen.qCurveTo(place(pts[0]), place(pts[1]))
        pen.closePath()
    return pen.glyph(), UPEM


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dejavu", type=Path, required=True, help="DejaVuSans.ttf (Book)")
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    source = TTFont(args.dejavu)
    if source["name"].getDebugName(4) != "DejaVu Sans":
        raise SystemExit(f"{args.dejavu} is not DejaVu Sans Book")
    cmap = source.getBestCmap()
    glyphs, advances = {".notdef": TTGlyphPen(None).glyph(), "space": TTGlyphPen(None).glyph()}, {".notdef": 500, "space": 250}
    for code, name in TRIANGLES.items():
        glyphs[name], advances[name] = triangle(source, cmap[code])
    glyphs[WRENCH[1]], advances[WRENCH[1]] = wrench()
    order = list(glyphs)
    builder = FontBuilder(UPEM, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({0x20: "space", **TRIANGLES, WRENCH[0]: WRENCH[1]})
    builder.setupGlyf(glyphs)
    glyf = builder.font["glyf"]
    builder.setupHorizontalMetrics({name: (advances[name], getattr(glyf[name], "xMin", 0)) for name in order})
    builder.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    builder.setupNameTable({
        "familyName": "SRW64 Symbols", "styleName": "Regular", "uniqueFontIdentifier": "SRW64 Symbols Regular 1.0",
        "fullName": "SRW64 Symbols", "psName": "SRW64Symbols-Regular", "version": "Version 1.0",
        "copyright": "Triangles from DejaVu Sans: Copyright (c) 2003 by Bitstream, Inc. (Bitstream Vera licence); "
                     "DejaVu changes are in the public domain. Wrench glyph drawn for the SRW64 project.",
        "licenseDescription": "See LICENSE-SRW64Symbols.txt next to this font.",
    })
    builder.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT, sTypoLineGap=0,
                     usWinAscent=ASCENT, usWinDescent=-DESCENT, fsType=0, achVendID="NONE")
    builder.setupPost()
    # Fixed timestamps: the same inputs give the same bytes.
    builder.font.recalcTimestamp = False
    builder.font["head"].created = builder.font["head"].modified = timestampFromString("Wed Sep 23 00:00:00 2026")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    builder.save(args.out)
    print(args.out, hashlib.sha256(args.out.read_bytes()).hexdigest(), args.out.stat().st_size, "bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
