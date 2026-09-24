"""Author the HD Goraon (world-map ship, original resource 5593) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/goraon_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 67 units = 820 m), model.glb and optional previews with the
original rendered from the same cameras (compare.png).

Design reference: Aura Battle Ship Goraon from Aura Battler Dunbine (mechanical
design Yutaka Izubuchi), built by King Foizon Go of the land of Lau and commanded
by Elle Hamm. Official figures used: length 820 met, beam 510, height 280, dry
weight 102,000 ruftons; the only Aura Battle Ship with a conventional warship
layout; Aura Nova cannon in the bow, four large-calibre main guns, many aura
vulcans; dark green hull. Sources: ja.wikipedia "オーラマシン" (艦船 section),
srw.wiki.cre.jp "ゴラオン", the 1984 design sheet (reproduced at kopenguin.com,
"オーラマシン一覧"), a licensed product card (820 / 510, 102000), the licensed
MRD resin kit and a licensed garage-kit box painting (colours: dark grey-green
hull, deep green recesses, maroon trim, white pod noses).

Layout read from those views: a two-tier bow (slim upper hull over the heavier
Nova-cannon hull, joined by a keel web); a mid hull with paired swept dorsal horns;
aft, two box engine blocks either side of the spine, broad cranked wings with
down-turned tip fins, four white-nosed pods (above and below each wing root), a
wide flat belly with grilles, and a bridge tower crowned by a V of fins. The
original 196-triangle model has the same parts in the same places (bow toward +z).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'hull': (92, 110, 101, 255), 'panel': (124, 142, 130, 255), 'green': (46, 78, 56, 255),
    'maroon': (112, 50, 46, 255), 'white': (226, 232, 228, 255), 'dark': (38, 44, 42, 255),
    'glow': (150, 235, 200, 40),
})
loft, box, cylinder, sphere, plate, mirrored, add_object = (KIT.loft, KIT.box, KIT.cylinder, KIT.sphere,
                                                           KIT.plate, KIT.mirrored, KIT.add_object)


def ring(cx, hw, bottom, top, n=20, p=2.6, cy=None):
    """Rounded (superellipse) section: centre x, half width, bottom and top heights."""
    cy = (bottom + top) / 2 if cy is None else cy
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        c, s = math.cos(a), math.sin(a)
        h = (top - cy) if s >= 0 else (cy - bottom)
        pts.append((cx + hw * math.copysign(abs(c) ** (2 / p), c), cy + h * math.copysign(abs(s) ** (2 / p), s)))
    return pts


def tube(points, radii, color, n=10, name='tube'):
    """Round tube through original-frame points; a zero radius ends in a point."""
    bm = bmesh.new()
    pts = [Vector(p) for p in points]
    rings, normal = [], None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        ref = normal if normal is not None else (Vector((0, 1, 0)) if abs(t.y) < 0.9 else Vector((1, 0, 0)))
        normal = (ref - t * ref.dot(t)).normalized()
        side = t.cross(normal)
        rx, ry = radii[i] if isinstance(radii[i], tuple) else (radii[i], radii[i])
        if max(rx, ry) <= 1e-6:
            rings.append([bm.verts.new(P(*p))])
            continue
        rings.append([bm.verts.new(P(*(p + side * (rx * math.cos(2 * math.pi * k / n)) +
                                           normal * (ry * math.sin(2 * math.pi * k / n))))) for k in range(n)])
    for a, b in zip(rings, rings[1:]):
        if len(a) == 1 or len(b) == 1:
            tip, rim = (a[0], b) if len(a) == 1 else (b[0], a)
            for k in range(n):
                bm.faces.new((rim[k], rim[(k + 1) % n], tip))
            continue
        for k in range(n):
            bm.faces.new((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]))
    for end in (rings[0], rings[-1]):
        if len(end) > 1:
            bm.faces.new(end)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return add_object(bm, color, name=name)


# ---------------------------------------------------------------- upper hull (spine and bow)
# Slim upper hull from the stern to the forward gun muzzle at z 46.
SPINE = [(-15.5, 3.0, -4.0, 3.4), (-10.0, 3.4, -4.8, 3.8), (-2.0, 3.8, -5.2, 3.9), (8.0, 4.3, -5.5, 3.7),
         (17.0, 4.5, -5.4, 3.4), (25.0, 4.1, -4.9, 3.0), (32.0, 3.4, -4.2, 2.5), (38.0, 2.6, -3.5, 1.8),
         (42.5, 1.8, -2.8, 1.1), (45.0, 1.25, -2.2, 0.5)]
loft([(z, ring(0, hw, b, t, n=24, p=2.3)) for z, hw, b, t in SPINE], 'hull', name='spine')
# Deck plating along the top of the spine and forecastle.
loft([(z, ring(0, hw * 0.62, t - 0.4, t + 0.18, n=16, p=3.0)) for z, hw, b, t in SPINE[1:9]], 'panel', name='deck')
cylinder((0, -0.85, 44.4), (0, -0.85, 46.6), 0.95, 'panel', segments=16)
cylinder((0, -0.85, 46.4), (0, -0.85, 46.7), 0.62, 'dark', segments=14)
# Side sponsons of the mid hull with hangar slots.
mirrored(lambda s: loft([(z, ring(s * (hw - 0.6), 1.5, b, t, n=16, p=2.8)) for z, hw, b, t in
                         [(4.0, 4.6, -4.6, 0.8), (10.0, 5.4, -5.0, 1.2), (22.0, 5.2, -4.6, 1.0), (29.0, 4.2, -3.8, 0.4)]],
                        'hull', name='sponson'))
for s in (-1, 1):
    for z in (11.0, 14.0, 17.0, 20.0):
        box((s * 5.35, -1.7, z), (0.3, 1.6, 1.6), 'green')
    box((s * 4.6, 0.95, 16.0), (1.4, 0.12, 12.0), 'maroon')

# ---------------------------------------------------------------- Aura Nova cannon hull
NOVA = [(9.0, 2.6, -9.2, -5.2), (14.0, 3.3, -10.2, -5.6), (22.0, 3.4, -10.6, -6.2), (32.0, 3.1, -10.3, -6.6),
        (41.0, 2.5, -9.8, -6.9), (47.0, 1.95, -9.3, -7.1), (48.8, 1.75, -9.1, -7.2)]
loft([(z, ring(0, hw, b, t, n=24, p=2.2)) for z, hw, b, t in NOVA], 'hull', name='nova-hull')
loft([(z, ring(0, hw * 0.55, t - 0.3, t + 0.15, n=14, p=3.0)) for z, hw, b, t in NOVA[1:6]], 'green', name='nova-deck')
cylinder((0, -8.15, 48.3), (0, -8.15, 50.6), 1.55, 'panel', segments=20, bevel=0.06)
cylinder((0, -8.15, 50.5), (0, -8.15, 51.0), 1.75, 'hull', segments=20)
cylinder((0, -8.15, 50.7), (0, -8.15, 51.05), 1.1, 'dark', segments=18)
cylinder((0, -8.15, 50.95), (0, -8.15, 51.02), 0.8, 'glow', segments=16)
for s in (-1, 1):
    for i in range(3):
        box((s * 2.2, -8.0, 26.0 + i * 1.8), (0.14, 0.7, 1.1), 'dark')
# Keel web joining the two bow hulls.
plate([(0.35, -6.9, 40.0), (0.35, -4.2, 43.0), (0.35, -4.4, 20.0), (0.35, -6.5, 16.0)], 0.7, 'hull', bevel=0.08)
plate([(0.4, -6.0, 38.0), (0.4, -4.8, 39.5), (0.4, -5.0, 25.0), (0.4, -6.2, 24.0)], 0.8, 'green')

# ---------------------------------------------------------------- belly (wide flat lower hull)
BELLY = [(-9.0, 7.5, -9.0, -5.6), (-5.0, 12.0, -10.4, -5.6), (1.0, 14.8, -11.0, -5.8), (8.0, 15.2, -11.0, -6.0),
         (12.0, 13.2, -10.8, -6.1), (15.5, 8.5, -10.4, -6.3), (18.0, 3.8, -9.8, -6.4)]
loft([(z, ring(0, hw, b, t, n=28, p=4.5)) for z, hw, b, t in BELLY], 'hull', name='belly')
# Slatted grilles under the belly and four round thrusters at its stern.
for s in (-1, 1):
    for i in range(7):
        box((s * 7.5, -11.05, -2.0 + i * 1.7), (9.0, 0.1, 0.5), 'dark')
    for x in (3.2, 7.4):
        cylinder((s * x, -8.2, -8.2), (s * x, -8.2, -10.0), 1.3, 'panel', segments=16, cap=False)
        cylinder((s * x, -8.2, -8.4), (s * x, -8.2, -9.9), 1.0, 'dark', segments=16, cap=False)
        cylinder((s * x, -8.2, -8.8), (s * x, -8.2, -8.9), 1.0, 'glow', segments=16)
    box((s * 9.2, -5.7, 6.0), (7.0, 0.12, 9.0), 'green')


# ---------------------------------------------------------------- engine blocks
def engine(side):
    cx = side * 7.2
    sections = [(8.0, ring(cx, 1.6, -3.0, 1.2, n=20, p=2.2)), (5.5, ring(cx, 3.2, -4.4, 2.8, n=20, p=3.2)),
                (2.0, ring(cx, 3.9, -5.0, 3.6, n=20, p=4.0)), (-12.5, ring(cx, 4.1, -5.2, 3.9, n=20, p=4.0)),
                (-16.0, ring(cx, 4.0, -5.1, 3.8, n=20, p=4.0))]
    loft(sections, 'hull', name='engine')
    # Flat top with panel lines and deep green inset, like the kit's engine decks.
    box((cx, 3.95, -6.0), (6.4, 0.14, 15.0), 'panel', bevel=0.04)
    box((cx, 4.05, -8.0), (3.8, 0.1, 8.0), 'green')
    for z in (-2.0, -5.0, -12.0, -14.6):
        box((cx, 4.08, z), (6.2, 0.06, 0.16), 'dark')
    box((cx + side * 4.08, -0.6, -6.0), (0.12, 2.6, 13.0), 'green')
    box((cx + side * 4.12, 1.6, -6.0), (0.1, 0.3, 14.0), 'maroon')
    # Exhaust grille on the stern face.
    box((cx, -0.6, -16.05), (6.6, 7.0, 0.2), 'dark')
    for i in range(5):
        box((cx, -3.6 + i * 1.5, -16.1), (5.8, 0.55, 0.1), 'glow')


mirrored(engine)


# ---------------------------------------------------------------- wings
def wing(side):
    y0, y1 = -0.7, 0.3   # slight dihedral toward the tip
    root = [(side * 10.6, y0, 6.5), (side * 10.6, y0, -14.5)]
    tip = [(side * 23.0, y1, -11.0), (side * 23.3, y1, -5.5), (side * 20.2, y1 - 0.2, 8.2)]
    plate([root[0], (side * 16.0, y0 + 0.3, 8.0), tip[2], tip[1], tip[0], (side * 14.0, y0 + 0.4, -15.5), root[1]]
          if side > 0 else
          [root[1], (side * 14.0, y0 + 0.4, -15.5), tip[0], tip[1], tip[2], (side * 16.0, y0 + 0.3, 8.0), root[0]],
          1.0, 'hull', bevel=0.22)
    # Upper plating, green insets and a maroon leading-edge stripe.
    def on_wing(x, z, lift):   # a point on the wing's upper surface, raised by lift
        t = (abs(x) - 10.6) / 12.6
        return (x, y0 + (y1 - y0) * t + 1.0 + lift, z)
    plate([on_wing(side * 11.4, 5.0, 0.1), on_wing(side * 19.0, 6.6, 0.1), on_wing(side * 22.2, -5.0, 0.1),
           on_wing(side * 22.0, -9.8, 0.1), on_wing(side * 14.2, -13.8, 0.1), on_wing(side * 11.4, -13.2, 0.1)][::side],
          0.08, 'panel')
    plate([on_wing(side * 14.5, 2.5, 0.2), on_wing(side * 19.5, 3.5, 0.2), on_wing(side * 20.6, -4.0, 0.2),
           on_wing(side * 15.2, -7.5, 0.2)][::side], 0.08, 'green')
    plate([on_wing(side * 12.0, 6.9, 0.2), on_wing(side * 19.4, 7.9, 0.2), on_wing(side * 19.8, 7.1, 0.2),
           on_wing(side * 12.0, 6.1, 0.2)][::side], 0.08, 'maroon')
    for z in (-8.5, -11.0):
        box((side * 17.8, y0 + 1.3, z), (6.0, 0.06, 0.18), 'dark')
    # Down-turned fin at the wing tip (seen trailing on the kit's wing ends).
    plate([(side * 23.0, y1, -5.8), (side * 23.1, y1, -11.2), (side * 22.4, -6.2, -16.0), (side * 22.6, -4.0, -10.5)],
          0.5, 'hull', bevel=0.06)
    plate([(side * 22.2, y1 + 0.6, -3.0), (side * 22.8, y1 + 0.6, -9.0), (side * 22.0, y1 + 3.4, -12.5)], 0.4, 'panel')


mirrored(wing)


# ---------------------------------------------------------------- pods (white-nosed, forward)
def pod(side, up, aft, fore, r=1.75):
    x = side * 11.6
    loft([(aft, ring(x, r * 0.7, up - r * 0.7, up + r * 0.7, n=18, p=2.0)),
          (aft + 1.2, ring(x, r, up - r, up + r, n=18, p=2.0)),
          (fore, ring(x, r, up - r, up + r, n=18, p=2.0))], 'hull', name='pod')
    tube([(x, up, fore - 0.05), (x, up, fore + 1.8), (x, up, fore + 3.6), (x, up, fore + 5.2)],
         [r * 0.98, r * 0.82, r * 0.45, 0.0], 'white', n=18, name='pod-nose')
    box((x, up, fore - 0.2), (r * 2.05, r * 2.05, 0.3), 'maroon')
    box((x, up + (r + 0.05 if up > 0 else -r - 0.05), (aft + fore) / 2), (0.6, 0.12, fore - aft - 3), 'green')


for s in (-1, 1):
    pod(s, 1.9, -6.0, 11.5)
    pod(s, -4.6, -1.5, 14.0)
    box((s * 11.6, 0.4, 2.0), (0.8, 1.2, 8.0), 'hull')         # pylons to the wing
    box((s * 11.6, -2.3, 5.0), (0.8, 1.6, 8.0), 'hull')

# ---------------------------------------------------------------- bridge tower and crown
loft([(-12.0, ring(0, 2.4, 3.0, 6.0, n=20, p=2.6)), (-9.5, ring(0, 2.9, 3.0, 8.6, n=20, p=2.6)),
      (-4.0, ring(0, 2.6, 3.0, 10.0, n=20, p=2.6)), (1.5, ring(0, 2.0, 3.0, 8.4, n=20, p=2.6)),
      (5.0, ring(0, 1.2, 3.0, 5.2, n=20, p=2.6)), (7.0, ring(0, 0.6, 3.2, 4.0, n=20, p=2.6))], 'hull', name='tower')
loft([(-8.5, ring(0, 2.95, 8.0, 8.9, n=16, p=3.0)), (-2.5, ring(0, 2.6, 8.4, 9.4, n=16, p=3.0))], 'window', name='bridge-band')
loft([(-10.5, ring(0, 1.8, 9.2, 10.3, n=16, p=2.4)), (-3.0, ring(0, 1.7, 9.6, 10.7, n=16, p=2.4))], 'panel', name='bridge-roof')
# Crown: a V of tall swept fins over the bridge, as in the design's front view.
mirrored(lambda s: plate([(s * 0.9, 9.6, -9.8), (s * 1.0, 9.8, -4.2), (s * 2.4, 12.6, -7.4), (s * 3.2, 13.4, -11.8)][::s],
                         0.35, 'panel', bevel=0.05))
mirrored(lambda s: plate([(s * 2.0, 8.0, -1.0), (s * 2.0, 6.0, 4.0), (s * 3.4, 9.2, -2.8)][::s], 0.3, 'hull'))
cylinder((0, 10.5, -6.5), (0, 11.6, -6.5), 0.2, 'grey', segments=8)
sphere((0, 10.8, -9.0), 0.55, 'panel', segments=12)


# ---------------------------------------------------------------- dorsal horns and guns
def horn(side, base_z, base_y, length, lean=1.0, r=0.55):
    x = side * 1.5
    pts = [(x, base_y - 0.3, base_z), (x + side * 0.4 * lean, base_y + length * 0.35, base_z - length * 0.35),
           (x + side * 0.9 * lean, base_y + length * 0.62, base_z - length * 0.85),
           (x + side * 1.3 * lean, base_y + length * 0.7, base_z - length * 1.35)]
    tube(pts, [(r * 0.6, r * 2.4), (r * 0.45, r * 1.6), (r * 0.3, r * 0.8), 0.0], 'hull', n=10, name='horn')


for s in (-1, 1):
    horn(s, 30.0, 2.6, 5.0)
    horn(s, 25.5, 3.0, 5.6)
    horn(s, 38.5, 1.7, 3.2, r=0.4)


def main_gun(z, up, facing=1):
    cylinder((0, up - 0.2, z), (0, up + 0.4, z), 1.25, 'panel', segments=18)
    loft([(z - facing * 1.1, ring(0, 1.0, up + 0.3, up + 1.1, n=12, p=3)),
          (z + facing * 1.2, ring(0, 0.85, up + 0.3, up + 0.8, n=12, p=3))], 'hull', name='gun')
    for s in (-1, 1):
        cylinder((s * 0.38, up + 0.6, z + facing * 0.8), (s * 0.38, up + 0.6, z + facing * 3.6), 0.16, 'dark', segments=8)


main_gun(34.0, 2.2)          # forward upper deck pair
main_gun(21.0, 3.3)
main_gun(13.0, 3.6)
main_gun(-14.5, 3.6, facing=-1)   # aft, trained astern

# Aura vulcans along the hull.
AA = [(s * 3.2, 2.9, z) for s in (-1, 1) for z in (28.0, 18.0, 8.0)]
AA += [(s * 7.2, 4.1, z) for s in (-1, 1) for z in (1.0, -15.0)]
AA += [(s * 17.0, 0.9, z) for s in (-1, 1) for z in (-12.0,)]
for x, up, z in AA:
    box((x, up + 0.2, z), (0.6, 0.4, 0.6), 'panel', bevel=0.04)
    cylinder((x, up + 0.3, z), (x, up + 0.3, z + 1.0), 0.07, 'dark', segments=6)

KIT.export(OUT, 'Goraon', previews=PREVIEWS, reference=5593)
