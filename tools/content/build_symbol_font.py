#!/usr/bin/env python3
"""Build content/fonts/SRW64Symbols.ttf: the game symbols HarmonyOS Sans lacks.

The glyph map (reference/original-glyph-map.csv) shows these ROM tiles as ▶ ▷ ◀ and 🔧;
HarmonyOS Sans SC and Condensed have none of them, so the text engine falls back to this
font after them (docs/design/dialogue-typesetting.md).

- ▶ ▷ ◀ are DejaVu Sans outlines (Bitstream Vera licence, DejaVu changes in the public
  domain; see content/fonts/LICENSE-SRW64Symbols.txt), scaled to the size and advance the
  current fallback Arial Unicode MS uses, so existing page layouts keep their widths.
- 🔧 is drawn here after the ROM's repair icon (tile 258): a combination wrench on the
  diagonal, open jaw at the top right and ring at the bottom left. Apple Color Emoji is
  the only installed font with this character and cannot be redistributed.

- The weapon markers the ROM font draws in weapon menus, drawn here after those icons
  at U+E000 + their ROM glyph id (the Private Use Area; HarmonyOS Sans has nothing
  there): U+E0F4 the 格闘 fist, U+E0F3 the 射撃 crosshair, U+E0F1 circled P (usable
  after moving), U+E0F2 circled B (beam) and U+E23F the MAP badge, its letters cut out.
  The letters are DejaVu Sans Bold outlines. The fist is content/fonts/marker-fist.svg:
  a Qwen Image 3.0 Pro redrawing of the ROM icon (a fist pointing left, the thumb on
  top), chosen by the user and traced with potrace, its lines thickened to match.

Vertical metrics copy HarmonyOS Sans SC so a fallback never raises a line's ascent.
Needs fontTools and skia-pathops (pip install fonttools skia-pathops); the built font
is tracked, so this runs only when the glyphs change.

    python tools/content/build_symbol_font.py --dejavu /path/to/DejaVuSans.ttf \
        --dejavu-bold /path/to/DejaVuSans-Bold.ttf
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.misc.timeTools import timestampFromString
import re

import pathops
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.reverseContourPen import ReverseContourPen
from fontTools.pens.roundingPen import RoundingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "content/fonts/SRW64Symbols.ttf"
FIST = ROOT / "content/fonts/marker-fist.svg"
UPEM = 1000
ASCENT, DESCENT = 928, -244          # HarmonyOS Sans SC hhea
TRIANGLE_ADVANCE = 600               # Arial Unicode MS: 1229/2048 em
TRIANGLE_HEIGHT = 422                # Arial Unicode MS: 864/2048 em
TRIANGLE_CENTRE = 375                # Arial Unicode MS: (336+1200)/2 /2048 em
WRENCH_BOX, WRENCH_CENTRE = 800, (500, 360)
TRIANGLES = {0x25B6: "uni25B6", 0x25B7: "uni25B7", 0x25C0: "uni25C0"}
WRENCH = (0x1F527, "u1F527")
# Weapon markers: ROM glyph id -> glyph name, at U+E000 + id. One band 0.76 em tall,
# centred on the triangles' line; narrow ones are 8:10 like the ROM icons, MAP 13:10.
MARKERS = {241: "uniE0F1", 242: "uniE0F2", 243: "uniE0F3", 244: "uniE0F4", 575: "uniE23F"}
ICON_BOTTOM, ICON_HEIGHT = -20, 760
NARROW, WIDE, BEARING, STROKE = 600, 980, 40, 80


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


class Outline:
    """Contours in font units, rounded when drawn. Clockwise fills, anticlockwise cuts."""

    def __init__(self):
        self.pen = TTGlyphPen(None)
        self.out = RoundingPen(self.pen)

    def path(self, path, clockwise=True):
        """m/l/q commands drawn clockwise; anticlockwise reverses them."""
        target = self.out if clockwise else ReverseContourPen(self.out)
        for kind, *pts in path:
            if kind == "m":
                target.moveTo(pts[0])
            elif kind == "l":
                target.lineTo(pts[0])
            else:
                target.qCurveTo(*pts)
        target.closePath()

    def rect(self, x0, y0, x1, y1, clockwise=True):
        self.path([("m", (x0, y0)), ("l", (x0, y1)), ("l", (x1, y1)), ("l", (x1, y0))], clockwise)

    def rounded(self, x0, y0, x1, y1, r, clockwise=True):
        """A rectangle whose corners are quadratic quarter curves of radius r (or (rx, ry))."""
        rx, ry = r if isinstance(r, tuple) else (r, r)
        self.path([("m", (x0, y0 + ry)), ("l", (x0, y1 - ry)), ("q", (x0, y1), (x0 + rx, y1)),
                   ("l", (x1 - rx, y1)), ("q", (x1, y1), (x1, y1 - ry)),
                   ("l", (x1, y0 + ry)), ("q", (x1, y0), (x1 - rx, y0)),
                   ("l", (x0 + rx, y0)), ("q", (x0, y0), (x0, y0 + ry))], clockwise)

    def ellipse(self, cx, cy, rx, ry, clockwise=True):
        """Eight quadratic segments, drawn clockwise from the rightmost point."""
        k = 1 / math.cos(math.pi / 8)
        path = [("m", (cx + rx, cy))]
        for i in range(8):
            a0, a1 = -i * math.pi / 4, -(i + 1) * math.pi / 4
            m = (a0 + a1) / 2
            path.append(("q", (cx + rx * k * math.cos(m), cy + ry * k * math.sin(m)),
                         (cx + rx * math.cos(a1), cy + ry * math.sin(a1))))
        self.path(path[:-1] + [("q", path[-1][1], (cx + rx, cy))], clockwise)

    def letters(self, font: TTFont, text: str, centre, height, width=None, tracking=0, cut=False):
        """DejaVu letters scaled to a cap height, centred; cut=True makes them holes."""
        glyphs, cmap = font.getGlyphSet(), font.getBestCmap()
        names = [cmap[ord(c)] for c in text]
        advances = [font["hmtx"][n][0] for n in names]
        bounds = BoundsPen(glyphs)
        for n in names:
            glyphs[n].draw(bounds)
        x0, y0, x1, y1 = bounds.bounds
        total = sum(advances) + tracking * (len(names) - 1)
        scale = height / (y1 - y0)
        if width:
            scale = min(scale, width / total)
        x = centre[0] - total * scale / 2
        dy = centre[1] - (y0 + y1) / 2 * scale
        for n, advance in zip(names, advances):
            target = ReverseContourPen(self.out) if cut else self.out
            glyphs[n].draw(TransformPen(target, (scale, 0, 0, scale, x, dy)))
            x += (advance + tracking) * scale

    def glyph(self):
        return self.pen.glyph()


def traced_marker(svg: Path) -> tuple:
    """An even-odd SVG path (M/L/C/Z, y up) scaled into the icon band, its winding fixed."""
    view = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg.read_text()).group(1).split()]
    data = re.search(r' d="([^"]+)"', svg.read_text()).group(1)
    width, height = view[2], view[3]
    scale = ICON_HEIGHT / height
    place = lambda x, y: (BEARING + float(x) * scale, ICON_BOTTOM + float(y) * scale)  # noqa: E731
    path = pathops.Path(fillType=pathops.FillType.EVEN_ODD)
    pen = path.getPen()
    for command, args in re.findall(r"([MLCZ])([^MLCZ]*)", data):
        numbers = re.findall(r"-?\d+(?:\.\d+)?", args)
        points = [place(numbers[i], numbers[i + 1]) for i in range(0, len(numbers), 2)]
        if command == "M":
            pen.moveTo(points[0])
        elif command == "L":
            pen.lineTo(points[0])
        elif command == "C":
            pen.curveTo(*points)
        else:
            pen.closePath()
    path.simplify(fix_winding=True)
    glyph_pen = TTGlyphPen(None)
    path.draw(Cu2QuPen(RoundingPen(glyph_pen), 1.0, reverse_direction=False))
    return glyph_pen.glyph(), round(width * scale) + 2 * BEARING


def markers(bold: TTFont) -> dict:
    """The five weapon markers, after the ROM icons (reference/original-glyph-map.csv)."""
    top = ICON_BOTTOM + ICON_HEIGHT
    mid = ICON_BOTTOM + ICON_HEIGHT / 2
    x0, x1 = BEARING, BEARING + NARROW
    cx = (x0 + x1) / 2
    built = {}
    # Circled P and B: a ring filling the band, the letter bold inside.
    for glyph_id, letter in ((241, "P"), (242, "B")):
        o = Outline()
        o.ellipse(cx, mid, NARROW / 2, ICON_HEIGHT / 2)
        o.ellipse(cx, mid, NARROW / 2 - STROKE, ICON_HEIGHT / 2 - STROKE, clockwise=False)
        o.letters(bold, letter, (cx, mid), 400)
        built[glyph_id] = (o.glyph(), NARROW + 2 * BEARING)
    # 射撃: corner brackets around a thick plus, a gunsight.
    o = Outline()
    arm_x, arm_y = 210, 250
    for sx, sy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        ex = x1 if sx else x0
        ey = top if sy else ICON_BOTTOM
        hx = ex - arm_x if sx else ex + arm_x
        vy = ey - arm_y if sy else ey + arm_y
        o.rect(min(ex, hx), min(ey, ey - STROKE if sy else ey + STROKE), max(ex, hx), max(ey, ey - STROKE if sy else ey + STROKE))
        o.rect(min(ex, ex - STROKE if sx else ex + STROKE), min(ey, vy), max(ex, ex - STROKE if sx else ex + STROKE), max(ey, vy))
    bar = 150
    o.rect(cx - 220, mid - bar / 2, cx + 220, mid + bar / 2)
    o.rect(cx - bar / 2, mid - 230, cx + bar / 2, mid + 230)
    built[243] = (o.glyph(), NARROW + 2 * BEARING)
    built[244] = traced_marker(FIST)
    # MAP: a rounded red badge in the ROM; the glyph is the badge with the letters cut out.
    o = Outline()
    o.rounded(BEARING, ICON_BOTTOM, BEARING + WIDE, top, (220, 230))
    o.letters(bold, "MAP", (BEARING + WIDE / 2, mid), 430, width=WIDE - 220, tracking=-30, cut=True)
    built[575] = (o.glyph(), WIDE + 2 * BEARING)
    return built


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dejavu", type=Path, required=True, help="DejaVuSans.ttf (Book)")
    parser.add_argument("--dejavu-bold", type=Path, required=True, help="DejaVuSans-Bold.ttf")
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
    bold = TTFont(args.dejavu_bold)
    if bold["name"].getDebugName(4) != "DejaVu Sans Bold":
        raise SystemExit(f"{args.dejavu_bold} is not DejaVu Sans Bold")
    for glyph_id, (glyph, advance) in markers(bold).items():
        glyphs[MARKERS[glyph_id]], advances[MARKERS[glyph_id]] = glyph, advance
    order = list(glyphs)
    builder = FontBuilder(UPEM, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({0x20: "space", **TRIANGLES, WRENCH[0]: WRENCH[1],
                               **{0xE000 + glyph_id: name for glyph_id, name in MARKERS.items()}})
    builder.setupGlyf(glyphs)
    glyf = builder.font["glyf"]
    builder.setupHorizontalMetrics({name: (advances[name], getattr(glyf[name], "xMin", 0)) for name in order})
    builder.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    builder.setupNameTable({
        "familyName": "SRW64 Symbols", "styleName": "Regular", "uniqueFontIdentifier": "SRW64 Symbols Regular 1.0",
        "fullName": "SRW64 Symbols", "psName": "SRW64Symbols-Regular", "version": "Version 1.0",
        "copyright": "Triangles from DejaVu Sans: Copyright (c) 2003 by Bitstream, Inc. (Bitstream Vera licence); "
                     "DejaVu changes are in the public domain. Weapon marker letters from DejaVu Sans Bold. "
                     "Wrench and weapon marker glyphs drawn for the SRW64 project.",
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
