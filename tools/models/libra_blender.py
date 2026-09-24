"""Author the HD Libra (world-map fortress, original resource 5590, part 0) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/libra_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Y up, the
four blocks on the diagonals of the XZ plane, units of the N64 mesh), model.glb
and optional previews with the original part 0 rendered from the same cameras
(compare.png). Part 1 of the resource, the リーブラ name plate, stays original
and is not modelled here.

Design reference: the Peacemillion-class super-large space battleship Libra from
Mobile Suit Gundam Wing. Official description (Gundam Channel mechanic manual):
an octahedral main block with four diamond-patterned engine blocks set
horizontally at its vertices; the main cannon sits in the central block and
numerous beam cannons cover the hull. Full length about 3,500 m (SRW Wiki).
Layout and detail follow the GUNDAM Official Website line art (terraced
rhombic blocks with vertical fins on top and pylons underneath) and the
Gundam Channel colour render and a TV-series top view (near-black armour with
gold circuit lines, grey-green terraces, silver ridge lines, a light hub with a
square opening). The original spans 84 x 70 units, so one unit is roughly 42 m.

Sources:
  https://www.gundam-c.com/manual/mechanic/w/libra.html
  https://gundam-official.com/mecha/yldmfdcn97c8ji2tfvvmo2jg
  https://srw.wiki.cre.jp/wiki/%E3%83%AA%E3%83%BC%E3%83%96%E3%83%A9
Reference images were only viewed for comparison; none are in the repository.
"""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'armour': (44, 50, 64, 255),      # near-black navy hull
    'armour_mid': (68, 76, 92, 255),
    'under': (34, 38, 48, 255),
    'terrace': (142, 158, 152, 255),  # grey-green terraces (anime colours)
    'hub': (164, 178, 172, 255),
    'silver': (200, 206, 208, 255),
    'gold': (218, 176, 60, 255),
})
box, cylinder, add_object = KIT.box, KIT.cylinder, KIT.add_object


def loft3(rings, color, bevel=0.0, cap=True, name='loft3', segments=2):
    """Loft through rings of arbitrary original-coordinate points (x, up, fwd)."""
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


def strip(a, b, normal, width, color, lift=0.07, thickness=0.1):
    """Thin raised line from a to b lying on a face with the given outward normal."""
    a, b, n = Vector(a), Vector(b), Vector(normal).normalized()
    side = n.cross(b - a).normalized() * (width / 2)
    pts = [a - side, a + side, b + side, b - side]
    KIT.plate([tuple(p + n * lift) for p in pts], thickness, color, name='strip')


def chamfer(poly, frac):
    """Cut every corner of a polygon by frac of its adjacent edges."""
    out = []
    for i, c in enumerate(poly):
        prev, nxt = poly[i - 1], poly[(i + 1) % len(poly)]
        f = frac[i] if isinstance(frac, (list, tuple)) else frac
        out.append((c[0] + f * (prev[0] - c[0]), c[1] + f * (prev[1] - c[1])))
        out.append((c[0] + f * (nxt[0] - c[0]), c[1] + f * (nxt[1] - c[1])))
    return out


# ---------------------------------------------------------------- engine blocks
# Each block is a rhombus in plan whose long diagonal runs outward from a corner
# of the hub (inner tip INNER, outer tip OUTER); local u runs along it, v across.
INNER, OUTER, HALF_W = (7.0, 7.0), (42.0, 35.0), 15.5
LENGTH = math.hypot(OUTER[0] - INNER[0], OUTER[1] - INNER[1])
EU = ((OUTER[0] - INNER[0]) / LENGTH, (OUTER[1] - INNER[1]) / LENGTH)
EV = (-EU[1], EU[0])
RHOMBUS = chamfer([(0.0, 0.0), (LENGTH / 2, -HALF_W), (LENGTH, 0.0), (LENGTH / 2, HALF_W)],
                  [0.09, 0.12, 0.09, 0.12])


