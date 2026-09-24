"""Author the HD Axis (story world-map landmark, original resource 5598 part 1) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/axis_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json, model.glb and optional previews with the original rendered from the
same cameras (compare.png). Replaces part 1 only; part 0 is the name plate 「アクシズ」
(read from its textures) and stays original.

Frame. Resource 5598 is two node-type-5 parts: the plate and a flat 130 x 130 picture of
Axis (x -65..65, y -65..65, z 0). The model draw (8008A914) builds type-5 nodes as
guScale x guAlign(0, view direction): the part is turned to face the camera every frame,
local +z toward the viewer, +x screen right, +y screen up. The HD mesh is drawn with
the same matrix, so it is always seen along its -z axis like the picture it replaces
(the compare 'front' view is the in-game view; the others only show the solid). To
show the top of the asteroid the way the picture does, the solid is built upright and
then tipped toward the viewer (TILT) and rolled (ROLL) to match the picture's pose:
peak left of centre, Moussa on the right on its frame, two cones hanging below.

Design reference: asteroid base Axis (Mobile Suit Zeta/ZZ Gundam, Char's
Counterattack). Features taken from the setting descriptions: a wide flat central
body, roughly circular from above, with one conical spire rising from the top and two
hanging below (ja.wikipedia 「アクシズ」; Gundam Wiki "Axis"); the asteroid Moussa, the
residential block, joined to the top by many iron-frame supports; four large nuclear
pulse engines on a flat cut face at one end of the body, surrounded by a mesh of
struts (Gundam Wiki "Axis", including its lineart of the engine face). Proportions
and colours (khaki grey rock, pale frames) follow the original 5598 picture and
anime stills of Axis in Char's Counterattack. Reference pictures were only viewed
for comparison; nothing was copied into the repository.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit({
    'rock': (118, 112, 90, 255), 'rocklight': (146, 139, 110, 255), 'rockdark': (92, 88, 70, 255),
    'moussa': (128, 119, 98, 255), 'moussalight': (152, 142, 118, 255), 'frame': (200, 192, 160, 255),
    'rockshadow': (70, 67, 54, 255), 'metal': (140, 142, 138, 255), 'bell': (60, 58, 56, 255), 'light': (236, 220, 150, 60),
})
box, cylinder, add_object = KIT.box, KIT.cylinder, KIT.add_object
TAU = math.tau
TILT, ROLL = 24.0, 10.0   # degrees: top toward the viewer, then right end up


# ---------------------------------------------------------------- deterministic rock noise
# Sums of sines along fixed directions with fixed phases: the same mesh on every run.
DIRS = [Vector(d).normalized() for d in ((1, .3, .2), (-.4, 1, .5), (.2, -.6, 1), (.8, .8, -.3), (-1, .2, .7),
                                          (.5, -1, -.4), (-.3, -.2, -1), (.9, -.5, .6), (-.7, .9, -.6))]


def field(p, f0, a0, octaves=3, shift=0.0, ridged=False):
    """Band-limited lumps around a point p (original coordinates)."""
    total = 0.0
    for o in range(octaves):
        f, a = f0 * 2.07 ** o, a0 * 0.5 ** o
        for k, d in enumerate(DIRS):
            s = math.sin(f * (d.x * p[0] + d.y * p[1] + d.z * p[2]) + ((k * 0.618034 + o * 0.414214 + shift) % 1.0) * TAU)
            total += a * ((1 - abs(s)) * 2 - 1 if ridged else s) / 3.0
    return total


def solid(rings, bottom=None, top=None):
    """Closed bmesh from rings of original-coordinate points (equal counts, same turning
    sense); bottom/top are apex points closing the ends with fans, else flat caps."""
    bm = bmesh.new()
    vr = [[bm.verts.new(P(*p)) for p in ring] for ring in rings]
    n = len(vr[0])
    for a, b in zip(vr, vr[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    for ring, apex in ((vr[0], bottom), (vr[-1], top)):
        if apex is None:
            bm.faces.new(ring)
        else:
            c = bm.verts.new(P(*apex))
            for i in range(n):
                bm.faces.new((ring[i], ring[(i + 1) % n], c))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def orig(v):
    """Blender vector -> original (x, up, fwd)."""
    return (v.x, v.z, -v.y)


def add_rock(bm, shade, name):
    """Split a rock mesh into colour groups chosen per face by shade(centre, normal).
    Every group keeps the whole rock's smooth vertex normals, so the colour borders do
    not show as creases."""
    bm.normal_update()
    groups = {}
    for f in bm.faces:
        groups.setdefault(shade(orig(f.calc_center_median()), orig(f.normal)), []).append(f)
    for color, faces in sorted(groups.items()):
        sub, vmap, normals = bmesh.new(), {}, []
        for f in faces:
            loop = []
            for v in f.verts:
                if v not in vmap:
                    vmap[v] = sub.verts.new(v.co)
                    normals.append(v.normal.copy())
                loop.append(vmap[v])
            sub.faces.new(loop)
        obj = add_object(sub, color, name=f'{name}-{color}')
        obj.data.normals_split_custom_set_from_vertices(normals)
    bm.free()


def rock_shade(light='rocklight', base='rock', dark='rockdark', shift=0.0):
    """Lighter on sun-facing (upward) rock, darkest under the body, mottled."""
    def shade(c, n):
        v = 0.7 * field(c, 0.22, 1.0, octaves=2, shift=shift) + 0.8 * n[1]
        if n[1] < -0.35 and v < -0.2:
            return 'rockshadow'
        return light if v > 0.5 else dark if v < -0.45 else base
    return shade


# ---------------------------------------------------------------- central body
# Leaf-shaped plan with pointed ends on +-x, thin lens section: a few units of rock
# above the rim line, more below where the hanging cones start.
L, L_EAST, W = 62.0, 57.0, 36.0
CUT, CUT_Z = (-43.0, 1.0), 21.0
NPHI, NTOP, NBOT = 120, 14, 12


def rim(phi):
    s, c = math.sin(phi), math.cos(phi)
    x, z = (L if c < 0 else L_EAST) * c, W * s * (1 - 0.55 * c * c)
    wobble = 1 + 0.07 * math.sin(3 * phi + 0.7) + 0.05 * math.sin(5 * phi + 2.1) + 0.035 * math.sin(11 * phi + 0.4)
    x, z = x * wobble, z * wobble
    if z < 0:   # the flat engine face chopped into the rear-left edge, easing out at its ends
        z = max(z, -CUT_Z - 1.6 * max(0.0, x - CUT[1], CUT[0] - x))
    return x, z


def body():
    def outline(t):
        return [(x * t, z * t) for x, z in (rim(TAU * i / NPHI) for i in range(NPHI))]

    rings = []
    for k in range(1, NTOP):                  # top surface, centre outward
        t = k / NTOP
        ring = []
        for x, z in outline(t):
            h = 4.2 * (1 - t * t) ** 0.8 + (1 - t) * field((x, 0, z), 0.16, 1.6, shift=0.1) + 0.6 * (1 - t ** 4)
            ring.append((x, h + 0.4 * field((x, 3, z), 0.6, 0.5, octaves=2, ridged=True), z))
        rings.append(ring)
    # Rim: a thin crumbled edge joining the two surfaces.
    rings.append([(x, -0.3 + 0.5 * field((x, 0, z), 0.4, 1.0, octaves=2), z) for x, z in outline(1.0)])
    for k in range(NBOT - 1, 0, -1):          # bottom surface, rim inward
        t = k / NBOT
        rings.append([(x, -0.8 - 12.0 * (1 - t) ** 1.4 + (1 - t) * field((x, -5, z), 0.18, 1.8, shift=0.4), z)
                      for x, z in outline(t)])
    bm = solid(rings, bottom=(0, 5.4, 0), top=(0, -12.8, 0))
    add_rock(bm, rock_shade(), 'body')


body()


# ---------------------------------------------------------------- spires
def spire(cx, cz, base, height, lean, down=False, bow=1.3, y0=0.0, n=48, rows=24, shift=0.0):
    """Rock cone out of the body: the peak, or (down) one of the hanging spires. base is
    the (x, fwd) radii where it leaves the body at height y0 (inside the body's lens);
    bow > 1 hollows the flanks; lean moves the tip off the base centre."""
    sign = -1 if down else 1
    rings = []
    for k in range(rows):
        u = k / rows                          # 0 base .. 1 tip
        s = (1 - u) ** bow
        y = y0 + (sign * height - y0) * u
        ox, oz = lean[0] * u, lean[1] * u
        ring = []
        for i in range(n):
            a = TAU * i / n
            flutes = 0.12 * math.sin(5 * a + 4 * u + shift) + 0.08 * math.sin(9 * a + 1.3 - 2 * u + shift)
            px, pz = cx + base[0] * s * math.cos(a), cz + base[1] * s * math.sin(a)
            lump = 1 + flutes * (0.3 + u) + 0.12 * field((px, y, pz), 0.2, 1.0, octaves=2, shift=shift)
            ring.append((cx + ox + (px - cx) * lump, y, cz + oz + (pz - cz) * lump))
        rings.append(ring)
    return solid(rings, bottom=None, top=(cx + lean[0], sign * height, cz + lean[1]))


# The peak: a broad ridge left of centre, its tip set toward Moussa so the right flank
# is the steeper one, as in the picture.
add_rock(spire(-7.0, -6.0, (26.0, 16.0), 43.0, (8.0, -1.0), bow=1.2, y0=0.2, shift=0.3), rock_shade(shift=0.2), 'peak')
# Two hanging spires under the body, in the body's shadow.
UNDER = rock_shade('rock', 'rockdark', 'rockshadow', shift=0.5)
add_rock(spire(-23.0, 8.0, (23.0, 16.0), 37.0, (0.0, 1.0), down=True, bow=1.0, y0=-0.5, shift=1.1), UNDER, 'cone-a')
add_rock(spire(1.0, 3.0, (27.0, 21.0), 50.0, (-1.0, -3.0), down=True, bow=1.05, y0=-0.5, shift=2.3), UNDER, 'cone-b')


# ---------------------------------------------------------------- Moussa and its frame
MX, MY, MZ = 38.0, 23.5, -8.0
MR = (17.0, 12.0, 15.0)


def moussa():
    n, rows = 48, 26
    rings = []
    for k in range(1, rows):
        v = k / rows
        th = math.pi * v
        ring = []
        for i in range(n):
            a = TAU * i / n
            d = Vector((math.sin(th) * math.cos(a), -math.cos(th), math.sin(th) * math.sin(a)))
            lump = 1 + 0.06 * field(tuple(d * 10), 0.35, 1.0, octaves=3, shift=1.7)
            ring.append((MX + d.x * MR[0] * lump, MY + d.y * MR[1] * lump, MZ + d.z * MR[2] * lump))
        rings.append(ring)
    bm = solid(rings, bottom=(MX, MY - MR[1] * 0.98, MZ), top=(MX, MY + MR[1] * 1.02, MZ))
    add_rock(bm, rock_shade('moussalight', 'moussa', 'rockdark', shift=1.3), 'moussa')


moussa()
# Iron-frame supports: a ring of columns from the body to Moussa's underside, and
# raking struts fanning out toward the peak.
for i in range(18):
    a = TAU * i / 18 + 0.2
    x, z = MX + 9.0 * math.cos(a), MZ + 8.0 * math.sin(a)
    cylinder((x, 2.0, z), (x * 0.96 + MX * 0.04, MY - 10.0, z), 0.45, 'frame', segments=8)
ring = [(MX + 9.3 * math.cos(TAU * k / 24), 3.2, MZ + 8.3 * math.sin(TAU * k / 24)) for k in range(25)]
for p, q in zip(ring, ring[1:]):
    cylinder(p, q, 0.5, 'metal', segments=6)
for i in range(7):
    t = i / 6
    z = MZ + (t - 0.5) * 14
    cylinder((MX - 9.0, MY - 8.0 + 3 * t, z), (MX - 22.0 - 3 * math.sin(t * 3), 3.0 + 2 * t, z * 1.2), 0.4, 'frame', segments=6)
    cylinder((MX + 8.0, MY - 9.0, z), (MX + 16.0, 1.5, z * 1.1), 0.35, 'frame', segments=6)
# Docks and gun batteries on Moussa.
for a, b in ((0.3, 0.9), (1.9, 0.7), (3.4, 1.1), (4.6, 0.8)):
    d = Vector((math.cos(a) * math.sin(b), math.cos(b), math.sin(a) * math.sin(b)))
    c = (MX + d.x * MR[0] * 1.02, MY + d.y * MR[1] * 1.02, MZ + d.z * MR[2] * 1.02)
    box(c, (2.4, 1.2, 2.4), 'metal', bevel=0.2)

# ---------------------------------------------------------------- engine face
# The chopped-open end of the body: a flat face on the rear-left with four nuclear
# pulse engines in a row (one set lower than the rest), framed by a mesh of struts.
# It faces away from the world-map viewer (-z), so in game it only shapes the rim.
EZ = -CUT_Z        # face plane (fwd)
ENG = [(-34.0, -2.8), (-25.0, -2.5), (-16.0, -2.7), (-7.5, -4.2)]


def engine_face():
    inner = [(-41.0, -6.5), (-2.0, -7.5), (0.0, 0.6), (-40.0, 0.8)]
    outer = [(-39.0, -7.0), (-2.5, -8.2), (-2.0, 0.8), (-38.0, 1.0)]
    KIT.loft([(EZ, outer), (-4.0, inner)], 'rockdark', bevel=0.6, name='engine-block')
    box((-20.5, -3.3, EZ - 0.2), (37.0, 8.8, 0.4), 'metal', bevel=0.15)
    for x, y in ENG:
        cylinder((x, y, EZ), (x, y, EZ - 1.4), 3.9, 'metal', segments=24, bevel=0.1)
        cylinder((x, y, EZ - 1.3), (x, y, EZ - 4.0), 2.9, 'bell', segments=24, radius_b=3.7, cap=False)
        cylinder((x, y, EZ - 1.5), (x, y, EZ - 1.7), 2.4, 'bell', segments=24)
        cylinder((x, y, EZ - 3.7), (x, y, EZ - 4.2), 3.85, 'metal', segments=24, radius_b=3.9, cap=False)
    for i in range(9):
        x = -40.0 + i * 4.6
        cylinder((x, 1.5, EZ + 1.0), (x + 2.3, -9.0, EZ - 2.6), 0.2, 'frame', segments=6)
        cylinder((x + 2.3, 1.5, EZ - 2.6), (x, -9.0, EZ + 1.0), 0.2, 'frame', segments=6)


engine_face()

# ---------------------------------------------------------------- surface works
# Ports, blocks and lights on the upper surface (the pale specks in the picture).
WORKS = [(-44.0, 12.0), (-40.0, -10.0), (-30.0, -18.0), (22.0, 14.0), (17.0, -16.0), (48.0, 4.0), (52.0, -6.0),
         (28.0, 8.0), (14.0, 2.0), (-50.0, 2.0), (38.0, -18.0), (24.0, -22.0)]
for i, (x, z) in enumerate(WORKS):
    t = math.hypot(x / L, z / W)
    y = 4.2 * max(0.0, 1 - t * t) ** 0.8 + 0.3
    box((x, y + 0.5, z), (3.0 + (i % 3), 1.6 + 0.4 * (i % 2), 2.2 + (i % 4) * 0.5), 'metal', bevel=0.15)
    box((x, y + 1.4 + 0.2 * (i % 2), z), (0.6, 0.3, 0.6), 'light')

# ---------------------------------------------------------------- pose for the billboard
POSE = Matrix.Rotation(math.radians(-ROLL), 4, 'Y') @ Matrix.Rotation(math.radians(TILT), 4, 'X')
for obj, _ in KIT.parts:
    obj.matrix_world = POSE @ obj.matrix_world

KIT.export(OUT, 'Axis', previews=PREVIEWS, reference=5598, reference_parts=[1])
