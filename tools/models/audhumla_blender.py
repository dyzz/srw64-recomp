"""Author the HD Audhumla (world-map transport, original resource 5586) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/audhumla_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z nose, +Y up,
units of the N64 mesh, 66 units = 524 m span, so 1 unit is about 7.9 m), model.glb
and optional previews with the original rendered from the same cameras (compare.png).

Design reference: Garuda-class super-large transport aircraft Audhumla from Mobile
Suit Zeta Gundam (Karaba flagship, captain Hayato Kobayashi). Official figures used:
length 317 m, span 524 m (Gundam Channel mechanic manual); red-family colour
scheme ("赤系統"). Layout follows the official colour art (front, top, rear) and TV
episode 13 stills: a deep rounded fuselage with a flat back and an oval dorsal
hatch, long pointed nose with probes and small canards, bridge glazing on the nose
and a window strip on each flank; high blended wing with a long leading-edge root,
about 30 degrees of sweep and wingtip fins; five boxy engine blocks in a row on
top of each wing exhausting over the trailing edge; small pods under the leading
edge; twin canted fins with outboard stabilisers at the stern. Salmon-orange hull
with vivid orange-red engine blocks as in the TV colour settei.

Sources: https://www.gundam-c.com/manual/mechanic/z/audhumla.html,
https://en.gundam-official.com/mecha/ak28n3ex3gtz2ncub1aw814n,
https://gundam.fandom.com/wiki/Garuda-class (settei scans and episode stills).
Reference images were only viewed for comparison; none are stored.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'hull': (234, 146, 98, 255), 'panel': (222, 132, 88, 255), 'hulldark': (186, 98, 64, 255),
    'engine': (224, 82, 44, 255), 'enginedark': (160, 52, 34, 255), 'glass': (118, 138, 168, 255),
    'belly': (206, 118, 80, 255),
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


# ---------------------------------------------------------------- fuselage
# Stations: (fwd, half width, top, keel). Long tapering nose ending in a point a little
# below mid height, flat back, deep belly with chines, boxy stern door.
HULL = [(20.0, 0.3, 1.5, 0.9), (19.0, 1.0, 2.2, 0.0), (17.0, 2.0, 3.2, -1.1), (14.0, 3.0, 4.0, -2.2),
        (10.5, 3.9, 4.6, -3.0), (6.0, 4.6, 5.0, -3.6), (0.0, 5.0, 5.2, -3.9), (-6.0, 5.0, 5.2, -3.8),
        (-11.0, 4.8, 5.0, -3.2), (-15.0, 4.4, 4.8, -2.2), (-17.8, 3.9, 4.6, -1.1), (-19.0, 3.6, 4.4, -0.5)]


def hull_ring(hw, top, keel):
    """Hard-chined section: flat deck, sloped shoulders, vertical flanks, faceted belly."""
    h = top - keel
    half = [(0.0, top), (0.5, top), (0.8, top - 0.1 * h), (0.97, top - 0.25 * h), (1.0, top - 0.42 * h),
            (0.95, top - 0.6 * h), (0.78, top - 0.8 * h), (0.5, top - 0.94 * h), (0.2, keel), (0.0, keel)]
    right = [(hw * x, y) for x, y in half]
    return right[1:-1] + [(0.0, keel)] + [(-x, y) for x, y in reversed(right[1:-1])] + [(0.0, top)]


loft([(f, hull_ring(hw, t, k)) for f, hw, t, k in HULL], 'hull', bevel=0.12, name='fuselage')


def hull_top(f):
    return lerp_table(HULL, f, 2)


def hull_hw(f):
    return lerp_table(HULL, f, 1)


# Belly plating: darker keel band and hangar door frame under the wing box.
loft([(f, [(-hw * 0.24, k - 0.06), (hw * 0.24, k - 0.06), (hw * 0.24, k + 0.3), (-hw * 0.24, k + 0.3)])
      for f, hw, _, k in HULL[3:-2]], 'belly', bevel=0.04, name='keel')
for f in (8.0, -2.0, -12.0):
    box((0, lerp_table(HULL, f, 3) + 0.05, f), (6.4, 0.12, 0.15), 'hulldark')

# Stern cargo door (flat rear face) with a darker frame.
box((0, 2.1, -19.02), (5.2, 3.8, 0.1), 'hulldark')
box((0, 2.1, -19.08), (4.4, 3.1, 0.06), 'panel')

# Nose: probe, beak and two small canards.
cylinder((0, 1.4, 19.6), (0, 1.4, 21.0), 0.28, 'hulldark', segments=10, radius_b=0.04)
mirrored(lambda s: cylinder((s * 0.9, 0.7, 19.0), (s * 1.3, 0.5, 20.2), 0.14, 'hulldark', segments=8, radius_b=0.03))
mirrored(lambda s: plate([(s * 1.6, 1.2, 18.6), (s * 3.0, 1.0, 17.6), (s * 3.0, 1.0, 17.0), (s * 1.8, 1.2, 17.0)],
                         0.18, 'panel', bevel=0.04))
# Raised bridge deck on the nose with glazing on its sloped front and sides, and the
# window strip on each flank.
loft([(16.4, [(-1.1, 3.25), (1.1, 3.25), (0.8, 3.45), (-0.8, 3.45)]),
      (15.2, [(-1.7, 3.5), (1.7, 3.5), (1.3, 4.35), (-1.3, 4.35)]),
      (11.0, [(-2.1, 4.35), (2.1, 4.35), (1.7, 5.0), (-1.7, 5.0)]),
      (9.0, [(-2.1, 4.6), (2.1, 4.6), (1.6, 5.0), (-1.6, 5.0)])], 'panel', bevel=0.08, name='bridge')
loft([(16.0, [(-0.95, 3.52), (0.95, 3.52), (0.8, 3.62), (-0.8, 3.62)]),
      (15.25, [(-1.5, 3.9), (1.5, 3.9), (1.25, 4.3), (-1.25, 4.3)])], 'window', name='bridge-glass')
mirrored(lambda s: loft([(15.0, [(s * 1.62, 3.85), (s * 1.74, 3.85), (s * 1.5, 4.25), (s * 1.38, 4.25)]),
                         (12.6, [(s * 1.9, 4.45), (s * 2.02, 4.45), (s * 1.8, 4.8), (s * 1.68, 4.8)])],
                        'window', name='bridge-side'))
mirrored(lambda s: box((s * (hull_hw(10.0) - 0.02), 1.7, 10.0), (0.12, 0.55, 3.4), 'glass', bevel=0.03))
mirrored(lambda s: box((s * (hull_hw(10.0) + 0.02), 1.7, 10.0), (0.06, 0.8, 3.8), 'hulldark'))
# Dorsal spine, oval upper hatch (shuttle-booster / Base Jabber hatch) and panel seams.
box((0, hull_top(4.0) + 0.02, 4.0), (1.0, 0.16, 18.0), 'panel', bevel=0.04)
sphere((0, hull_top(-1.0) - 0.15, -1.0), 1.0, 'hulldark', scale=(2.4, 0.5, 4.6), segments=24)
sphere((0, hull_top(-1.0) - 0.05, -1.0), 1.0, 'panel', scale=(2.0, 0.5, 4.1), segments=24)
for f in (13.0, 8.0, -8.0, -13.0):
    box((0, hull_top(f) + 0.01, f), (hull_hw(f) * 1.5, 0.05, 0.12), 'hulldark')
mirrored(lambda s: box((s * 2.4, hull_top(0.0) + 0.02, 0.0), (0.12, 0.05, 24.0), 'hulldark'))

# ---------------------------------------------------------------- wings
# Spanwise stations: (x, leading edge, trailing edge, mid-plane height, thickness).
WING = [(4.0, 11.0, -13.0, 3.0, 2.4), (8.0, 5.2, -13.2, 3.1, 1.5), (14.0, 1.4, -13.5, 3.3, 1.2),
        (21.0, -3.1, -13.9, 3.5, 0.9), (28.0, -7.6, -14.3, 3.8, 0.62), (32.2, -10.1, -14.5, 3.95, 0.45)]
FRACTIONS = [0.0, 0.06, 0.2, 0.42, 0.7, 1.0]
HALF = [0.3, 0.8, 1.0, 0.9, 0.55, 0.12]


def airfoil(x, le, te, up, t):
    chord = le - te
    top = [(x, up + t / 2 * h, le - s * chord) for s, h in zip(FRACTIONS, HALF)]
    bottom = [(x, up - t / 2 * h * 0.7, le - s * chord) for s, h in zip(FRACTIONS, HALF)]
    return top + bottom[::-1]


def wing_at(x, col):
    return lerp_table(sorted(WING, key=lambda r: -r[0]), x, col)


def wing_top(x, f):
    """Upper surface height at span x and chord position f (approximate)."""
    le, te, up, t = (wing_at(x, c) for c in (1, 2, 3, 4))
    s = min(max((le - f) / (le - te), 0.0), 1.0)
    h = HALF[0]
    for (s0, h0), (s1, h1) in zip(zip(FRACTIONS, HALF), zip(FRACTIONS[1:], HALF[1:])):
        if s0 <= s <= s1:
            h = h0 + (h1 - h0) * (s - s0) / (s1 - s0)
    return up + t / 2 * h


def wing(side):
    ring_loft([[(side * x0, u, f) for _, u, f in airfoil(x0, le, te, up, t)] for x0, le, te, up, t in WING],
              'hull', bevel=0.05, name='wing')
    # Darker trailing-edge flaps and panel seams on the upper surface.
    for x0, x1 in ((22.5, 27.0), (27.6, 31.6)):
        rings = []
        for x in (x0, x1):
            te = wing_at(x, 2)
            rings.append([(side * x, wing_top(x, te + 2.2) + 0.03, te + 2.2), (side * x, wing_top(x, te + 0.2) + 0.03, te + 0.2),
                          (side * x, wing_top(x, te + 0.2) + 0.1, te + 0.2), (side * x, wing_top(x, te + 2.2) + 0.1, te + 2.2)])
        ring_loft(rings, 'panel', name='flap')
    for x in (23.0, 27.3, 31.0):
        le, te = wing_at(x, 1), wing_at(x, 2)
        box((side * x, wing_top(x, (le + te) / 2) + 0.04, (le + te) / 2), (0.1, 0.05, (le - te) * 0.8), 'hulldark')
    # Leading-edge seam line.
    rings = []
    for x in (9.0, 31.0):
        le = wing_at(x, 1)
        f = le - 0.9
        rings.append([(side * x, wing_top(x, f) + 0.02, f + 0.08), (side * x, wing_top(x, f) + 0.02, f - 0.08),
                      (side * x, wing_top(x, f) + 0.08, f - 0.08), (side * x, wing_top(x, f) + 0.08, f + 0.08)])
    ring_loft(rings, 'hulldark', name='seam')
    # Wingtip fin, mostly above the wing with a short ventral part, swept like the wing.
    def tip_x(u):  # canted outboard like the rear settei view
        return side * (32.2 + (u - 3.9) * 0.3)
    plate([(tip_x(u), u, f) for u, f in ((3.9, -9.8), (7.4, -12.6), (7.4, -13.8), (3.7, -14.9), (2.1, -14.1), (2.6, -12.2))],
          0.3 * side, 'panel', bevel=0.06)
    for f, u in ((-11.2, 5.9), (-12.4, 7.3)):
        cylinder((tip_x(u), u, f), (tip_x(u), u + 0.25, f + 1.0), 0.07, 'hulldark', segments=6)
    # Spikes (AA mounts and antennas) along the outer leading edge.
    for x in (25.5, 28.0, 30.5):
        le = wing_at(x, 1)
        cylinder((side * x, wing_top(x, le - 0.6), le - 0.6), (side * x, wing_top(x, le - 0.6) + 0.25, le + 0.5),
                 0.08, 'hulldark', segments=6, radius_b=0.02)


mirrored(wing)


# ---------------------------------------------------------------- engine blocks
def engine_block(side, x):
    """Boxy engine with a sloped intake face, sitting on the wing and exhausting at the TE."""
    te = wing_at(x, 2)
    rear, front = te - 0.35, te + 6.4
    base = wing_top(x, te + 3.0) - 0.25
    w, h = 1.18, 1.75
    def sect(f, hw, top, lip=0.0):
        return (f, [(side * x - hw, base), (side * x + hw, base), (side * x + hw, top - lip),
                    (side * x + hw - 0.25, top), (side * x - hw + 0.25, top), (side * x - hw, top - lip)])
    loft([sect(rear, w, base + h, 0.2), sect(front - 1.8, w, base + h, 0.2),
          sect(front - 0.4, w * 0.96, base + h * 0.72, 0.15), sect(front, w * 0.9, base + h * 0.35, 0.1)],
         'engine', bevel=0.08, name='engine')
    # Dark intake slot on the sloped face and a louvre band on top.
    box((side * x, base + h * 0.55, front - 1.0), (w * 1.7, 0.36, 0.5), 'enginedark')
    for k in range(3):
        box((side * x, base + h + 0.02, rear + 1.4 + k * 0.9), (w * 1.6, 0.05, 0.28), 'enginedark')
    # Exhaust at the trailing edge with an emissive core.
    box((side * x, base + h * 0.5, rear - 0.05), (w * 1.75, h * 0.72, 0.12), 'nozzle')
    box((side * x, base + h * 0.5, rear - 0.12), (w * 1.35, h * 0.46, 0.04), 'glow')


ENGINES = [8.7 + i * 2.62 for i in range(5)]
for s in (-1, 1):
    for x in ENGINES:
        engine_block(s, x)
    # Red fairings closing both ends of the engine row, as in the colour settei.
    for x in (ENGINES[0] - 1.45, ENGINES[-1] + 1.45):
        te = wing_at(x, 2)
        box((s * x, wing_top(x, te + 3.0) + 0.55, te + 2.6), (0.35, 1.4, 6.0), 'enginedark', bevel=0.06)


# ---------------------------------------------------------------- leading-edge pods
def le_pod(side, x):
    le = wing_at(x, 1)
    u = wing_at(x, 3) - 0.55
    cylinder((side * x, u, le - 4.2), (side * x, u, le + 0.9), 0.62, 'panel', segments=16, bevel=0.03)
    cylinder((side * x, u, le + 0.9), (side * x, u, le + 1.6), 0.62, 'engine', segments=16, radius_b=0.3)
    cylinder((side * x, u, le + 1.55), (side * x, u, le + 1.65), 0.28, 'enginedark', segments=12)


mirrored(lambda s: le_pod(s, 7.6))
mirrored(lambda s: le_pod(s, 21.6))


# ---------------------------------------------------------------- tail
def tail(side):
    root = side * 3.5
    cant = side * 1.7
    top_u = 14.4
    plate([(root, hull_top(-9.0) - 0.2, -8.6), (root, hull_top(-18.2) - 0.2, -18.2),
           (root + cant, top_u, -19.4), (root + cant, top_u, -16.6)], 0.55 * side, 'hull', bevel=0.08)
    # Root fairing along the fin base.
    loft([(-8.2, [(root - 0.45, hull_top(-8.2) - 0.1), (root + 0.45, hull_top(-8.2) - 0.1),
                  (root + 0.25, hull_top(-8.2) + 0.2), (root - 0.25, hull_top(-8.2) + 0.2)]),
          (-11.0, [(root - 0.55, hull_top(-11) - 0.1), (root + 0.55, hull_top(-11) - 0.1),
                   (root + 0.45, hull_top(-11) + 1.1), (root - 0.35, hull_top(-11) + 1.1)]),
          (-18.6, [(root - 0.6, hull_top(-18.6) - 0.1), (root + 0.6, hull_top(-18.6) - 0.1),
                   (root + 0.5, hull_top(-18.6) + 1.3), (root - 0.4, hull_top(-18.6) + 1.3)])],
         'panel', bevel=0.06, name='fin-root')
    # Outboard stabiliser at mid height.
    x0, u0 = root + cant * 0.45, 9.2
    plate([(x0, u0, -14.3), (x0, u0, -17.8), (x0 + side * 3.6, u0 + 0.5, -19.0), (x0 + side * 3.6, u0 + 0.5, -17.6)],
          0.28, 'hull', bevel=0.05)
    box((x0 + side * 3.5, u0 + 0.62, -18.3), (0.35, 0.3, 1.4), 'engine')
    # Red fin cap and antenna spikes on the leading edge.
    plate([(root + cant * 0.93, 13.6, -16.2), (root + cant * 0.93, 13.6, -19.2),
           (root + cant, top_u, -19.4), (root + cant, top_u, -16.6)], 0.46 * side, 'engine')
    for u in (8.0, 10.5, 12.7):
        t = (u - 4.6) / (top_u - 4.6)
        f = -8.6 + (-16.6 + 8.6) * t
        x = root + cant * t
        cylinder((x, u, f), (x, u + 0.3, f + 1.1), 0.07, 'hulldark', segments=6)


mirrored(tail)

# ---------------------------------------------------------------- belly spikes and AA guns
for f, s in ((12.0, 1), (12.0, -1), (-6.0, 1), (-6.0, -1)):
    k = lerp_table(HULL, f, 3)
    cylinder((s * 1.6, k + 0.4, f), (s * 2.2, k - 0.7, f - 1.2), 0.12, 'hulldark', segments=6, radius_b=0.03)
cylinder((0, lerp_table(HULL, 2.0, 3) + 0.2, 2.0), (0, lerp_table(HULL, 2.0, 3) - 1.0, 0.6), 0.14, 'hulldark', segments=6,
         radius_b=0.03)
for f in (9.0, -4.0):
    mirrored(lambda s, f=f: (box((s * 2.2, hull_top(f) + 0.15, f), (0.55, 0.3, 0.55), 'panel', bevel=0.04),
                             cylinder((s * 2.2, hull_top(f) + 0.3, f), (s * 2.2, hull_top(f) + 0.3, f + 0.9), 0.06,
                                      'hulldark', segments=6)))

KIT.export(OUT, 'Audhumla', previews=PREVIEWS, reference=5586)