def block(sx, sz):
    def world(u, v, up):
        return (sx * (INNER[0] + u * EU[0] + v * EV[0]), up, sz * (INNER[1] + u * EU[1] + v * EV[1]))

    def ring(k, up, poly=RHOMBUS):
        # Similar polygons about the block centre keep every side face planar.
        return [world(LENGTH / 2 + (u - LENGTH / 2) * k, v * k, up) for u, v in poly]

    # Base slab: dark armour walls with a silver window band, grey-green terrace on top.
    loft3([ring(0.95, -3.0), ring(1.0, -2.4), ring(1.0, 0.8)], 'armour', bevel=0.1, name='base')
    loft3([ring(1.006, -1.25), ring(1.006, -0.55)], 'silver', name='window-band')
    loft3([ring(0.99, 0.8), ring(0.975, 1.05)], 'terrace', name='terrace')
    # Second tier and the stepped pyramid: dark slopes carrying the gold circuit pattern.
    loft3([ring(0.80, 1.0), ring(0.80, 1.9), ring(0.77, 2.25)], 'armour_mid', bevel=0.08, name='tier')
    loft3([ring(0.81, 1.9), ring(0.81, 1.98)], 'terrace', name='tier-edge')
    low, high = ring(0.77, 2.25), ring(0.36, 5.6)
    loft3([low, high], 'armour', bevel=0.06, name='pyramid')
    loft3([ring(0.38, 5.6), ring(0.34, 6.0)], 'terrace', bevel=0.04, name='crown')
    # Hipped cap: the ridge runs along the long diagonal (anisotropic top ring).
    loft3([ring(0.30, 6.0), [world(LENGTH / 2 + (u - LENGTH / 2) * 0.2, v * 0.03, 7.6) for u, v in RHOMBUS]],
          'silver', bevel=0.03, name='peak')

    # Silver hip lines up the pyramid from each rhombus corner (chamfer midpoints).
    for c in range(4):
        a0, a1 = Vector(low[2 * c]), Vector(low[2 * c + 1])
        b0, b1 = Vector(high[2 * c]), Vector(high[2 * c + 1])
        cylinder(tuple((a0 + a1) / 2 + Vector((0, 0.08, 0))), tuple((b0 + b1) / 2 + Vector((0, 0.08, 0))),
                 0.22, 'silver', segments=4)

    # Gold circuits: short Manhattan runs on the four big slope faces.
    rng = random.Random(5590 + 3 * sx + sz)
    for c in range(4):
        i, j = 2 * c + 1, (2 * c + 2) % len(low)
        a0, a1, b0, b1 = (Vector(p) for p in (low[i], low[j], high[i], high[j]))
        normal = (a1 - a0).cross(b0 - a0).normalized()
        if normal.y < 0:
            normal = -normal

        def face(s, t):
            return tuple(a0.lerp(a1, s).lerp(b0.lerp(b1, s), t))

        for _ in range(6):
            s, t = rng.uniform(0.12, 0.88), rng.uniform(0.12, 0.7)
            horizontal = rng.random() < 0.5
            for _ in range(3):
                if horizontal:
                    s2 = min(0.9, max(0.1, s + rng.choice((-1, 1)) * rng.uniform(0.12, 0.3)))
                    strip(face(s, t), face(s2, t), normal, 0.34, 'gold')
                    s = s2
                else:
                    t2 = min(0.82, max(0.08, t + rng.choice((-1, 1)) * rng.uniform(0.2, 0.45)))
                    strip(face(s, t), face(s, t2), normal, 0.34, 'gold')
                    t = t2
                horizontal = not horizontal
        # A long gold trace along the foot of each slope.
        strip(face(0.1, 0.1), face(0.9, 0.1), normal, 0.3, 'gold')

    # Twin towers on the ridge line either side of the peak.
    for u in (0.36, 0.64):
        uc = LENGTH * u
        foot = [(uc - 1.0, -0.7), (uc + 1.0, -0.7), (uc + 1.0, 0.7), (uc - 1.0, 0.7)]
        rise = 5.4 + 1.6 * (1 - abs(u - 0.5) * 2)
        loft3([[world(pu, pv, 4.6) for pu, pv in foot],
               [world(pu, pv, rise + 2.2) for pu, pv in foot],
               [world(uc + (pu - uc) * 0.6, pv * 0.8, rise + 2.8) for pu, pv in foot]],
              'armour_mid', bevel=0.08, name='tower')
        loft3([[world(pu, pv, rise + 2.3) for pu, pv in foot],
               [world(pu, pv, rise + 2.5) for pu, pv in foot]], 'silver', name='tower-band')

    # Twin beam-cannon mounts around the terrace, barrels trained outward.
    for u, v in ((0.12, 0.0), (0.5, 0.86), (0.5, -0.86), (0.88, 0.0), (0.3, 0.52), (0.7, -0.52),
                 (0.3, -0.52), (0.7, 0.52)):
        cu, cv = LENGTH * u, HALF_W * v * (1 - abs(u - 0.5) * 1.1)
        cx, _, cz = world(cu, cv, 0)
        box((cx, 1.35, cz), (1.3, 0.6, 1.3), 'silver', bevel=0.06)
        ox, _, oz = world(cu + (u - 0.5) * 2.4, cv * 1.25, 0)
        d = Vector((ox - cx, 0, oz - cz)).normalized()
        side = Vector((-d.z, 0, d.x)) * 0.25
        for k in (-1, 1):
            a = Vector((cx, 1.5, cz)) + side * k
            cylinder(tuple(a), tuple(a + d * 1.5), 0.1, 'armour', segments=6)

    # Underside: a shallow inverted terrace and two pylons.
    loft3([ring(0.95, -3.0), ring(0.62, -5.0), ring(0.28, -6.1)], 'under', bevel=0.1, name='keel')
    for u in (0.3, 0.7):
        uc = LENGTH * u
        pts = [world(uc - 2.0, 0.0, -4.4), world(uc + 2.0, 0.0, -4.4),
               world(uc + 1.1, 0.0, -9.0), world(uc - 0.7, 0.0, -9.0)]
        KIT.plate(pts, 0.8, 'armour_mid', bevel=0.08, name='pylon')


