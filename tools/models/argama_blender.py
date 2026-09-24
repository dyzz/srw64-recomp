"""Author the HD Argama (world-map ship, original resource 5585) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/argama_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 63 units = 323 m), model.glb and optional previews with the
original rendered from the same cameras (compare.png).

Design reference: Argama-class assault cruiser Argama from Mobile Suit Zeta
Gundam (mechanical design Mamoru Nagano, revised to director Yoshiyuki Tomino's
instructions). Official figures used: length 323 m; built with White Base as the
model; two open catapult decks, one each side, over a two-level MS deck; two
residential blocks outside the hull that swing out on arms for 1 G in peacetime
and fold beside the hull as shields in battle (modelled folded); the bridge on
a support that lowers in combat; two mega particle cannons stowed behind
shutters on the lower hull sides; four single guns (forward upper centre, two
forward lower sides, aft upper centre) and a missile launcher behind the
bridge. Colours: white hull, dark slate catapult decks, the red bridge support,
orange cannon shutters and residential block ports, red engine nozzles.
Proportions follow the side, top, front and rear views of the licensed Cosmo
Fleet Special figure in its combat configuration.

Sources: ガンダムチャンネル 设定页 (gundam-c.com/manual/mechanic/z/ahgama.html),
Wikipedia アーガマ (ガンダムシリーズ), スーパーロボット大戦Wiki アーガマ,
MegaHouse Cosmo Fleet Special 機動戦士Ζガンダム アーガマ product photos (megahobby.jp).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'slate': (78, 83, 98, 255), 'crimson': (176, 40, 44, 255), 'amber': (214, 118, 46, 255),
    'amberdark': (150, 70, 30, 255), 'black': (36, 38, 44, 255),
})
loft, box, cylinder, sphere, mirrored, add_object = KIT.loft, KIT.box, KIT.cylinder, KIT.sphere, KIT.mirrored, KIT.add_object


def solid(rings, color, bevel=0.0, name='solid', segments=2):
    """Capped loft through rings of (x, up, fwd) points in any orientation."""
    bm = bmesh.new()
    verts = [[bm.verts.new(P(*p)) for p in ring] for ring in rings]
    n = len(verts[0])
    for a, b in zip(verts, verts[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(list(reversed(verts[0])))
    bm.faces.new(verts[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return add_object(bm, color, bevel, segments=segments, name=name)


def disc(center, radius, color, normal_axis='z', depth=0.12, segments=20):
    """Thin disc facing along +/- an axis (x or z) centred on a face."""
    x, y, z = center
    if normal_axis == 'z':
        cylinder((x, y, z - depth / 2), (x, y, z + depth / 2), radius, color, segments=segments)
    else:
        cylinder((x - depth / 2, y, z), (x + depth / 2, y, z), radius, color, segments=segments)


# ------------------------------------------------------------ catapult decks
# Two open decks from the bow back to the hangars; the arm deepens toward the root.
ARM = [(30.5, 7.9, -0.35), (24.0, 8.9, -0.6), (12.0, 10.9, -1.3), (3.0, 12.4, -1.6), (-3.0, 12.6, -1.6)]
INNER = 3.85


def catapult(side):
    def ring(outer, bottom):
        xi, xo = side * INNER, side * outer
        return [(xi, bottom), (xo - side * 0.5, bottom), (xo, bottom + 0.5), (xo, 0.4), (xi, 0.4)]

    loft([(f, ring(o, b)) for f, o, b in ARM], 'panel', bevel=0.1, name='arm')
    # Dark deck with the centre rail; light edge strip outboard.
    loft([(f - (0.25 if i == 0 else 0), [(side * (INNER + 0.35), 0.4), (side * (o - 1.1), 0.4),
                                            (side * (o - 1.1), 0.48), (side * (INNER + 0.35), 0.48)])
          for i, (f, o, _) in enumerate(ARM[:4])], 'slate', name='deck')
    mid0, mid1 = side * (INNER + 7.9 - 1.1) / 2, side * (INNER + 12.4 - 1.1) / 2
    loft([(29.5, [(mid0 - 0.12, 0.48), (mid0 + 0.12, 0.48), (mid0 + 0.12, 0.52), (mid0 - 0.12, 0.52)]),
          (3.0, [(mid1 - 0.12, 0.48), (mid1 + 0.12, 0.48), (mid1 + 0.12, 0.52), (mid1 - 0.12, 0.52)])], 'black', name='rail')
    # Launch marks: yellow chevrons near the tip and along the deck.
    KIT.plate([(mid0 - 0.55, 0.5, 29.2), (mid0 + 0.55, 0.5, 29.2), (mid0, 0.5, 30.0)][::side], 0.03, 'yellow')
    for f in (22.0, 16.0, 10.0):
        t = (30.5 - f) / 27.5
        x = side * (INNER + 0.9 + 0.2 * t)
        box((x, 0.5, f), (0.35, 0.03, 0.35), 'yellow')
    # Catapult shuttle ("geta") parked at the deck root.
    box((side * (INNER + (12.4 - INNER) * 0.45), 0.72, 1.5), (1.4, 0.5, 2.2), 'black', bevel=0.08)


mirrored(catapult)

# ------------------------------------------------------------ lower hull (MS hangars)
# Wide lower hull under the catapult roots; the front rises into the arms.


def hull_ring(w, bottom, chine):
    return [(-(w - 1.3), bottom), (w - 1.3, bottom), (w, chine), (w, -0.4), (-w, -0.4), (-w, chine)]


loft([(15.6, hull_ring(10.8, -1.3, -1.0)), (8.0, hull_ring(12.1, -4.8, -3.6)), (3.0, hull_ring(12.2, -6.3, -5.0)),
      (-7.8, hull_ring(12.2, -6.3, -5.0))], 'hull', bevel=0.16, name='lower-hull')
box((0, -6.36, -2.0), (18.0, 0.08, 9.0), 'panel')


def shutter(side):
    # Orange shutter of the stowed mega particle cannon on the flank.
    x = side * 12.3
    bm = bmesh.new()
    ring_in, ring_out = [], []
    n = 24
    for k in range(n):
        a = 2 * math.pi * k / n
        y, z = -3.1 + 2.3 * math.sin(a), -0.9 + 2.9 * math.cos(a)
        ring_in.append(bm.verts.new(P(x - side * 0.25, y, z)))
        ring_out.append(bm.verts.new(P(x + side * 0.2, y, z)))
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((ring_in[k], ring_in[j], ring_out[j], ring_out[k]))
    bm.faces.new(ring_in[::-1])
    bm.faces.new(ring_out)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    add_object(bm, 'amber', 0.04, name='shutter')
    for u in (-1.6, -0.8, 0.0, 0.8, 1.6):
        half = 2.9 * math.sqrt(max(0.0, 1 - (u / 2.3) ** 2)) * 0.92
        box((x + side * 0.22, -3.1 + u, -0.9), (0.06, 0.12, 2 * half), 'amberdark')
    # Shutter frame.
    box((x + side * 0.05, -3.1, -0.9), (0.2, 5.4, 6.6), 'grey', bevel=0.06)


mirrored(shutter)

# Forward lower single guns (one each side, under the hangars).


def lower_gun(side):
    x = side * 7.2
    box((x, -6.8, 2.0), (1.6, 1.0, 2.6), 'black', bevel=0.08)
    cylinder((x, -6.8, 3.2), (x, -6.8, 11.5), 0.26, 'black', segments=10)
    cylinder((x, -6.8, 3.2), (x, -6.8, 6.0), 0.38, 'black', segments=10)


mirrored(lower_gun)

# ------------------------------------------------------------ central hull
# Wedge between the decks with the forward gun, rising to the red bridge support.


def central(w, top):
    return [(-w, -0.5), (w, -0.5), (w, top - 0.8), (0.7 * w, top), (-0.7 * w, top), (-w, top - 0.8)]


loft([(18.6, central(2.3, 0.8)), (12.0, central(3.0, 2.5)), (6.0, central(3.5, 3.1)), (1.0, central(3.7, 3.3))],
     'hull', bevel=0.12, name='central-fore')
box((0, 1.6, 17.0), (3.0, 0.06, 2.0), 'panel')
# Forward upper single gun.
box((0, 2.9, 12.0), (1.9, 0.9, 2.6), 'black', bevel=0.1)
cylinder((0, 3.0, 13.2), (0, 3.0, 22.5), 0.26, 'black', segments=10)
cylinder((0, 3.0, 13.2), (0, 3.0, 16.0), 0.4, 'black', segments=10)

# Red bridge support: a sloped ramp from the deck to the bridge, a white rib down its middle.
loft([(9.2, [(-3.4, 1.0), (3.4, 1.0), (3.4, 1.3), (-3.4, 1.3)]),
      (0.6, [(-3.5, 1.0), (3.5, 1.0), (3.4, 7.0), (-3.4, 7.0)]),
      (-1.2, [(-3.5, 1.0), (3.5, 1.0), (3.4, 7.0), (-3.4, 7.0)])], 'crimson', bevel=0.1, name='support')
loft([(8.8, [(-0.35, 1.2), (0.35, 1.2), (0.35, 1.6), (-0.35, 1.6)]),
      (0.4, [(-0.35, 6.8), (0.35, 6.8), (0.35, 7.2), (-0.35, 7.2)])], 'hull', name='rib')
for t in (0.3, 0.55):
    f = 9.0 - t * 8.4
    up = 1.2 + t * 5.8
    mirrored(lambda s: box((s * 1.9, up + 0.05, f), (1.6, 0.06, 0.5), 'hull'))
# Upper hull aft of the support: white roof, red side walls, down to the engines.
loft([(0.8, [(-3.4, 1.0), (3.4, 1.0), (3.4, 6.3), (-3.4, 6.3)]),
      (-13.0, [(-3.4, 1.0), (3.4, 1.0), (3.4, 6.3), (-3.4, 6.3)])], 'crimson', bevel=0.08, name='upper-walls')
box((0, 6.38, -6.2), (6.2, 0.2, 13.4), 'hull', bevel=0.05)
box((0, 6.5, -8.5), (3.6, 0.05, 6.0), 'panel')

# ------------------------------------------------------------ bridge
loft([(4.4, [(-2.4, 6.6), (2.4, 6.6), (2.1, 8.3), (-2.1, 8.3)]),
      (3.6, [(-2.8, 6.6), (2.8, 6.6), (2.6, 8.6), (-2.6, 8.6)]),
      (-3.2, [(-2.9, 6.6), (2.9, 6.6), (2.7, 8.6), (-2.7, 8.6)])], 'hull', bevel=0.1, name='bridge')
box((0, 7.7, 4.1), (4.2, 0.55, 0.9), 'window')
mirrored(lambda s: box((s * 2.83, 7.7, 1.2), (0.1, 0.45, 3.6), 'window'))
box((0, 8.64, 0.4), (4.0, 0.08, 4.0), 'panel')
# Antennas raked forward and a mast.
mirrored(lambda s: cylinder((s * 0.9, 8.6, 1.2), (s * 1.3, 10.2, 4.2), 0.1, 'grey', segments=6))
mirrored(lambda s: cylinder((s * 1.8, 8.6, -0.5), (s * 2.3, 9.8, 1.8), 0.08, 'grey', segments=6))
cylinder((0, 8.6, -1.6), (0, 10.4, -1.6), 0.14, 'grey', segments=8)
box((0, 9.8, -1.6), (1.8, 0.14, 0.18), 'grey')
# Missile launcher behind the bridge.
box((0, 6.95, -5.0), (2.6, 0.9, 2.2), 'grey', bevel=0.08)
for x in (-0.7, 0.0, 0.7):
    box((x, 7.42, -4.6), (0.4, 0.05, 0.4), 'black')

# ------------------------------------------------------------ residential blocks (folded)


def residence(side):
    xi, xo = side * 4.3, side * 11.0
    cx = (xi + xo) / 2
    box((cx, 3.8, -6.85), (abs(xo - xi), 5.8, 8.9), 'panel', bevel=0.25, segments=2)
    box((cx, 6.72, -7.5), (5.2, 0.06, 5.6), 'hull')
    # Two ports on the forward face.
    for dx in (-1.6, 1.6):
        disc((cx + dx, 3.9, -2.37), 1.3, 'grey', depth=0.1)
        disc((cx + dx, 3.9, -2.3), 1.05, 'amber', depth=0.1)
        disc((cx + dx, 3.9, -2.25), 0.55, 'amberdark', depth=0.06)
    # Rounded hatch outline on the outer face.
    x = xo + side * 0.03
    box((x, 2.2, -7.2), (0.08, 0.22, 4.0), 'dark')
    mirrored(lambda s: box((x, 3.6, -7.2 + s * 2.0), (0.08, 3.0, 0.22), 'dark'))
    cylinder((x - side * 0.04, 5.1, -7.2), (x + side * 0.04, 5.1, -7.2), 2.1, 'dark', segments=20)
    cylinder((x - side * 0.05, 5.1, -7.2), (x + side * 0.05, 5.1, -7.2), 1.85, 'panel', segments=20)
    box((x + side * 0.02, 4.2, -7.2), (0.1, 1.8, 3.8), 'panel')
    # Folding arm pivot on the inner face.
    cylinder((xi - side * 0.1, 4.0, -8.5), (xi - side * 0.9, 4.0, -8.5), 0.9, 'grey', segments=14)


mirrored(residence)

# ------------------------------------------------------------ engine blocks


def engine(side):
    xc = side * 9.1

    def ring(w, bottom, top):
        return [(xc - 0.8 * w, bottom), (xc + 0.8 * w, bottom), (xc + w, bottom + 1.6), (xc + w, top - 0.8),
                (xc + 0.8 * w, top), (xc - 0.8 * w, top), (xc - w, top - 0.8), (xc - w, bottom + 1.6)]

    loft([(-7.2, ring(3.8, -6.2, 0.9)), (-9.5, ring(4.1, -7.5, 2.2)), (-16.0, ring(4.1, -7.7, 2.2)),
          (-24.0, ring(4.0, -7.0, 2.1)), (-27.0, ring(3.8, -5.8, 1.8))], 'hull', bevel=0.2, segments=2, name='engine')
    box((xc, 2.22, -18.5), (5.6, 0.06, 8.0), 'panel')
    box((xc + side * 1.5, 2.5, -18.0), (1.6, 0.5, 9.0), 'panel', bevel=0.08)
    # Vents on the outer flank.
    for i in range(4):
        box((xc + side * 4.1, 0.2, -12.5 - i * 3.2), (0.1, 0.6, 2.0), 'black')
    # Three nozzles in a row on the stern face.
    for dx in (-2.5, 0.0, 2.5):
        x = xc + dx
        cylinder((x, -3.2, -26.6), (x, -3.2, -28.2), 1.15, 'red', segments=18, radius_b=1.25, cap=False)
        cylinder((x, -3.2, -26.7), (x, -3.2, -28.1), 0.9, 'nozzle', segments=18, cap=False)
        cylinder((x, -3.2, -27.2), (x, -3.2, -27.3), 0.9, 'glow', segments=18)


mirrored(engine)

# Curved side fins (crescent shields) outboard of the engines, with cooling slits.


def side_fin(side):
    root, fore, aft = side * 12.9, -9.5, -25.5
    span, n = 6.2, 14
    pts = []
    for k in range(n + 1):
        a = math.pi * k / n
        f = (fore + aft) / 2 + (fore - aft) / 2 * math.cos(a)
        r = math.sin(a)
        pts.append((root + side * span * r, 0.2 - 3.2 * r * r, f))
    top = [(x, y + 0.5, f) for x, y, f in pts]
    bm = bmesh.new()
    lo = [bm.verts.new(P(*p)) for p in pts]
    hi = [bm.verts.new(P(*p)) for p in top]
    bm.faces.new(lo[::-1])
    bm.faces.new(hi)
    for k in range(n):
        bm.faces.new((lo[k], lo[k + 1], hi[k + 1], hi[k]))
    bm.faces.new((lo[n], lo[0], hi[0], hi[n]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    add_object(bm, 'panel', 0.08, name='side-fin')
    for i, f in enumerate((-13.0, -16.0, -19.0, -22.0)):
        r = math.sin(math.acos(max(-1.0, min(1.0, (f - (fore + aft) / 2) / ((fore - aft) / 2)))))
        x = root + side * span * r * 0.55
        box((x, 0.68 - 3.2 * (r * 0.55) ** 2, f), (1.8 * r + 0.3, 0.06, 0.55), 'black')


mirrored(side_fin)

# ------------------------------------------------------------ central aft hull
loft([(-8.0, [(-5.1, -5.0), (5.1, -5.0), (5.1, 4.2), (-5.1, 4.2)]),
      (-24.0, [(-5.1, -5.0), (5.1, -5.0), (5.1, 4.2), (-5.1, 4.2)]),
      (-27.0, [(-4.6, -4.2), (4.6, -4.2), (4.6, 3.6), (-4.6, 3.6)])], 'hull', bevel=0.16, name='aft')
mirrored(lambda s: box((s * 4.2, 4.26, -18.5), (0.5, 0.08, 15.0), 'crimson'))
box((0, 4.26, -19.0), (6.0, 0.06, 8.0), 'panel')
# Main nozzle.
cylinder((0, -0.4, -26.6), (0, -0.4, -28.6), 2.4, 'red', segments=24, radius_b=2.55, cap=False)
cylinder((0, -0.4, -26.7), (0, -0.4, -28.5), 2.05, 'nozzle', segments=24, cap=False)
cylinder((0, -0.4, -27.4), (0, -0.4, -27.5), 2.05, 'glow', segments=24)
# Aft upper single gun, trained astern.
box((0, 4.8, -17.5), (2.0, 1.0, 2.8), 'black', bevel=0.1)
cylinder((0, 4.9, -18.8), (0, 4.9, -27.5), 0.26, 'black', segments=10)
cylinder((0, 4.9, -18.8), (0, 4.9, -21.5), 0.4, 'black', segments=10)

# Tail blades: two long stabilisers with red spines running astern under the hull.


def tail(side):
    x = side * 4.4
    loft([(-16.0, [(x - 0.25, -4.8), (x + 0.25, -4.8), (x + 0.25, -1.2), (x - 0.25, -1.2)]),
          (-27.0, [(x - 0.22, -3.6), (x + 0.22, -3.6), (x + 0.22, -1.3), (x - 0.22, -1.3)]),
          (-32.5, [(x - 0.1, -2.0), (x + 0.1, -2.0), (x + 0.1, -1.7), (x - 0.1, -1.7)])], 'panel', bevel=0.04, name='tail')
    loft([(-16.0, [(x - 0.28, -1.3), (x + 0.28, -1.3), (x + 0.28, -0.9), (x - 0.28, -0.9)]),
          (-32.5, [(x - 0.12, -1.8), (x + 0.12, -1.8), (x + 0.12, -1.6), (x - 0.12, -1.6)])], 'red', name='tail-spine')


mirrored(tail)

KIT.export(OUT, 'Argama', previews=PREVIEWS, reference=5585)
