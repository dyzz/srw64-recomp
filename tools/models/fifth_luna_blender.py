"""Author the HD Fifth Luna (story world-map landmark, original resource 5607 part 0) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/fifth_luna_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json, model.glb and optional previews with the original rendered from the
same cameras (compare.png). Replaces part 0 only; part 1 is the camera-facing name plate
「フィフス・ルナ」 (read from its textures, confirmed in scene 103) and stays original.

Frame. The original part is an 88-triangle textured rock, x -7..7 (its thin axis),
y -16..17 (long axis), z -14..14: a narrow ridge toward +z and a broad stepped face
toward -z. The resource's node table places it at (0, -100, 0) turned (0, -90, 35)
degrees, so the world-map camera sees one of the broad +-x faces with the long axis
leaning; the HD rock keeps that frame, origin and extent and details both broad faces.

Design reference: the resource-mining asteroid Fifth Luna (5th ルナ) from Char's
Counterattack, captured by Neo Zeon and dropped on Lhasa. Setting notes used: an
asteroid brought into Earth orbit for mining and later a Federation base, able to move
under its own nuclear pulse engines (Gundam Wiki 「フィフス・ルナ」 and "Fifth Luna";
GG.Lab 「フィフス・ルナ」). Shape and details follow the film's concept sketch shown on
the Gundam Wiki (Fandom) page: a lumpy wedge of rock with several bowl-shaped pulse
engines set along one edge (one noted as 100 m across), masts, a crater, and the old
railway and port works (「かつての鉄道及び港口等」). Three engines sit on the -z face
where the original model has its three stepped blocks. Colours: brown-grey rock with
grey installations. Reference pictures were only viewed for comparison; nothing was
copied into the repository.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit({
    'rock': (116, 110, 100, 255), 'rocklight': (144, 138, 126, 255), 'rockdark': (88, 83, 76, 255),
    'rockshadow': (66, 63, 58, 255), 'metal': (148, 150, 152, 255), 'bell': (58, 58, 62, 255),
    'frame': (186, 182, 170, 255), 'light': (240, 214, 150, 60),
})
box, cylinder, add_object = KIT.box, KIT.cylinder, KIT.add_object
TAU = math.tau


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


def add_split(bm, shade, name, smooth=True):
    """Split a mesh into colour groups chosen per face by shade(centre, normal). With
    smooth, every group keeps the whole mesh's vertex normals so colour borders do not
    show as creases."""
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
        if smooth:
            obj.data.normals_split_custom_set_from_vertices(normals)
    bm.free()


# ---------------------------------------------------------------- the asteroid
RX, RY, RZ, RZ_AFT = 6.6, 16.2, 13.6, 11.2
NU, NV = 96, 64           # around the long (y) axis, pole to pole
CRATERS = [((6.0, -6.5, 3.5), 3.6, 1.1), ((-5.8, 7.0, -1.0), 2.6, 0.8), ((5.2, 9.5, -4.5), 1.8, 0.5),
           ((-5.0, -9.0, 4.0), 2.0, 0.6)]
ENGINES = [(10.0, -0.4), (0.8, -0.6), (-8.6, -0.3)]   # (y, x) on the -z face
ENGINE_R = 3.1


def body_point(u, v):
    """Surface point for angle u around the y axis and v from the lower (0) to the upper
    (1) end: a wedge-shaped potato, broad toward -z, a ridge toward +z."""
    th = math.pi * v
    dy = -math.cos(th)
    dx, dz = math.sin(th) * math.cos(TAU * u), math.sin(th) * math.sin(TAU * u)
    sx = math.copysign(abs(dx) ** 0.8, dx)
    sy = math.copysign(abs(dy) ** 0.9, dy)
    sz = math.copysign(abs(dz) ** (0.55 if dz < 0 else 0.95), dz)   # flat stern face
    z = (RZ if sz > 0 else RZ_AFT) * sz
    zn = sz
    w = 0.92 - 0.30 * zn - 0.27 * zn * zn          # thickness: widest just aft of centre
    y = RY * sy * (1 - 0.10 * zn)
    x = RX * sx * w * (1 - 0.25 * sy * sy)
    z += 1.6 * math.sin(0.19 * y + 0.6) - 0.8     # a slight bend along the long axis
    p = Vector((x, y, z))
    # Lumps and crags along the radial direction, craters on the broad faces.
    radial = Vector((x / RX, y / RY, z / RZ)).normalized()
    d = field(p, 0.22, 1.25, octaves=3, shift=0.2) + 0.55 * field(p, 0.55, 1.0, octaves=2, shift=0.7, ridged=True)
    for c, r, depth in CRATERS:
        q = (p - Vector(c)).length / r
        if q < 1.6:
            d += -depth * max(0.0, 1 - q * q) + 0.45 * depth * math.exp(-((q - 1.05) / 0.22) ** 2)
    # Seats for the engines: the stern face is hollowed a little around each bowl.
    for ey, ex in ENGINES:
        q = math.hypot(y - ey, x - ex) / (ENGINE_R * 1.25)
        if z < -6 and q < 1.4:
            d -= 0.9 * max(0.0, 1 - q * q)
    return p + Vector((radial.x * RX, radial.y * RY, radial.z * RZ)).normalized() * d * 0.8


def asteroid():
    rings = [[tuple(body_point(i / NU, k / NV)) for i in range(NU)] for k in range(1, NV)]
    bottom, top = tuple(body_point(0, 0)), tuple(body_point(0, 1))
    return rings, bottom, top


RINGS, BOTTOM, TOP = asteroid()


def rock_shade(c, n):
    """Lighter on the upper faces, darker below and in hollows, mottled."""
    v = 0.75 * field(c, 0.45, 1.0, octaves=2, shift=1.1) + 0.6 * n[1]
    if v < -0.75:
        return 'rockshadow'
    return 'rocklight' if v > 0.55 else 'rockdark' if v < -0.3 else 'rock'


add_split(solid(RINGS, bottom=BOTTOM, top=TOP), rock_shade, 'asteroid')


def surface(y, z, side):
    """The rock surface point on the +x (side 1) or -x (side -1) face nearest (y, z),
    with an outward direction."""
    best = max((p for ring in RINGS for p in ring),
               key=lambda p: side * p[0] - 3.0 * math.hypot(p[1] - y, p[2] - z))
    return Vector(best), Vector((side, 0.0, 0.0))


# ---------------------------------------------------------------- nuclear pulse engines
def bowl(center, radius, depth, n=32, rows=8):
    """A thick pulse-engine bowl opening toward -z: dark inside, metal lip and back."""
    cx, cy, cz = center
    profile = []                                           # (radius, fwd offset), inside then outside
    for k in range(rows + 1):                              # inner surface, bottom to lip
        a = (math.pi / 2) * k / rows
        profile.append((radius * math.sin(a), depth * math.cos(a)))
    profile.append((radius * 1.12, -0.25))                 # lip
    profile.append((radius * 1.18, 0.35))
    for k in range(rows, -1, -1):                          # outer surface, lip to back
        a = (math.pi / 2) * k / rows
        profile.append((radius * 1.1 * math.sin(a) + 0.01, 0.5 + (depth + 0.6) * math.cos(a)))
    rings = []
    for r, f in profile[1:-1]:
        rings.append([(cx + r * math.cos(TAU * i / n), cy + r * math.sin(TAU * i / n), cz + f) for i in range(n)])
    bm = solid(rings, bottom=(cx, cy, cz + profile[0][1]), top=(cx, cy, cz + profile[-1][1]))

    def shade(c, nrm):
        inner = math.hypot(c[0] - cx, c[1] - cy) < radius * 0.999 and c[2] - cz > -0.1 and nrm[2] < 0.3
        return 'bell' if inner else 'metal'
    add_split(bm, shade, 'bowl', smooth=False)
    # Cage over the bowl: meridian bars from the lip to the bottom.
    for m in range(4):
        a = math.pi * m / 4
        pts = []
        for k in range(-6, 7):
            t = (math.pi / 2) * k / 6
            r = radius * 0.97 * math.sin(t)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a), cz + depth * 0.97 * math.cos(t) - 0.05))
        for p, q in zip(pts, pts[1:]):
            cylinder(p, q, 0.09, 'frame', segments=5)


for ey, ex in ENGINES:
    # Each bowl stands on the stern face, its back set into the rock.
    seat = sorted(p[2] for ring in RINGS for p in ring
                  if math.hypot(p[1] - ey, p[0] - ex) < ENGINE_R and p[2] < 0)
    cz = seat[0] - 2.2
    bowl((ex, ey, cz), ENGINE_R, 2.6)
    cylinder((ex, ey, cz + 3.4), (ex, ey, seat[len(seat) // 2] + 1.5), 1.3, 'metal', segments=16)
    # Mount: a pylon from the back of the bowl and struts from the lip into the rock.
    for i in range(6):
        a = TAU * (i + 0.5) / 6
        p = (ex + ENGINE_R * 1.15 * math.cos(a), ey + ENGINE_R * 1.15 * math.sin(a), cz + 0.3)
        cylinder(p, (ex + ENGINE_R * 1.3 * math.cos(a), ey + ENGINE_R * 1.3 * math.sin(a), cz + 3.6), 0.16, 'frame', segments=5)


# ---------------------------------------------------------------- port and railway
# The old mining port on the +x face near the ridge, with the railway running aft.
port, out = surface(-1.0, 7.5, 1)
box(tuple(port - out * 0.4), (2.2, 3.4, 4.0), 'metal', bevel=0.2)
box(tuple(port + out * 0.9 + Vector((0, 0.6, 0))), (0.9, 1.4, 2.6), 'metal', bevel=0.1)
box(tuple(port + out * 1.0 + Vector((0, -1.2, 0.6))), (0.7, 0.4, 1.2), 'light')
for i in range(14):
    z = 5.0 - i * 1.15
    y = -1.5 - 0.35 * i + 0.02 * i * i
    p, out = surface(y, z, 1)
    box(tuple(p + out * 0.05), (0.35, 0.28, 1.25), 'frame')
for y, z in ((4.0, 2.0), (-10.0, 1.0), (8.0, -6.0)):
    p, out = surface(y, z, 1)
    box(tuple(p - out * 0.2), (1.8, 1.6, 2.2), 'metal', bevel=0.15)
    box(tuple(p + out * 0.75), (0.5, 0.3, 0.5), 'light')
# Works on the -x face.
for y, z in ((-4.0, 4.0), (6.0, 3.0), (-12.0, -3.0)):
    p, out = surface(y, z, -1)
    box(tuple(p - out * 0.2), (1.8, 1.4, 2.4), 'metal', bevel=0.15)
    box(tuple(p + out * 0.75), (0.5, 0.3, 0.5), 'light')

# ---------------------------------------------------------------- masts
for y, z, side, lean in ((12.0, 6.0, 1, 0.3), (-13.0, 8.0, 1, -0.2), (3.0, 11.0, -1, 0.2), (-6.0, -8.0, -1, -0.3),
                         (14.0, -4.0, 1, 0.5)):
    p, out = surface(y, z, side)
    tip = p + (out + Vector((0, lean, 0.25))).normalized() * 3.6
    cylinder(tuple(p - out * 0.3), tuple(tip), 0.14, 'metal', segments=6)
    box(tuple(tip), (0.35, 0.35, 0.35), 'light')

KIT.export(OUT, 'Fifth Luna', previews=PREVIEWS, reference=5607, reference_parts=[0])