for sx in (-1, 1):
    for sz in (-1, 1):
        block(sx, sz)

# ---------------------------------------------------------------- central block
# Octahedral hub carrying the main cannon: equator corners on the diagonals where
# the engine blocks join, a square opening on top, the cannon muzzle underneath.
SQUARE = chamfer([(9.0, 9.0), (-9.0, 9.0), (-9.0, -9.0), (9.0, -9.0)], 0.12)


def hub_ring(k, up):
    return [(x * k, up, z * k) for x, z in SQUARE]


loft3([hub_ring(0.34, -6.0), hub_ring(1.0, -0.8), hub_ring(1.0, 0.8), hub_ring(0.36, 6.0)], 'hub',
      bevel=0.14, name='hub')
loft3([hub_ring(1.02, -0.35), hub_ring(1.02, 0.35)], 'armour', name='hub-belt')
loft3([hub_ring(0.40, 5.8), hub_ring(0.40, 6.5)], 'silver', bevel=0.05, name='hub-crown')
box((0, 6.52, 0), (4.4, 0.08, 4.4), 'nozzle')
box((0, 6.2, 0), (2.4, 0.2, 2.4), 'dark')
# Dark triangular ports on each upper face.
for ang in range(4):
    a = math.radians(90 * ang)
    c, s = math.cos(a), math.sin(a)
    tri = [(-2.2, 1.6), (2.2, 1.6), (0.0, 4.4)]  # (across, up) on the face
    pts = []
    for across, up in tri:
        d = 9.0 * (1 - (up - 0.8) / 5.2 * 0.64)  # face offset at this height
        pts.append((c * d - s * across, up, s * d + c * across))
    # Nudge outwards along the face normal so the port sits on the face.
    n = Vector((c * 5.2, 5.76, s * 5.2)).normalized()
    KIT.plate([tuple(Vector(p) + n * 0.08) for p in pts], 0.12, 'window', name='port')
# Main cannon muzzle under the hub.
cylinder((0, -5.4, 0), (0, -7.4, 0), 2.8, 'grey', segments=24, bevel=0.06)
cylinder((0, -7.2, 0), (0, -7.8, 0), 3.1, 'silver', segments=24, bevel=0.04)
cylinder((0, -6.8, 0), (0, -7.85, 0), 1.9, 'nozzle', segments=20)

KIT.export(OUT, 'Libra', previews=PREVIEWS, reference=5590, reference_parts=[0])
