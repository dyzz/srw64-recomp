"""Author the HD La Vie en Rose (world-map dock ship, original resource 5592, part 0) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/la_vie_en_rose_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z toward the
dock mouth and docking arms, +Y up, units of the N64 mesh), model.glb and
optional previews with the original part 0 rendered from the same cameras
(compare.png). Part 1 of the resource, the ラビアンローズ name plate, stays
original and is not modelled here.

Design reference: Anaheim Electronics' La Vie en Rose-class self-propelled dock
ship from Mobile Suit Gundam 0083 (also in Z / ZZ). Official figures: length
618 m, mass 98,535 t (Gundam Wiki); named for its rose-petal outline; a large
dock at the front able to berth capital ships, eight factory blocks behind the
petals, a rear section with centrifugal gravity, docking arms round the dock
(Gundam Wiki, Gundam Channel). Shape and colours follow the Bandai EX Model
1/1700 kit photos and box art (licensed; yellow petal disc, red lens-shaped
body behind it, dark teal-grey hub and arms, small engine cluster with antennas
aft), the Gundam Channel Z colour art (sixteen petals, long thin outer arms
reaching out from behind the rim) and 0083 OVA frames (heavy docking arms
over the hub, red factory blocks between the petals and the red body). The
original is 54 units from the engine to the arm tips, so one unit is roughly
11 m; the outer arms reach 54 units from the axis as in the original.

Sources:
  https://gundam.wiki.cre.jp/wiki/%E3%83%A9%E3%83%93%E3%82%A2%E3%83%B3%E3%83%AD%E3%83%BC%E3%82%BA
  https://www.gundam-c.com/manual/mechanic/z/la-vie-en-rose.html
  Bandai EX Model 1/1700 La Vie en Rose (product photos and box art)
Reference images were only viewed for comparison; none are in the repository.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'petal': (234, 204, 84, 255),     # yellow petal disc
    'petal_line': (176, 140, 50, 255),
    'rose': (196, 54, 44, 255),       # red body and factory blocks
    'rose_dark': (138, 36, 34, 255),
    'arm': (70, 90, 96, 255),         # dark teal-grey hub and arms
    'arm_light': (122, 142, 146, 255),
    'engine': (150, 156, 160, 255),
})
box, cylinder, sphere, add_object = KIT.box, KIT.cylinder, KIT.sphere, KIT.add_object


def polar(r, a, fwd):
    return (r * math.cos(a), r * math.sin(a), fwd)


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


# ---------------------------------------------------------------- petal disc
# Sixteen petals in a shallow dish opening forward, outer ends rounded off.
PETALS = 16
# (radius, front face fwd, half-width factor)
STATIONS = [(16.5, 12.0, 1.0), (21.0, 12.6, 1.0), (26.0, 13.6, 1.0), (29.6, 14.8, 1.0),
            (31.0, 15.6, 0.86), (31.6, 15.9, 0.55)]
THICK = 1.3
for i in range(PETALS):
    mid = 2 * math.pi * i / PETALS
    half = math.pi / PETALS - math.radians(1.3)

    def section(r, fwd, k, lift=0.0, width=None):
        h = half * k if width is None else width / r
        return [polar(r, mid - h, fwd + lift), polar(r, mid + h, fwd + lift),
                polar(r, mid + h, fwd - THICK), polar(r, mid - h, fwd - THICK)]

    loft3([section(*s) for s in STATIONS], 'petal', bevel=0.12, segments=1, name='petal')
    # Centre groove and a cross seam in the darker ochre.
    loft3([section(r, f + 0.06, 1, lift=0.0, width=0.5) for r, f, _ in STATIONS[:4]], 'petal_line', name='groove')
    loft3([section(r, f + 0.06, 0.92) for r, f in ((23.4, 13.08), (23.8, 13.16))], 'petal_line', name='seam')

# Mounting plate behind the petals.
cylinder((0, 0, 8.6), (0, 0, 10.9), 18.2, 'rose_dark', segments=40, bevel=0.2)

# ---------------------------------------------------------------- dock hub
cylinder((0, 0, 6.0), (0, 0, 13.4), 8.6, 'arm', segments=40, bevel=0.25)
cylinder((0, 0, 13.3), (0, 0, 14.3), 8.1, 'arm_light', segments=40, bevel=0.15)
cylinder((0, 0, 14.2), (0, 0, 15.1), 6.2, 'arm', segments=40, bevel=0.12)
cylinder((0, 0, 15.0), (0, 0, 15.25), 5.1, 'arm_light', segments=40)
cylinder((0, 0, 15.1), (0, 0, 15.35), 3.9, 'dark', segments=32)
box((0, 0, 15.4), (2.2, 2.2, 0.1), 'nozzle')
# Eight red factory blocks round the hub, between it and the petals.
for k in range(8):
    a = 2 * math.pi * k / 8 + math.pi / 8
    cylinder(polar(12.6, a, 7.5), polar(12.6, a, 12.9), 3.0, 'rose', segments=18, bevel=0.3)
    cylinder(polar(12.6, a, 12.8), polar(12.6, a, 13.2), 2.1, 'rose_dark', segments=14)
    # Short spoke from the hub rim out to the petal root.
    box(polar(12.6, a + math.pi / 8, 10.2), (1.4, 1.4, 1.4), 'arm')

# ---------------------------------------------------------------- red body
# Lens-shaped main body behind the petals with a darker rim band.
sphere((0, 0, 3.4), 1.0, 'rose', scale=(22.5, 22.5, 7.0), segments=40)
cylinder((0, 0, 2.8), (0, 0, 4.0), 22.7, 'rose_dark', segments=48, bevel=0.2)
for k in range(12):
    a = 2 * math.pi * k / 12
    box(polar(21.2, a, 0.4), (1.2, 1.2, 0.8), 'rose_dark')

# ---------------------------------------------------------------- engine section
# Red tank cluster, grey engine block, four nozzles and splayed antennas.
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    sphere(polar(3.4, a, -4.6), 2.8, 'rose', segments=14)
cylinder((0, 0, -3.6), (0, 0, -13.6), 4.6, 'engine', segments=32, bevel=0.2)
for f in (-7.2, -11.2):
    cylinder((0, 0, f + 0.5), (0, 0, f - 0.5), 5.05, 'arm_light', segments=32, bevel=0.08)
for k in range(4):
    a = k * math.pi / 2
    cylinder(polar(2.3, a, -13.4), polar(2.3, a, -16.9), 1.15, 'engine', segments=18, radius_b=1.55, cap=False)
    cylinder(polar(2.3, a, -13.5), polar(2.3, a, -16.8), 0.95, 'nozzle', segments=18, radius_b=1.35, cap=False)
    cylinder(polar(2.3, a, -14.0), polar(2.3, a, -14.1), 1.0, 'glow', segments=18)
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    cylinder(polar(4.5, a, -9.0), polar(11.5, a, -16.5), 0.18, 'arm_light', segments=6)
    sphere(polar(11.5, a, -16.5), 0.35, 'arm_light', segments=8)


# ---------------------------------------------------------------- arms
def truss(a, b, radius, color, rod_offset, braces=3):
    """Main beam with a thin parallel rod and cross braces, read as a lattice arm."""
    cylinder(a, b, radius, color, segments=6)
    oa = tuple(p + o for p, o in zip(a, rod_offset))
    ob = tuple(p + o for p, o in zip(b, rod_offset))
    cylinder(oa, ob, radius * 0.4, color, segments=6)
    for t in [(k + 0.5) / braces for k in range(braces)]:
        p = tuple(pa + (pb - pa) * t for pa, pb in zip(a, b))
        q = tuple(pa + (pb - pa) * (t + 0.5 / braces) for pa, pb in zip(oa, ob))
        cylinder(p, q, radius * 0.3, color, segments=4)


# Six long outer arms from behind the rim, bending forward (as the original's six).
for deg in (90, 30, -30, -90, -150, 150):
    a = math.radians(deg)
    root, knee, elbow, tip = polar(21.0, a, 8.6), polar(34.0, a, 13.2), polar(43.0, a, 20.0), polar(53.2, a, 28.4)
    side = (-math.sin(a) * 0.9, math.cos(a) * 0.9, 0.0)
    truss(root, knee, 0.8, 'arm', side, braces=2)
    truss(knee, elbow, 0.75, 'arm', side, braces=2)
    truss(elbow, tip, 0.65, 'arm', side, braces=2)
    for p in (knee, elbow):
        sphere(p, 0.95, 'arm', segments=8)
    # Claw: two fingers bent forward at the tip.
    for s in (-1, 1):
        finger = (tip[0] + side[0] * s * 1.2 - math.cos(a) * 1.2, tip[1] + side[1] * s * 1.2 - math.sin(a) * 1.2, tip[2] + 2.6)
        cylinder(tip, finger, 0.3, 'arm', segments=6)

# Eight heavy docking arms round the dock, reaching forward with claws.
for k in range(8):
    a = 2 * math.pi * k / 8
    root, elbow, wrist = polar(8.6, a, 12.5), polar(15.2, a, 23.0), polar(12.8, a, 32.2)
    side = (-math.sin(a) * 1.0, math.cos(a) * 1.0, 0.0)
    box(root, (2.4, 2.4, 2.4), 'arm_light', bevel=0.15, segments=1)
    truss(root, elbow, 0.95, 'arm', side, braces=3)
    truss(elbow, wrist, 0.85, 'arm', side, braces=2)
    sphere(elbow, 1.25, 'arm_light', segments=10)
    sphere(wrist, 1.0, 'arm', segments=8)
    for dr, df in ((-2.2, 4.2), (1.8, 3.8)):
        finger = polar(12.8 + dr, a, 32.2 + df)
        cylinder(wrist, finger, 0.42, 'arm', segments=6)

KIT.export(OUT, 'La Vie en Rose', previews=PREVIEWS, reference=5592, reference_parts=[0])
