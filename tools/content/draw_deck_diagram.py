#!/usr/bin/env python3
"""Draw content/ui/deck-controller.png and .json: the Controls page's controller diagram.

A Steam Deck's inputs in the flat style the user chose (2026-09-28): white shapes on dark
grey, button names on them, and a grey leader line from each input to the place where
the page writes what that input does (frontend.cpp controls_page). Drawn here, not
copied: the shapes are simple polygons, circles and rounded rectangles.

Only what a game reads is drawn: L1 L2 R1 R2, View, Menu, the D-pad, both sticks and
A B X Y. The back grips, the touchpads and the Steam / quick access buttons belong to
Steam Input by default (docs/design/steam-deck-controls.md).

The .json names each label slot: the input group it describes, where its text anchors
(canvas pixels) and which way it runs ("left": ends at x, "right": starts at x,
"centre": centred on x). The page reads it at run time, so moving a label needs no C++.

    python tools/content/draw_deck_diagram.py
"""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "content/ui/deck-controller.png"
LAYOUT = ROOT / "content/ui/deck-controller.json"
FONT = ROOT / "build/fonts/HarmonyOS_Sans_SC.ttf"
W, H = 1440, 1080         # 4:3 like the game window; the controller in the middle, labels round it
OX, OY, K = 250, 260, 0.78  # where the controller drawing (1202 x 600 units) sits, and its scale
SS = 3                    # supersampling
BACK, SHAPE, INK, RING, LINE = (35, 35, 35), (244, 244, 244), (35, 35, 35), (92, 92, 92), (128, 128, 128)


def mirror(points):
    return [(1202 - x, y) for x, y in points]


def draw() -> dict:
    img = Image.new("RGB", (W * SS, H * SS), BACK)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(str(FONT), int(46 * K) * SS)
    font.set_variation_by_name("Bold")
    small = ImageFont.truetype(str(FONT), 30 * SS)
    small.set_variation_by_name("Bold")

    def p(x, y):
        return ((OX + K * x) * SS, (OY + K * y) * SS)

    def poly(points, colour=SHAPE):
        pts = [p(x, y) for x, y in points]
        d.polygon(pts, fill=colour)
        # Rounded corners: a thick outline with curved joints. It starts halfway along the
        # last edge, since PIL rounds only the joints between segments, not the ends.
        mid = ((pts[-1][0] + pts[0][0]) / 2, (pts[-1][1] + pts[0][1]) / 2)
        d.line([mid] + pts + [mid], fill=colour, width=int(22 * K) * SS, joint="curve")

    def rrect(x0, y0, x1, y1, r, colour=SHAPE):
        d.rounded_rectangle([p(x0, y0), p(x1, y1)], radius=r * K * SS, fill=colour)

    def circle(cx, cy, r, colour=SHAPE):
        d.ellipse([p(cx - r, cy - r), p(cx + r, cy + r)], fill=colour)

    def text(x, y, value, f=font, colour=INK):
        d.text(p(x, y), value, font=f, fill=colour, anchor="mm")

    def line(points):
        # Canvas pixels: the labels live outside the controller drawing.
        pts = [(x * SS, y * SS) for x, y in points]
        d.line(pts, fill=LINE, width=4 * SS, joint="curve")
        x, y = pts[-1]
        r = 9 * SS
        d.ellipse([x - r, y - r, x + r, y + r], fill=SHAPE, outline=LINE, width=4 * SS)

    # Shoulders: L2 above L1, mirrored on the right.
    l2 = [(80, 76), (178, 56), (214, 66), (236, 124), (232, 136), (82, 174), (72, 164)]
    l1 = [(82, 236), (248, 192), (268, 198), (292, 228), (322, 232), (326, 256), (314, 266), (112, 266), (84, 252)]
    for shape in (l2, l1, mirror(l2), mirror(l1)):
        poly(shape)
    text(150, 118, "L2"); text(228, 236, "L1"); text(1202 - 150, 118, "R2"); text(1202 - 228, 236, "R1")
    # View and Menu.
    rrect(290, 318, 378, 360, 21); rrect(824, 318, 912, 360, 21)
    d.rectangle([p(318, 328), p(340, 344)], outline=INK, width=4 * SS)
    d.rectangle([p(328, 334), p(352, 352)], fill=INK)
    for dy in (329, 338, 347):
        d.rounded_rectangle([p(852, dy), p(884, dy + 4)], radius=2 * SS, fill=INK)
    # D-pad.
    rrect(100, 414, 298, 482, 14); rrect(166, 348, 232, 548, 14)
    # Sticks, with the click glyphs' letters.
    for cx in (467, 735):
        circle(cx, 493, 90, RING); circle(cx, 493, 56)
    # Face buttons.
    for label, (cx, cy) in {"Y": (1000, 360), "X": (910, 447), "B": (1090, 447), "A": (1000, 537)}.items():
        circle(cx, cy, 45); text(cx, cy + 2, label)

    # Leader lines, from where the label ends to the input (dot on the input); canvas
    # pixels. Above the controller: the triggers, View and Menu; beside it: L1, the D-pad,
    # R1 and the face buttons; under it: the sticks, the right one a row lower.
    slots = []

    def at(x, y):
        return (OX + K * x, OY + K * y)

    def slot(group, align, x, y, route):
        line(route)
        slots.append({"group": group, "align": align, "x": round(x), "y": round(y)})

    l2, r2 = at(150, 64), at(1052, 64)
    slot("l2", "centre", l2[0], 110, [(l2[0], 132), l2])
    slot("r2", "centre", r2[0], 110, [(r2[0], 132), r2])
    view, menu = at(348, 318), at(854, 318)  # right of L1's tip, left of R1's
    slot("view", "centre", view[0], 220, [(view[0], 242), view])
    slot("menu", "centre", menu[0], 220, [(menu[0], 242), menu])
    l1, r1 = at(84, 244), at(1118, 244)
    slot("l1", "left", 282, l1[1], [(290, l1[1]), l1])
    slot("r1", "right", 1158, r1[1], [(1150, r1[1]), r1])
    dpad = at(100, 448)
    slot("dpad", "left", 282, dpad[1], [(290, dpad[1]), dpad])
    for group, (cx, cy) in {"y": (1045, 360), "b": (1135, 447), "a": (1045, 537)}.items():
        point = at(cx, cy)
        slot(group, "right", 1158, point[1], [(1150, point[1]), point])
    x_bottom = at(910, 492)
    slot("x", "right", 1158, 772, [(1150, 772), (x_bottom[0], 772), x_bottom])
    ls, rs = at(467, 583), at(735, 583)
    slot("ls", "centre", ls[0], 830, [(ls[0], 808), ls])
    slot("rs", "centre", rs[0], 920, [(rs[0], 898), rs])
    img = img.resize((W, H), Image.Resampling.LANCZOS)
    img.save(OUTPUT, optimize=True)
    return {"schema": "srw64.controller-diagram.v1", "image": OUTPUT.name, "width": W, "height": H, "slots": slots}


def main() -> int:
    layout = draw()
    LAYOUT.write_text(json.dumps(layout, indent=1) + "\n")
    print(OUTPUT, OUTPUT.stat().st_size, "bytes,", len(layout["slots"]), "label slots")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
