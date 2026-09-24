"""Author the HD Medea (world-map transport, original resource 5587) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/medea_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z nose, +Y up,
units of the N64 mesh, 75 units = 67.7 m span, so 1.108 units per metre), model.glb
and optional previews with the original rendered from the same cameras (compare.png).
The Medea is the game's fallback world-map vehicle (model table index 24 -> 3).

Design reference: Earth Federation Medea transport from Mobile Suit Gundam
(mechanical design Kunio Okawara). Official figures used: length 45.0 m, span
67.7 m, height 15.9 m, 160 t payload; six jet engines for cruise and lift rotors
for VTOL; twin AA guns under the cockpit (Gundam Channel mechanic manual). Layout
from the TV settei (front and rear views) and the 1983 1/550 kit box art: the
fuselage sits on a three-legged tower ("三脚のやぐら") with a lift-fan pod and
wheels at each foot, the exchangeable cargo container hangs between the legs,
high wing with four turbofans side by side on top and one at each wingtip, the
tips outboard of them drooping down, and twin tail booms ending in fins with
outboard stabilisers. Yellow airframe, grey-green container, blue-violet lift fans
and blue cockpit glazing as in the colour settei.

Sources: https://www.gundam-c.com/manual/mechanic/gundam/midia.html,
https://ja.wikipedia.org/wiki/%E3%83%9F%E3%83%87%E3%82%A2,
https://gundam.fandom.com/wiki/Medea (settei scans, 1/550 box art).
Reference images were only viewed for comparison; none are stored.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'hull': (244, 202, 76, 255), 'panel': (228, 182, 62, 255), 'hulldark': (184, 138, 44, 255),
    'container': (136, 150, 146, 255), 'containerdark': (104, 116, 114, 255), 'fan': (98, 100, 178, 255),
    'glass': (126, 170, 212, 255), 'intake': (58, 60, 68, 255), 'spinner': (178, 180, 184, 255),
    'tyre': (38, 38, 42, 255),
})
loft, box, cylinder, sphere, plate, mirrored, add_object = (KIT.loft, KIT.box, KIT.cylinder, KIT.sphere,
                                                          KIT.plate, KIT.mirrored, KIT.add_object)


def ring_loft(rings, color, bevel=0.0, cap=True, name='ring-loft', segments=2):
    """Loft through rings of (x, up, fwd) points in any orientation (spanwise wings, struts)."""
    bm = bmesh.new()
    verts = [[bm.verts.new(P(*p)) for p in ring] for ring in rings]
    n = len(verts[0])
    for a, b in zip(verts, verts[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    if cap:
        bm.faces.new(list(reversed(verts[0])))
        bm.faces.new(verts[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return add_object(bm, color, bevel, segments=segments, name=name)


def lerp_table(table, key, col):
    """Linear interpolation in a table of rows sorted by descending key (row[0])."""
    for a, b in zip(table, table[1:]):
        if b[0] <= key <= a[0]:
            t = (key - a[0]) / (b[0] - a[0])
            return a[col] + (b[col] - a[col]) * t
    return table[0][col] if key > table[0][0] else table[-1][col]


def rounded(hw, top, bottom, n=20, p=2.8, cx=0.0):
    """Superellipse ring (x, up) about a vertical centre line at cx."""
    mid, hh = (top + bottom) / 2, (top - bottom) / 2
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        pts.append((cx + hw * math.copysign(abs(c) ** (2 / p), c), mid + hh * math.copysign(abs(s) ** (2 / p), s)))
    return pts


def circle(r, cx, cy, n=24):
    return [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


# ---------------------------------------------------------------- fuselage
# Stations: (fwd, half width, top, bottom). Bulbous cockpit nose, body under the wing
# centre section, short tail cone that lifts toward the wing trailing edge.
BODY = [(25.2, 1.5, 5.3, 3.3), (24.8, 2.5, 6.0, 2.5), (24.0, 3.2, 6.6, 2.0), (22.4, 3.6, 7.0, 1.8),
        (19.0, 3.8, 7.2, 1.7), (12.0, 3.9, 7.2, 1.7), (4.0, 4.1, 7.2, 1.8), (-2.0, 4.0, 7.1, 2.1),
        (-5.5, 3.2, 6.9, 3.2), (-7.5, 1.9, 6.6, 4.6), (-8.2, 0.9, 6.4, 5.4)]
loft([(f, rounded(hw, t, b)) for f, hw, t, b in BODY], 'hull', name='fuselage')


def body_hw(f):
    return lerp_table(BODY, f, 1)


# Cockpit glazing wrapping the upper nose, side windows, belly strake and twin AA guns.
sphere((0, 5.75, 23.55), 1.0, 'glass', scale=(2.75, 1.05, 1.75), segments=24)
box((0, 6.9, 22.0), (0.35, 0.3, 2.4), 'panel', bevel=0.05)
for i in range(5):
    f = 20.6 - i * 1.3
    mirrored(lambda s, f=f: box((s * (body_hw(f) - 0.02), 5.5, f), (0.12, 0.55, 0.7), 'window', bevel=0.02))
mirrored(lambda s: box((s * (body_hw(8.0) + 0.02), 4.4, 8.0), (0.06, 0.12, 20.0), 'hulldark'))
box((0, 1.65, 12.0), (1.8, 0.2, 18.0), 'panel', bevel=0.05)
box((0, 2.3, 23.2), (1.8, 0.8, 1.2), 'panel', bevel=0.1)
mirrored(lambda s: cylinder((s * 0.45, 2.2, 23.6), (s * 0.45, 2.2, 25.3), 0.13, 'dark', segments=8))

# ---------------------------------------------------------------- wing
# Spanwise stations: (x, leading edge, trailing edge, mid-plane height, thickness).
WING = [(2.0, 8.0, -3.4, 6.9, 1.2), (10.0, 7.6, -3.2, 7.0, 1.05), (20.0, 6.4, -2.8, 7.1, 0.85),
        (29.5, 5.2, -2.3, 7.2, 0.7), (31.8, 4.9, -2.2, 7.2, 0.66)]
TIP = (37.5, 1.6, -4.4, 3.7, 0.4)
FRACTIONS = [0.0, 0.06, 0.2, 0.42, 0.7, 1.0]
HALF = [0.3, 0.8, 1.0, 0.9, 0.55, 0.12]


def airfoil(le, te, up, t):
    chord = le - te
    top = [(up + t / 2 * h, le - s * chord) for s, h in zip(FRACTIONS, HALF)]
    bottom = [(up - t / 2 * h * 0.7, le - s * chord) for s, h in zip(FRACTIONS, HALF)]
    return top + bottom[::-1]


def wing_at(x, col):
    return lerp_table(sorted(WING, key=lambda r: -r[0]), x, col)


def wing_top(x, f):
    le, te, up, t = (wing_at(x, c) for c in (1, 2, 3, 4))
    s = min(max((le - f) / (le - te), 0.0), 1.0)
    h = HALF[0]
    for (s0, h0), (s1, h1) in zip(zip(FRACTIONS, HALF), zip(FRACTIONS[1:], HALF[1:])):
        if s0 <= s <= s1:
            h = h0 + (h1 - h0) * (s - s0) / (s1 - s0)
    return up + t / 2 * h


def wing(side):
    ring_loft([[(side * x, u, f) for u, f in airfoil(le, te, up, t)] for x, le, te, up, t in WING],
              'hull', bevel=0.05, name='wing')
    # Drooped tip outboard of the tip engine, swept back.
    x0, le0, te0, up0, t0 = WING[-1]
    ring_loft([[(side * x, u, f) for u, f in airfoil(le, te, up, t)] for x, le, te, up, t in
               ((x0 - 0.3, le0, te0, up0, t0), (x0 + 1.2, le0 - 0.6, te0 - 0.3, up0 - 0.5, t0 * 0.9), TIP)],
              'hull', bevel=0.04, name='wingtip')
    # Trailing-edge flaps (ochre), panel seams and a walkway line on top.
    for x_in, x_out in ((5.0, 12.0), (15.0, 22.0), (22.6, 29.0)):
        rings = []
        for x in (x_in, x_out):
            te = wing_at(x, 2)
            fore, aft = te + 1.8, te + 0.15
            rings.append([(side * x, wing_top(x, fore) + 0.02, fore), (side * x, wing_top(x, aft) + 0.02, aft),
                          (side * x, wing_top(x, aft) + 0.1, aft), (side * x, wing_top(x, fore) + 0.1, fore)])
        ring_loft(rings, 'panel', name='flap')
    for x in (14.3, 22.3, 29.0):
        le, te = wing_at(x, 1), wing_at(x, 2)
        box((side * x, wing_top(x, (le + te) / 2) + 0.04, (le + te) / 2), (0.1, 0.05, (le - te) * 0.85), 'hulldark')
    rings = []
    for x in (13.0, 31.0):
        f = wing_at(x, 1) - 1.0
        rings.append([(side * x, wing_top(x, f) + 0.02, f + 0.08), (side * x, wing_top(x, f) + 0.02, f - 0.08),
                      (side * x, wing_top(x, f) + 0.08, f - 0.08), (side * x, wing_top(x, f) + 0.08, f + 0.08)])
    ring_loft(rings, 'hulldark', name='seam')


mirrored(wing)


# ---------------------------------------------------------------- jet engines
def turbofan(x, y, front, rear, r, color='hull'):
    """Nacelle with intake lip, fan face and spinner in front and an open exhaust behind."""
    length = front - rear
    stations = [(front, 0.88), (front - 0.3, 0.98), (front - 0.12 * length, 1.0), (front - 0.55 * length, 1.03),
                (rear + 0.22 * length, 0.96), (rear + 0.05 * length, 0.8), (rear, 0.72)]
    loft([(f, circle(r * k, x, y)) for f, k in stations], color, bevel=0.03, cap=False, name='nacelle')
    cylinder((x, y, front - 0.5), (x, y, front - 0.6), r * 0.86, 'intake', segments=24)
    cylinder((x, y, front - 0.55), (x, y, front + 0.35), r * 0.36, 'spinner', segments=16, radius_b=0.06)
    cylinder((x, y, rear + 0.9), (x, y, rear - 0.2), r * 0.72, 'nozzle', segments=24, radius_b=r * 0.62, cap=False)
    cylinder((x, y, rear + 0.45), (x, y, rear + 0.35), r * 0.62, 'glow', segments=24)
    # Seam rings on the cowling.
    for f in (front - 0.3 * length, rear + 0.3 * length):
        cylinder((x, y, f - 0.05), (x, y, f + 0.05), r * 1.035, 'panel', segments=24)


TOP_ENGINES = [(-9.6, 10.9, -3.0), (-3.2, 10.9, -3.0), (3.2, 10.9, -3.0), (9.6, 10.9, -3.0)]
for x, front, rear in TOP_ENGINES:
    turbofan(x, 9.9, front, rear, 2.55)
    box((x, 7.8, 3.4), (2.4, 1.2, 9.4), 'panel', bevel=0.12)  # saddle fairing to the wing
mirrored(lambda s: turbofan(s * 31.8, 7.05, 6.9, -2.7, 1.95))


# ---------------------------------------------------------------- tail booms
def boom(side):
    x = side * 13.5
    loft([(3.0, rounded(0.9, 7.8, 6.1, cx=x)), (-4.5, rounded(0.95, 7.9, 6.0, cx=x)),
          (-16.0, rounded(0.8, 7.7, 6.3, cx=x)), (-24.2, rounded(0.6, 7.5, 6.6, cx=x)),
          (-24.7, rounded(0.3, 7.3, 6.9, cx=x))], 'hull', name='boom')
    # Swept fin with a darker rudder, outboard stabiliser with elevator line.
    plate([(x - 0.17, 7.4, -15.0), (x - 0.17, 12.9, -22.6), (x - 0.17, 12.9, -24.9), (x - 0.17, 7.3, -24.4)],
          0.34, 'hull', bevel=0.05)
    plate([(x - 0.2, 7.9, -23.0), (x - 0.2, 12.6, -24.15), (x - 0.2, 12.6, -24.85), (x - 0.2, 7.9, -24.35)],
          0.4, 'panel')
    plate([(x, 7.05, -17.5), (x, 7.05, -24.0), (x + side * 7.2, 7.75, -24.6), (x + side * 7.2, 7.75, -22.3)],
          0.3, 'hull', bevel=0.04)
    plate([(x + side * 0.9, 7.2, -23.3), (x + side * 0.9, 7.2, -24.05), (x + side * 7.0, 7.82, -24.55),
           (x + side * 7.0, 7.82, -23.95)], 0.34, 'panel')
    box((x, 12.95, -23.8), (0.45, 0.2, 2.4), 'hulldark')
    cylinder((x, 7.0, -24.7), (x, 7.0, -25.0), 0.22, 'hulldark', segments=10)


mirrored(boom)


# ---------------------------------------------------------------- cargo container
box((0, -0.5, -1.0), (18.8, 4.8, 21.0), 'container', bevel=0.18, name='container')
for f in [8.0 - i * 2.25 for i in range(9)]:
    mirrored(lambda s, f=f: box((s * 9.42, -0.5, f), (0.08, 4.5, 0.22), 'containerdark'))
for u in (1.72, -2.72):
    box((0, u + (0.12 if u > 0 else -0.12), -1.0), (18.2, 0.08, 20.4), 'containerdark')
box((0, -0.5, 9.52), (15.6, 3.8, 0.08), 'containerdark')
box((0, -0.5, 9.56), (0.14, 3.6, 0.06), 'container')
box((0, -0.5, -11.52), (15.6, 3.8, 0.08), 'containerdark')
box((0, 1.95, -1.0), (15.0, 0.1, 18.0), 'container')
mirrored(lambda s: box((s * 9.43, 0.2, 6.0), (0.06, 1.4, 1.6), 'yellow'))
# Hangers from the fuselage belly to the container roof.
for f in (7.0, -5.0):
    mirrored(lambda s, f=f: box((s * 3.2, 1.9, f), (1.0, 0.8, 1.2), 'hulldark', bevel=0.08))


# ---------------------------------------------------------------- legs, lift fans and wheels
def strut(x, f, top, bottom, w_top, d_top, w_bot, d_bot, rake=0.0):
    def sect(u, w, d, df):
        return [(x - w, u, f + df - d), (x + w, u, f + df - d), (x + w, u, f + df + d), (x - w, u, f + df + d)]
    waist = sect((top + bottom) / 2, (w_top + w_bot) / 2 * 0.9, (d_top + d_bot) / 2 * 0.9, rake / 2)
    ring_loft([sect(top, w_top, d_top, 0.0), waist, sect(bottom, w_bot, d_bot, rake)], 'hull', bevel=0.18,
              segments=3, name='strut')


def lift_fan(x, u, f, r):
    cylinder((x, u + 0.35, f), (x, u - 0.2, f), r, 'hull', segments=28, bevel=0.06)
    cylinder((x, u - 0.19, f), (x, u - 0.26, f), r * 0.82, 'fan', segments=28)
    cylinder((x, u - 0.24, f), (x, u - 0.34, f), r * 0.22, 'spinner', segments=12)
    for k in range(8):
        a = math.pi * k / 8
        box((x, u - 0.28, f), (r * 1.6 * abs(math.cos(a)) + 0.12, 0.04, r * 1.6 * abs(math.sin(a)) + 0.12), 'fan')


def wheel(x, u, f, r=0.8, w=0.5):
    cylinder((x - w / 2, u, f), (x + w / 2, u, f), r, 'tyre', segments=18, bevel=0.05)
    cylinder((x - w / 2 - 0.02, u, f), (x + w / 2 + 0.02, u, f), r * 0.45, 'grey', segments=12)


# Nose leg: strut under the cockpit to a pod with a lift fan ahead of two wheels.
strut(0.0, 19.8, 2.2, -2.2, 1.3, 1.9, 1.0, 1.5, rake=0.4)
sphere((0, -2.8, 20.0), 1.0, 'hull', scale=(2.0, 1.25, 3.3), segments=20)
lift_fan(0.0, -3.7, 21.2, 1.75)
mirrored(lambda s: wheel(s * 0.9, -4.2, 18.0))


# Main legs outboard of the container: pod with three wheels abreast and a lift fan aft.
def main_leg(side):
    x = side * 12.0
    strut(x, 2.2, 6.6, -2.0, 1.1, 2.3, 0.95, 1.9, rake=-0.4)
    sphere((x, -2.7, 1.6), 1.0, 'hull', scale=(2.3, 1.3, 4.6), segments=20)
    lift_fan(x + side * 0.4, -3.6, -0.6, 1.95)
    for dx in (-1.25, 0.0, 1.25):
        wheel(x + dx, -4.2, 4.6, w=0.45)
    box((x, -2.0, 4.6), (3.6, 0.7, 1.4), 'hulldark', bevel=0.1)


mirrored(main_leg)

KIT.export(OUT, 'Medea', previews=PREVIEWS, reference=5587)
