"""Author the HD Peacemillion (world-map ship, original resource 5589) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/peacemillion_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh), model.glb and optional previews with the original rendered
from the same cameras (compare.png).

Identity: 5589 is world-map model table index 5, reached only through 3D72 7 (no
unit maps to it and the original scripts never use that parameter). Its 64x64
line-art texture is a top view of a pointed fan with converging ribs, and the mesh
is a very wide flat fan (x -66..66, z -27..27) rising to a central ridge, with a
ventral keel fin at the stern. That matches Peacemillion from New Mobile Report
Gundam Wing, which SRW64 fields as a neutral ship (unit 137), and the 3D72
parameters next to it (5 Barge, 6 Libra) are the other After Colony space
fortresses. The original's pointed bow faces +Z, kept here.

Design reference: Peacemillion-class super-large space battleship (Howard and
Professor G). Official description: a fan-shaped (扇形) form with a pure white hull,
about 3000 m long and 1000 m high, beam cannons in slits in the bow, stealth
(hyper jammer) built around the peculiar form. The official colour art (top and
bottom) shows stepped fan blades radiating from the bow, a central ridge with the
hatched bow slits and two round ports, black structural lines and lavender shading
on white, and underneath a narrow keel hull running aft past the trailing edge
with navy keel fins. The original's proportions (132 x 54 x 18 units, height a third
of the length) are kept; the fan steps, slits and keel follow the colour art.

Sources: https://www.gundam-c.com/manual/mechanic/w/peace-million.html,
https://en.gundam-official.com/mecha/zs44riut8i9pbh3hgfmovvjc (colour art),
https://srw.wiki.cre.jp/wiki/%E3%83%94%E3%83%BC%E3%82%B9%E3%83%9F%E3%83%AA%E3%82%AA%E3%83%B3,
https://gundam.fandom.com/wiki/Peacemillion (top and bottom colour art).
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
    'hull': (238, 240, 244, 255), 'panel': (212, 212, 228, 255), 'lavender': (176, 176, 206, 255),
    'line': (40, 42, 54, 255), 'slit': (26, 28, 38, 255), 'rib': (150, 152, 166, 255),
    'keelfin': (44, 54, 118, 255), 'mark': (228, 196, 60, 255),
})
box, cylinder, plate, mirrored, add_object = KIT.box, KIT.cylinder, KIT.plate, KIT.mirrored, KIT.add_object


def ring_loft(rings, color, bevel=0.0, cap=True, name='ring-loft', segments=2):
    """Loft through rings of (x, up, fwd) points in any orientation."""
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


# ---------------------------------------------------------------- planform
# The fan pivots at the bow; ribs are rays from PIVOT measured from the aft axis. The
# trailing edge runs from the tip (66, -15) in to (10, -18.5) as on the original.
PIVOT_F, PIVOT_UP = 27.0, 0.3
BASE = -0.6          # underside of the stepped blades
TIP_ANGLE = 57.5
CENTRE_ANGLE = 15.0  # half angle of the central ridge block
RAYS = [15.0, 24.0, 32.0, 39.0, 45.0, 50.5, 54.5, 57.5]
STEP = 0.9


def te_distance(angle):
    """Distance along a rib from the pivot to the trailing edge."""
    a = math.radians(angle)
    slope = 3.5 / 56.0
    return (PIVOT_F + 18.5 + 10 * slope) / (math.cos(a) + slope * math.sin(a))


def rib_point(angle, t, side=1):
    a = math.radians(angle)
    return side * t * math.sin(a), PIVOT_F - t * math.cos(a)


def ridge_height(angle):
    """Height of the blade ridge: 11 beside the centre block falling to 2.2 at the tip."""
    return 11.0 - (angle - CENTRE_ANGLE) / (TIP_ANGLE - CENTRE_ANGLE) * 8.8


def ridge_fraction(angle):
    return 0.76 + (angle - CENTRE_ANGLE) / (TIP_ANGLE - CENTRE_ANGLE) * 0.14


# ---------------------------------------------------------------- fan blades
def blade(side, inner, outer, color):
    """One fan blade: ramp from the bow up to a ridge, then a slope down to the trailing edge.
    Its inner edge stands STEP/2 below the blade inside it, so every rib shows a step."""
    rings = []
    for frac_key in ('front', 'ridge', 'rear'):
        ring_top, ring_bottom = [], []
        for angle, drop in ((inner, STEP / 2), (outer, -STEP / 2)):
            length = te_distance(angle)
            if frac_key == 'front':
                t, up = 1.2, PIVOT_UP + 0.05
            elif frac_key == 'ridge':
                t, up = length * ridge_fraction(angle), ridge_height(angle) - drop - 0.45
            else:
                t, up = length, 0.9 + (ridge_height(angle) - 2.2) * 0.12 - drop * 0.3
            x, f = rib_point(angle, t, side)
            ring_top.append((x, up, f))
            ring_bottom.append((x, BASE, f))
        # ring order: inner-bottom, outer-bottom, outer-top, inner-top
        rings.append([ring_bottom[0], ring_bottom[1], ring_top[1], ring_top[0]])
    ring_loft(rings, color, bevel=0.12, name='blade')


BLADE_COLORS = ['hull', 'hull', 'panel', 'hull', 'hull', 'panel', 'hull']
for s in (-1, 1):
    for k, (a, b) in enumerate(zip(RAYS, RAYS[1:])):
        blade(s, a, b, BLADE_COLORS[k])


def blade_top(angle, t, inner_side):
    """Height of the blade surface on a rib at distance t (inner_side: the blade outboard
    of this rib, i.e. the lower one)."""
    length = te_distance(angle)
    drop = STEP / 2 if inner_side else -STEP / 2
    ridge_t = length * ridge_fraction(angle)
    ridge_up = ridge_height(angle) - drop - 0.45
    rear_up = 0.9 + (ridge_height(angle) - 2.2) * 0.12 - drop * 0.3
    if t <= ridge_t:
        return PIVOT_UP + 0.05 + (ridge_up - PIVOT_UP - 0.05) * (t - 1.2) / (ridge_t - 1.2)
    return ridge_up + (rear_up - ridge_up) * (t - ridge_t) / (length - ridge_t)


# Black structural lines and lavender inset panels. Positions along a rib are given as
# fractions of that rib's length to the trailing edge, so decals follow ramp and slope.
def rib_line(side, angle, width=0.22, rear=False):
    """Dark line at the foot of the step on the outboard side of a rib."""
    ridge = ridge_fraction(angle)
    fracs = (0.06, ridge, 0.985) if rear else (0.06, ridge - 0.01)
    rings = []
    for frac in fracs:
        t = te_distance(angle) * frac
        x, f = rib_point(angle + 0.35, t, side)
        up = blade_top(angle, t, True)
        rings.append([(x - width, up - 0.05, f), (x + width, up - 0.05, f), (x + width, up + 0.08, f),
                      (x - width, up + 0.08, f)])
    ring_loft(rings, 'line', name='rib-line')


def blade_point(side, a, b, angle, frac, lift):
    """Point on the top of the blade between ribs a and b, on the ray `angle`."""
    t = te_distance(angle) * frac
    x, f = rib_point(angle, t, side)
    ua = blade_top(a, te_distance(a) * frac, True)
    ub = blade_top(b, te_distance(b) * frac, False)
    w = (angle - a) / (b - a)
    return x, ua + (ub - ua) * w + lift, f


def inset_panel(side, a, b, f0, f1, color, margin=1.2, lift=0.06):
    """Quad on a blade between two ribs (inset by margin degrees) from fraction f0 to f1."""
    pts = [blade_point(side, a, b, angle, frac, lift)
           for angle, frac in ((a + margin, f0), (b - margin, f0), (b - margin, f1), (a + margin, f1))]
    bm = bmesh.new()
    top = [bm.verts.new(P(*p)) for p in pts]
    bottom = [bm.verts.new(P(p[0], p[1] - 0.2, p[2])) for p in pts]
    bm.faces.new(top)
    bm.faces.new(list(reversed(bottom)))
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    add_object(bm, color, 0.03, name='inset')


MAJOR = (32.0, 45.0)  # the heavy black ribs of the colour art
for s in (-1, 1):
    for a in RAYS[1:-1]:
        rib_line(s, a, width=0.45 if a in MAJOR else 0.22, rear=a in MAJOR)
    for k, (a, b) in enumerate(zip(RAYS, RAYS[1:])):
        ridge = ridge_fraction(a)
        if k in (0, 3):
            inset_panel(s, a, b, ridge * 0.45, ridge * 0.72, 'lavender')
        elif k in (1, 4, 5):
            inset_panel(s, a, b, ridge * 0.6, ridge * 0.8, 'panel')
        # Black cross band just ahead of the ridge (the stepped lines of the texture) and a
        # dark rim along the trailing edge.
        inset_panel(s, a, b, ridge - 0.07, ridge - 0.04, 'line', margin=0.3)
        inset_panel(s, a, b, 0.955, 0.975, 'line', margin=0.3)
        # Lavender shade on the aft slope, as the colour art shades the rear faces.
        inset_panel(s, a, b, ridge + 0.02, 0.94, 'lavender' if k % 2 else 'panel', margin=0.5, lift=0.04)

# ---------------------------------------------------------------- central ridge block
# Flat-topped ridge between the +-15 degree ribs, peaking at z = -7 like the original's
# hump, with a steep aft slope down to the trailing edge.
C_RIDGE_F, C_RIDGE_UP = -7.0, 11.6
C_REAR_F, C_REAR_UP = -18.5, 1.9


def centre_top(f):
    if f >= C_RIDGE_F:
        return PIVOT_UP + 0.1 + (C_RIDGE_UP - PIVOT_UP - 0.1) * (PIVOT_F - 0.8 - f) / (PIVOT_F - 0.8 - C_RIDGE_F)
    return C_RIDGE_UP + (C_REAR_UP - C_RIDGE_UP) * (f - C_RIDGE_F) / (C_REAR_F - C_RIDGE_F)


def centre_ring(f, w):
    top = centre_top(f)
    return [(-w, BASE), (w, BASE), (w, top - 0.35), (w * 0.72, top), (-w * 0.72, top), (-w, top - 0.35)]


tan15 = math.tan(math.radians(CENTRE_ANGLE))
KIT.loft([(f, centre_ring(f, max((PIVOT_F - f) * tan15, 0.2))) for f in (26.2, 20.0, 10.0, 0.0, C_RIDGE_F)]
         + [(-12.0, centre_ring(-12.0, 11.0)), (C_REAR_F, centre_ring(C_REAR_F, 12.2))],
         'hull', bevel=0.14, name='centre')

# Bow slits: dark hatched triangle on the central ramp with radiating ribs, a light
# spine and two round ports, as in the texture and the colour art.
def ramp_band(x0, x1, f0, f1, lift, color, name):
    rings = []
    for f, (a, b) in ((f0, x0), (f1, x1)):
        up = centre_top(f) + lift
        rings.append([(a, up - 0.1, f), (b, up - 0.1, f), (b, up, f), (a, up, f)])
    ring_loft(rings, color, name=name)


SLIT_F0, SLIT_F1 = 24.5, -4.5
ramp_band((-0.3, 0.3), (-7.4, 7.4), SLIT_F0, SLIT_F1, 0.06, 'slit', 'slits')
for k in range(-5, 6):
    if k == 0:
        continue
    x1 = k * 1.3
    ramp_band((k * 0.06 - 0.05, k * 0.06 + 0.05), (x1 - 0.09, x1 + 0.09), SLIT_F0 - 1.0, SLIT_F1 + 0.3, 0.1, 'rib', 'slit-rib')
ramp_band((-0.18, 0.18), (-0.5, 0.5), SLIT_F0 + 1.0, C_RIDGE_F + 0.5, 0.13, 'panel', 'spine')
for x in (-4.4, 4.4):
    f = -5.9
    cylinder((x, centre_top(f) - 0.1, f), (x, centre_top(f) + 0.2, f), 0.75, 'panel', segments=20, bevel=0.04)
    cylinder((x, centre_top(f) + 0.18, f), (x, centre_top(f) + 0.24, f), 0.5, 'line', segments=20)
# Dark frame lines on the ridge top and the aft slope, and a sensor block on the ridge.
for f in (-2.0, -7.6, -12.5):
    w = min((PIVOT_F - f) * tan15, 11.0) * 0.7
    box((0, centre_top(f) + 0.03, f), (w * 2, 0.08, 0.28), 'line')
mirrored(lambda s: ramp_band((s * 3.8 - 0.12, s * 3.8 + 0.12), (s * 5.2 - 0.12, s * 5.2 + 0.12), 0.5, -16.5, 0.05,
                             'line', 'frame'))
box((0, C_RIDGE_UP + 0.35, -8.2), (3.2, 0.7, 2.2), 'panel', bevel=0.12)
box((0, C_RIDGE_UP + 0.55, -7.2), (2.6, 0.25, 0.1), 'slit')

# ---------------------------------------------------------------- underside
# Thin lower plate over the whole planform, then the keel hull hanging beneath it.
def planform(up, inset=0.0):
    pts = [(0.0, up, PIVOT_F - inset)]
    x, f = rib_point(TIP_ANGLE, te_distance(TIP_ANGLE) - inset, 1)
    pts.append((x, up, f))
    pts += [(10.0 - inset * 0.5, up, -18.5 + inset * 0.5), (0.0, up, -18.9 + inset * 0.5),
            (-10.0 + inset * 0.5, up, -18.5 + inset * 0.5)]
    x, f = rib_point(TIP_ANGLE, te_distance(TIP_ANGLE) - inset, -1)
    pts.append((x, up, f))
    return pts


def slab(top_points, depth, color, bevel=0.0, name='slab'):
    bm = bmesh.new()
    top = [bm.verts.new(P(*p)) for p in top_points]
    bottom = [bm.verts.new(P(p[0], p[1] - depth, p[2])) for p in top_points]
    bm.faces.new(top)
    bm.faces.new(list(reversed(bottom)))
    n = len(top)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return add_object(bm, color, bevel, name=name)


slab(planform(BASE + 0.05), 0.75, 'panel', bevel=0.1, name='underside')
# Grey stripes under the fan, parallel to the leading edges (bottom colour art).
for s in (-1, 1):
    for angle, t0, t1 in ((50.0, 20.0, 60.0), (40.0, 18.0, 52.0), (30.0, 16.0, 44.0)):
        x0, f0 = rib_point(angle, t0, s)
        x1, f1 = rib_point(angle, t1, s)
        ring_loft([[(x - 0.3, BASE - 0.72, f), (x + 0.3, BASE - 0.72, f), (x + 0.3, BASE - 0.85, f), (x - 0.3, BASE - 0.85, f)]
                   for x, f in ((x0, f0), (x1, f1))], 'lavender', name='stripe')

# Keel hull: (fwd, half width, keel depth). It runs past the trailing edge to the stern.
KEEL = [(18.0, 0.8, -1.9), (12.0, 3.2, -3.8), (2.0, 4.8, -5.4), (-8.0, 5.2, -6.2), (-14.0, 5.0, -6.3),
        (-19.0, 4.4, -5.6), (-23.0, 3.4, -4.0), (-25.6, 2.2, -2.6), (-26.4, 1.0, -1.8)]


def keel_ring(hw, keel, top=-0.2):
    h = top - keel
    return [(-hw * 0.9, top), (hw * 0.9, top), (hw, top - 0.3 * h), (hw * 0.75, top - 0.8 * h), (hw * 0.3, keel),
            (-hw * 0.3, keel), (-hw * 0.75, top - 0.8 * h), (-hw, top - 0.3 * h)]


KIT.loft([(f, keel_ring(hw, k, top=(-0.2 if f > -18.0 else 0.4))) for f, hw, k in KEEL], 'hull', bevel=0.12, name='keel')
KIT.loft([(f, [(-hw * 0.32, k - 0.05), (hw * 0.32, k - 0.05), (hw * 0.32, k + 0.25), (-hw * 0.32, k + 0.25)])
          for f, hw, k in KEEL[1:-2]], 'lavender', bevel=0.04, name='keel-strip')
# Aft deck on the stern where the keel runs out behind the fan.
slab([(-3.8, 0.45, -18.6), (3.8, 0.45, -18.6), (2.6, 0.45, -24.8), (-2.6, 0.45, -24.8)], 0.6, 'panel', bevel=0.08)
box((0, 0.5, -21.0), (4.2, 0.08, 0.16), 'line')

# Navy keel fin at the stern (the original's white ventral fin) and two canted side fins.
plate([(0.2, -5.0, -15.6), (0.2, -7.3, -24.4), (0.2, -7.3, -26.2), (0.2, -2.4, -26.0)], -0.4, 'keelfin', bevel=0.06)
mirrored(lambda s: plate([(s * 3.4, -3.8, -19.5), (s * 5.6, -5.4, -24.6), (s * 5.4, -5.3, -25.6), (s * 2.2, -2.6, -25.2)],
                         0.3, 'keelfin', bevel=0.05))
mirrored(lambda s: box((s * 1.1, -2.2, -25.9), (0.7, 0.5, 0.12), 'mark'))

# Main thrusters in the stern face of the keel.
for x, u in ((-1.2, -1.4), (1.2, -1.4), (0.0, -2.6)):
    cylinder((x, u, -25.4), (x, u, -26.9), 0.62, 'rib', segments=18, radius_b=0.7, cap=False)
    cylinder((x, u, -25.5), (x, u, -26.8), 0.5, 'nozzle', segments=18, cap=False)
    cylinder((x, u, -25.9), (x, u, -26.0), 0.5, 'glow', segments=18)

KIT.export(OUT, 'Peacemillion', previews=PREVIEWS, reference=5589)
