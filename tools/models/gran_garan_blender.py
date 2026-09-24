"""Author the HD Gran Garan (world-map ship, original resource 5594) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/gran_garan_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, about 76 units = 510 m), model.glb and optional previews
with the original rendered from the same cameras (compare.png).

Design reference: Aura Battle Ship Gran Garan from Aura Battler Dunbine, the
flagship of Queen Sheela of the land of Na (captain Kawasse Goo). Official figures
used: length 510 met, height 390, beam 510, dry weight 48,000 ruftons; described
as "three blocks and a bridge that towers like a castle", a tetrahedral hull whose
three sub-hulls spread from a tower-like central hull so that its guns cover every
direction; pastel blue; many triple AA mounts and twin aura cannons. Sources: the
official site dunbine.net (Aura Machine page and its design drawing), ja.wikipedia
"オーラマシン" (艦船 section), the 1984 design sheet (rear view with plan inset,
reproduced at kopenguin.com), and a licensed 1/2400 garage kit (colours: pastel
blue hull, maroon-brown decks on the outer halves of the arms, black detail lines,
capsule pods under the arms near the hub).

Layout: one sub-hull runs forward to the bow at +z and two sweep back at about
117 degrees either side, all sloping down from the hub to their tips; the tower
rises from the hub with antler-like horns (the longest sweeping back), bridge
bulges at two levels and twin spires; twin thruster ports and a ventral spike on
the hub's stern face. The original 138-triangle model has the same tripod: arm tips
at (0, -11, 55) and (+-39, -11, -20), tower to y 51, horns trailing toward -z.
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
    'hull': (150, 178, 224, 255), 'panel': (198, 214, 240, 255), 'shade': (108, 130, 186, 255),
    'deck': (122, 58, 50, 255), 'dark': (44, 50, 72, 255), 'window': (28, 40, 70, 255),
    'glow': (170, 215, 255, 40),
})
cylinder, sphere, plate, mirrored, add_object = KIT.cylinder, KIT.sphere, KIT.plate, KIT.mirrored, KIT.add_object


def ring(cx, hw, bottom, top, n=20, p=2.6, cy=None):
    """Rounded (superellipse) section as (lateral, height) points."""
    cy = (bottom + top) / 2 if cy is None else cy
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        c, s = math.cos(a), math.sin(a)
        h = (top - cy) if s >= 0 else (cy - bottom)
        pts.append((cx + hw * math.copysign(abs(c) ** (2 / p), c), cy + h * math.copysign(abs(s) ** (2 / p), s)))
    return pts


def skin(rings, color, cap=True, name='skin', bevel=0.0):
    """Loft through rings of original-frame points (equal counts; a 1-point ring is a tip)."""
    bm = bmesh.new()
    vr = [[bm.verts.new(P(*p)) for p in r] for r in rings]
    for a, b in zip(vr, vr[1:]):
        if len(a) == 1 or len(b) == 1:
            tip, rim = (a[0], b) if len(a) == 1 else (b[0], a)
            for k in range(len(rim)):
                bm.faces.new((rim[k], rim[(k + 1) % len(rim)], tip))
            continue
        for k in range(len(a)):
            bm.faces.new((a[k], a[(k + 1) % len(a)], b[(k + 1) % len(a)], b[k]))
    if cap:
        for end in (vr[0], vr[-1]):
            if len(end) > 2:
                bm.faces.new(end)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return add_object(bm, color, bevel, name=name)


def tube(points, radii, color, n=10, name='tube'):
    """Round (or (rx, ry) elliptical) tube through original-frame points; radius 0 ends in a point."""
    pts = [Vector(p) for p in points]
    rings, normal = [], None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        ref = normal if normal is not None else (Vector((0, 1, 0)) if abs(t.y) < 0.9 else Vector((0, 0, 1)))
        normal = (ref - t * ref.dot(t)).normalized()
        side = t.cross(normal)
        rx, ry = radii[i] if isinstance(radii[i], tuple) else (radii[i], radii[i])
        if max(rx, ry) <= 1e-6:
            rings.append([tuple(p)])
            continue
        rings.append([tuple(p + side * (rx * math.cos(2 * math.pi * k / n)) + normal * (ry * math.sin(2 * math.pi * k / n)))
                      for k in range(n)])
    return skin(rings, color, name=name)


# ---------------------------------------------------------------- three sub-hulls
def arm_frame(theta):
    """(u along the arm, s across, h up) -> original (x, y, z) for an arm at azimuth theta from +z."""
    st, ct = math.sin(theta), math.cos(theta)
    return lambda u, s, h: (u * st + s * ct, h, u * ct - s * st)


def along(profile, u):
    """(half width, bottom, top) of a (u, hw, bottom, top) profile, interpolated at u."""
    for (u0, *a), (u1, *b) in zip(profile, profile[1:]):
        if u0 <= u <= u1:
            t = (u - u0) / (u1 - u0)
            return tuple(x + (y - x) * t for x, y in zip(a, b))
    return tuple(profile[-1][1:])


def arm(theta, length):
    f = arm_frame(theta)
    L = length
    # (u, half width, bottom, top): the top ridge falls from the hub to a pointed tip.
    profile = [(0.0, 7.0, -4.0, 4.0), (0.15 * L, 6.6, -5.6, 3.0), (0.35 * L, 6.0, -7.4, 1.2),
               (0.55 * L, 5.2, -8.8, -1.4), (0.75 * L, 4.0, -9.9, -4.2), (0.9 * L, 2.6, -10.4, -6.8),
               (0.98 * L, 1.1, -10.3, -8.8)]
    rings = [[f(u, s, h) for s, h in ring(0, hw, b, t, n=24, p=2.7)] for u, hw, b, t in profile]
    skin(rings + [[f(L, 0, -9.9)]], 'hull', name='arm')
    # Maroon deck on the outer half, riding the falling top ridge.
    deck = [(u, hw * 0.52, t - 0.3, t + 0.14) for u, hw, b, t in profile[2:6]]
    skin([[f(u, s, h) for s, h in ring(0, hw, b, t, n=16, p=4.0)] for u, hw, b, t in deck], 'deck', name='deck')
    # Light upper strakes and the black detail line along each flank.
    for side in (-1, 1):
        skin([[f(u, side * (hw * 0.86) + ds, h + dh) for ds, dh in ring(0, 0.5, -0.45, 0.45, n=10, p=2.0)]
              for u, hw, b, t in profile[1:6] for h in [t - (t - b) * 0.18]], 'panel', name='strake')
        skin([[f(u, side * (hw * 0.99) + ds, h + dh) for ds, dh in ring(0, 0.22, -0.3, 0.3, n=8, p=2.0)]
              for u, hw, b, t in profile[1:6] for h in [b + (t - b) * 0.45]], 'dark', name='line')
    # Capsule pod slung under the arm near the hub.
    pod = [(0.2, 0.9, (1.0, 0.7)), (0.24, 1.35, (2.3, 1.3)), (0.33, 1.55, (2.8, 1.6)), (0.5, 1.5, (2.8, 1.6)),
           (0.58, 1.2, (2.2, 1.3)), (0.61, 0.8, (0.8, 0.5))]
    tube([f(t * L, 0, along(profile, t * L)[1] - d) for t, d, r in pod], [r for t, d, r in pod], 'panel', n=18, name='pod')
    tube([f(t * L, 0, along(profile, t * L)[1] - 2.95) for t in (0.3, 0.52)], [(0.35, 0.12), (0.35, 0.12)], 'dark', n=8)
    # Forked prongs at the tip.
    for side in (-1, 1):
        tube([f(0.9 * L, side * 1.6, -8.8), f(0.97 * L, side * 2.2, -9.3), f(1.02 * L, side * 2.6, -9.8)],
             [0.35, 0.22, 0.0], 'hull', n=8, name='prong')
    # Twin aura cannons and triple AA mounts along the ridge.
    for t_u in (0.3, 0.52, 0.72):
        u = t_u * L
        top = along(profile, u)[2]
        cx, cy, cz = f(u, 0, top + 0.4)
        cylinder((cx, top - 0.3, cz), (cx, top + 0.5, cz), 0.9, 'shade', segments=14)
        for side in (-1, 1):
            a = f(u, side * 0.35, top + 0.7)
            b = f(u + 2.2, side * 0.35, top + 0.7 - 0.5)
            cylinder(a, b, 0.14, 'dark', segments=6)
    for u, side in ((0.45 * L, 1), (0.45 * L, -1), (0.66 * L, 1), (0.66 * L, -1)):
        hw, b, t = along(profile, u)
        x, y, z = f(u, side * hw * 0.88, b + (t - b) * 0.62)
        sphere((x, y, z), 0.55, 'shade', segments=10)
    # Short spikes along the ridge near the hub.
    for u, side in ((0.16 * L, 1), (0.16 * L, -1), (0.26 * L, 1), (0.26 * L, -1)):
        top = along(profile, u)[2]
        tube([f(u, side * 2.4, top - 0.8), f(u - 1.0, side * 3.2, top + 1.6), f(u - 2.4, side * 3.6, top + 3.4)],
             [0.45, 0.25, 0.0], 'hull', n=8)
    # Root buttress flowing from the tower down onto the arm, as in the design's rear view.
    tube([f(1.5, 0, 12.0), f(0.1 * L, 0, 6.5), f(0.2 * L, 0, 3.4), f(0.3 * L, 0, 2.0)],
         [(1.6, 2.6), (1.6, 2.2), (1.2, 1.2), 0.0], 'hull', n=14, name='buttress')


BOW, REAR = 54.0, math.hypot(39.0, 20.0) - 0.5
REAR_ANGLE = math.atan2(39.0, -20.0)
arm(0.0, BOW)
arm(REAR_ANGLE, REAR)
arm(-REAR_ANGLE, REAR)

# ---------------------------------------------------------------- hub
sphere((0, 0.5, 0), 7.4, 'hull', scale=(1.0, 0.8, 1.0), segments=28)
sphere((0, 3.2, 0), 6.2, 'panel', scale=(1.0, 0.5, 1.0), segments=24)
# Stern face: housing with twin thruster ports, and a ventral spike beneath.
hx, hz = 0.0, -6.4
skin([[(x, y, z) for x, y in [(hx + dx, -1.2 + dy) for dx, dy in ring(0, 3.4, -2.6, 2.4, n=20, p=3.0)]]
      for z in (hz + 3.0, hz)], 'hull', name='port-housing')
for s in (-1, 1):
    cylinder((s * 1.55, -1.1, hz + 0.2), (s * 1.55, -1.1, hz - 0.4), 1.25, 'shade', segments=18)
    cylinder((s * 1.55, -1.1, hz - 0.35), (s * 1.55, -1.1, hz - 0.45), 0.95, 'glow', segments=16)
tube([(0, -3.6, hz + 0.4), (0, -6.4, hz - 0.2), (0, -9.4, hz - 0.8), (0, -11.6, hz - 1.2)], [0.9, 0.6, 0.3, 0.0], 'hull', n=10)
tube([(0, -5.2, 1.0), (0, -8.2, 0.5), (0, -11.2, 0.0)], [1.4, 0.7, 0.0], 'shade', n=12)

# ---------------------------------------------------------------- castle tower
TOWER = [(3.0, 8.0, 8.0, 0.0), (7.0, 6.2, 6.0, 0.3), (11.0, 4.8, 4.5, 0.5), (15.5, 3.8, 3.6, 0.5),
         (19.5, 4.6, 4.3, 0.3), (23.0, 4.0, 3.6, 0.0), (27.0, 3.0, 2.7, -0.3), (31.5, 2.5, 2.3, -0.3),
         (34.5, 3.1, 2.7, -0.2), (37.5, 2.5, 2.1, 0.0), (41.0, 1.9, 1.6, 0.0), (43.0, 1.2, 1.0, 0.1)]


def level(y, hx_, hz_, cz, n=24, p=2.3):
    return [(x, y, cz + z) for x, z in ring(0, hx_, -hz_, hz_, n=n, p=p)]


skin([level(*t) for t in TOWER], 'hull', name='tower')
# Bridge bands (dark window rings) at the two bulges, and light ribs between levels.
for y, grow in ((19.8, 0.15), (35.0, 0.12)):
    hx_, hz_ = [(a, b) for yy, a, b, c in TOWER if yy <= y][-1]
    skin([level(y - 0.6, hx_ + grow, hz_ + grow, 0.3), level(y + 0.6, hx_ + grow, hz_ + grow, 0.3)], 'window', name='bridge')
for y in (25.0, 39.0):
    hx_, hz_, cz = [(a, b, c) for yy, a, b, c in TOWER if yy <= y][-1]
    skin([level(y - 0.25, hx_ * 0.95 + 0.1, hz_ * 0.95 + 0.1, cz), level(y + 0.25, hx_ * 0.9 + 0.1, hz_ * 0.9 + 0.1, cz)],
         'panel', name='rib')
# Trunk ridges: the tower reads as bundled, root-like columns rather than a smooth cone.
for k in range(8):
    a = 2 * math.pi * (k + 0.5) / 8
    rows = [t for t in TOWER if t[0] <= 31.5]
    tube([(hx_ * math.cos(a) * 0.97, y, cz + hz_ * math.sin(a) * 0.97) for y, hx_, hz_, cz in rows],
         [(0.9 - 0.08 * i, 0.5 - 0.03 * i) for i in range(len(rows))], 'hull' if k % 2 else 'panel', n=8, name='ridge')
# Twin spires at the crown, the left one taller.
tube([(-0.7, 41.0, 0.2), (-0.6, 45.0, 0.4), (-0.5, 51.0, 0.8)], [1.1, 0.7, 0.0], 'hull', n=12)
tube([(0.8, 41.0, -0.2), (1.0, 44.5, -0.4), (1.2, 48.2, -0.8)], [0.95, 0.55, 0.0], 'panel', n=12)


# ---------------------------------------------------------------- antler horns
def horn(root, tip, bend, r, color='hull'):
    """Curved horn from a point on the tower toward tip; bend lifts the middle."""
    root, tip = Vector(root), Vector(tip)
    mid = (root + tip) / 2 + Vector((0, bend, 0))
    pts = [root, root.lerp(mid, 0.5), mid, mid.lerp(tip, 0.5), tip]
    tube([tuple(p) for p in pts], [(r * 0.7, r * 1.3), (r * 0.6, r * 1.05), (r * 0.45, r * 0.8), (r * 0.28, r * 0.45), 0.0],
         color, n=10, name='horn')


for s in (-1, 1):
    # Long trailing pairs (the original's two pairs of spikes toward -z).
    horn((s * 3.6, 10.5, -1.0), (s * 10.8, 22.5, -16.8), 2.0, 1.1)
    horn((s * 2.0, 27.5, -1.0), (s * 6.0, 40.5, -18.0), 1.6, 0.9)
    # Side and forward branches, shorter, as in the design's rear view.
    horn((s * 3.8, 13.0, 1.5), (s * 11.5, 19.5, 2.5), 1.5, 0.8, 'panel')
    horn((s * 3.2, 21.0, 1.0), (s * 9.0, 29.0, -3.0), 1.2, 0.7)
    horn((s * 2.2, 30.5, 0.5), (s * 6.5, 35.5, 3.0), 0.8, 0.5, 'panel')
    horn((s * 1.8, 37.5, -0.5), (s * 4.5, 43.5, -3.5), 0.8, 0.45)
    horn((s * 4.6, 6.0, 3.5), (s * 7.5, 12.0, 8.5), 0.8, 0.55, 'panel')
    # Bridge wing lookouts.
    tube([(s * 3.9, 19.8, 0.5), (s * 6.0, 20.2, 0.8), (s * 6.8, 20.4, 0.9)], [(0.35, 0.6), (0.3, 0.45), 0.0], 'panel', n=8)
horn((0, 24.0, 3.0), (0, 30.0, 8.0), 0.6, 0.6, 'panel')
# Crown of small spikes under the spires.
for k in range(6):
    a = 2 * math.pi * k / 6 + 0.3
    horn((1.4 * math.cos(a), 40.5, 1.2 * math.sin(a)), (4.2 * math.cos(a), 45.5, 3.6 * math.sin(a)), 0.5, 0.35,
         'panel' if k % 2 else 'hull')
horn((0, 16.0, 3.2), (0, 20.0, 9.0), 0.8, 0.7)
# Masts on the bridge roofs.
cylinder((0, 42.5, -1.2), (0, 45.0, -1.2), 0.12, 'dark', segments=6)
cylinder((0, 23.0, -2.8), (0, 25.5, -3.2), 0.12, 'dark', segments=6)

KIT.export(OUT, 'Gran Garan', previews=PREVIEWS, reference=5594)
