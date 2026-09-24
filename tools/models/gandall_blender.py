"""Author the HD Gandall (world-map ship, original resource 5595) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/gandall_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 87 units = 1,400 m), model.glb and optional previews with
the original rendered from the same cameras (compare.png).

Identity: resource 5595 is model-table index 11, paired with unit 222 ガンドール
(docs/native/native-ship-model.md). In the original data, base:units:0222 carries
the carrier ability and the weapons 二連装大型ビーム砲, 大型ビーム砲, ガンドール砲 and
ガンドール砲 MAP, and every stage deployment of unit 222 is piloted by actor 253
葉月博士 (Dr. Kotaro Hazuki): the base battleship Gandall of Super Beast Machine
God Dancouga (1985), commanded by Hazuki, first playable in Super Robot Wars 64.

Design reference: Gandall, the Beast Fighter team's base battleship, which
transforms from a carrier form into a mechanical dragon; the Gandall cannon fires
from the dragon's mouth. Super Robot Wars games, SRW 64 included, only ever show
the dragon form, and the original 330-triangle model is that form: head and horns
toward +z, long neck, armoured body with four legs, a wing beam carried above the
back on pylons, and a long tail toward -z. Official figures used: length 1,400 m,
height 600 m; one twin large beam cannon, sixteen large beam cannons, the Gandall
cannon in the mouth; three plasma fusion reactors. Sources: srw.wiki.cre.jp
"ガンドール", dic.pixiv.net "ガンドール(ダンクーガ)", ja.wikipedia "超獣機神ダンクーガ";
proportions of the dragon form (spiked head with swept horns, segmented neck and
tail, straight wing beams on a dorsal pylon, clawed feet) from Bandai's licensed
SMP Alternative Destiny prototype shown at Wonder Festival 2024 Summer
(hobby.watch.impress.co.jp, dengekihobby); colours (steel blue-grey armour with
darker gunmetal, red eyes) from TV cels and the SRW IMPACT sprite.
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
    'hull': (122, 138, 150, 255), 'panel': (172, 184, 194, 255), 'dark': (58, 66, 78, 255),
    'teal': (66, 100, 114, 255), 'claw': (196, 200, 206, 255), 'eye': (255, 60, 40, 60),
    'mouth': (255, 196, 120, 40), 'glow': (150, 205, 255, 40),
})
box, cylinder, sphere, plate, mirrored, add_object = (KIT.box, KIT.cylinder, KIT.sphere, KIT.plate, KIT.mirrored,
                                                      KIT.add_object)


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


def zloft(sections, color, n=20, p=2.6, name='loft'):
    """Loft along z: sections (z, centre x, half width, bottom, top)."""
    return skin([[(x, y, z) for x, y in ring(cx, hw, b, t, n=n, p=p)] for z, cx, hw, b, t in sections], color, name=name)


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


def bezier(a, b, c, steps):
    a, b, c = Vector(a), Vector(b), Vector(c)
    return [tuple((1 - t) ** 2 * a + 2 * (1 - t) * t * b + t ** 2 * c) for t in (i / steps for i in range(steps + 1))]


# ---------------------------------------------------------------- body
BODY = [(-17.0, 0, 4.2, 0.5, 7.2), (-14.0, 0, 8.0, -2.4, 10.2), (-9.0, 0, 9.8, -3.4, 11.8), (-2.0, 0, 9.9, -3.3, 11.4),
        (5.0, 0, 9.0, -2.6, 10.0), (10.5, 0, 7.2, -1.4, 8.8), (14.5, 0, 4.8, 0.4, 7.8)]
zloft(BODY, 'hull', n=28, p=3.0, name='body')
# Overlapping dorsal armour plates with a spine of short fins.
def body_top(z):
    for (z0, _, _, _, t0), (z1, _, _, _, t1) in zip(BODY, BODY[1:]):
        if z0 <= z <= z1:
            return t0 + (t1 - t0) * (z - z0) / (z1 - z0)
    return BODY[-1][4]


for i, z in enumerate((-13.0, -8.5, -4.0, 0.5, 5.0, 9.5)):
    hw = 6.6 - abs(z + 3.0) * 0.12
    top = max(body_top(z - 2.4), body_top(z + 2.2)) + 0.75
    zloft([(z - 2.4, 0, hw * 0.92, top - 1.6, top - 0.2), (z + 1.6, 0, hw, top - 1.4, top + 0.25),
           (z + 2.2, 0, hw * 0.9, top - 1.6, top - 0.1)], 'panel' if i % 2 == 0 else 'teal', n=18, p=3.4, name='plate')
    plate([(0.3, top + 0.1, z - 1.5), (0.3, top + 0.1, z + 1.4), (0.3, top + 1.9, z - 1.4)], 0.6, 'dark')
# Flank armour: hip plates over the hind legs and chest plates over the fore legs.
mirrored(lambda s: zloft([(-16.0, s * 11.2, 2.4, 3.6, 8.8), (-13.5, s * 12.4, 2.8, 3.2, 12.0), (-8.0, s * 12.6, 2.6, 3.6, 12.2),
                          (-6.0, s * 11.6, 2.0, 4.6, 10.4)], 'hull', n=18, p=3.2, name='hip'))
mirrored(lambda s: box((s * 13.4, 8.0, -11.0), (0.4, 4.8, 6.0), 'teal'))
mirrored(lambda s: zloft([(3.0, s * 9.6, 2.2, 1.0, 7.6), (6.0, s * 10.6, 2.6, 0.2, 8.6), (11.0, s * 9.8, 2.4, 0.6, 8.0),
                          (13.0, s * 8.6, 1.8, 1.6, 7.0)], 'hull', n=18, p=3.2, name='chest'))
mirrored(lambda s: box((s * 11.6, 3.0, 7.0), (0.4, 1.2, 6.0), 'dark'))
# Beam cannon ports along the flanks (16 large beam cannons).
for s in (-1, 1):
    for up in (1.8, -0.3):
        for i in range(4):
            cylinder((s * 9.8, up, -3.5 + i * 2.2), (s * 10.5, up, -3.5 + i * 2.2), 0.5, 'dark', segments=10)
# Belly keel and chest plate.
zloft([(-12.0, 0, 5.0, -4.2, -2.0), (4.0, 0, 5.0, -3.8, -1.6), (10.0, 0, 3.4, -2.4, -0.6)], 'dark', n=16, p=3.0, name='keel')

# ---------------------------------------------------------------- wing pylon and beams
mirrored(lambda s: tube([(s * 3.4, 10.5, -2.4), (s * 7.4, 13.8, -2.4), (s * 11.2, 16.0, -2.4)],
                        [(2.4, 0.8), (2.0, 0.7), (1.8, 0.7)], 'hull', n=12, name='pylon'))
zloft([(-5.2, 0, 2.2, 9.5, 15.6), (0.8, 0, 2.2, 9.5, 15.6)], 'hull', n=16, p=4.0, name='pylon-core')


def wing(side):
    # (x, chord centre z, half chord, bottom, top)
    span = [(3.0, -1.6, 4.0, 15.6, 17.6), (12.0, -1.5, 3.8, 15.8, 17.5), (20.0, -1.4, 3.4, 15.9, 17.4),
            (24.0, -1.3, 3.1, 16.0, 17.3)]
    skin([[(side * x, y, cz + z) for z, y in ring(0, hc, b, t, n=16, p=3.4)] for x, cz, hc, b, t in span], 'hull', name='beam')
    # Leading-edge strip, a teal top panel and dark joints between the segments.
    skin([[(side * x, y, cz + hc - 0.1 + z) for z, y in ring(0, 0.3, b + 0.3, t - 0.2, n=8, p=2.0)]
          for x, cz, hc, b, t in span], 'panel', name='edge')
    skin([[(side * x, t + 0.02 + y, cz - 0.6 + z) for z, y in ring(0, hc * 0.55, -0.12, 0.12, n=8, p=4.0)]
          for x, cz, hc, b, t in span[1:]], 'teal', name='top')
    for x in (12.0, 20.0):
        box((side * x, 16.7, -1.5), (0.35, 2.0, 7.8), 'dark')
    # Tip pod with a thruster astern and a forward spike.
    zloft([(-6.0, side * 26.2, 1.8, 15.4, 18.0), (-4.5, side * 26.2, 2.2, 15.0, 18.4), (1.5, side * 26.2, 2.2, 15.0, 18.4),
           (3.2, side * 26.2, 1.2, 15.8, 17.6)], 'panel', n=16, p=3.0, name='tip-pod')
    cylinder((side * 26.2, 16.7, -5.9), (side * 26.2, 16.7, -6.3), 1.0, 'glow', segments=14)
    tube([(side * 26.2, 16.7, 3.0), (side * 26.4, 16.7, 5.0), (side * 26.6, 16.7, 6.6)], [0.6, 0.35, 0.0], 'claw', n=8)
    box((side * 28.3, 16.7, -1.5), (0.3, 3.0, 6.0), 'hull')


mirrored(wing)

# ---------------------------------------------------------------- neck and head
NECK = bezier((0, 5.6, 12.0), (0, 8.4, 21.0), (0, 7.0, 30.5), 7)
tube(NECK, [(2.9, 2.5), (2.6, 2.3), (2.4, 2.1), (2.2, 1.95), (2.05, 1.85), (1.95, 1.8), (1.9, 1.75), (1.9, 1.75)], 'dark',
     n=16, name='neck')
for i, p in enumerate(NECK[1:-1]):
    q = NECK[i + 2]
    d = (Vector(q) - Vector(p)) * 0.5
    tube([tuple(Vector(p) - d * 0.9), p, tuple(Vector(p) + d * 0.85)],
         [(2.8 - i * 0.14, 2.4 - i * 0.1), (3.0 - i * 0.14, 2.6 - i * 0.1), (2.7 - i * 0.14, 2.3 - i * 0.1)],
         'hull', n=16, name='neck-plate')
    plate([(0.25, p[1] + 2.3 - i * 0.1, p[2] - 0.8), (0.25, p[1] + 2.3 - i * 0.1, p[2] + 0.9),
           (0.25, p[1] + 3.5 - i * 0.12, p[2] - 1.0)], 0.5, 'hull')

# Skull, upper jaw and snout: long and narrow, the Gandall cannon in the mouth.
zloft([(28.5, 0, 2.6, 4.2, 8.8), (31.5, 0, 2.9, 4.4, 9.4), (34.5, 0, 2.6, 4.7, 8.9), (37.5, 0, 2.1, 4.9, 8.0),
       (40.5, 0, 1.5, 5.0, 7.0), (43.2, 0, 0.7, 5.2, 6.1)], 'hull', n=20, p=2.4, name='skull')
zloft([(31.0, 0, 2.0, 8.8, 9.7), (34.5, 0, 1.6, 8.4, 9.2), (38.5, 0, 1.0, 7.4, 8.2)], 'panel', n=14, p=2.4, name='brow')
zloft([(29.5, 0, 2.3, 1.8, 4.0), (33.0, 0, 2.1, 2.2, 4.3), (37.0, 0, 1.6, 2.8, 4.4), (41.0, 0, 1.0, 3.3, 4.4),
       (42.8, 0, 0.5, 3.6, 4.3)], 'hull', n=18, p=2.4, name='jaw')
zloft([(31.0, 0, 1.7, 4.0, 4.8), (40.5, 0, 0.9, 4.3, 5.0)], 'dark', n=12, p=3.0, name='mouth')
cylinder((0, 4.6, 39.8), (0, 4.6, 41.2), 0.45, 'mouth', segments=12)
for s in (-1, 1):
    # Red eyes on the brow.
    sphere((s * 1.75, 7.9, 36.6), 0.5, 'eye', scale=(0.8, 0.55, 1.9), segments=12)
    # Swept horns: the long pair (to the original's y 14 at z 22) and a shorter lower pair.
    tube(bezier((s * 1.5, 8.8, 32.5), (s * 2.2, 12.6, 28.0), (s * 3.2, 14.6, 21.2), 5),
         [(0.55, 0.9), (0.5, 0.8), (0.42, 0.65), (0.32, 0.5), (0.2, 0.3), 0.0], 'claw', n=10, name='horn')
    tube(bezier((s * 2.2, 7.8, 31.0), (s * 3.6, 10.0, 27.5), (s * 4.8, 10.6, 23.5), 4),
         [(0.4, 0.6), (0.34, 0.5), (0.24, 0.34), (0.14, 0.2), 0.0], 'claw', n=8, name='horn')
    tube(bezier((s * 2.5, 5.4, 32.5), (s * 3.8, 5.6, 29.5), (s * 4.6, 4.8, 26.5), 3), [0.35, 0.28, 0.15, 0.0], 'claw', n=8)
    tube(bezier((s * 1.4, 2.4, 34.5), (s * 1.9, 1.2, 33.0), (s * 2.2, 0.4, 31.0), 3), [0.3, 0.22, 0.12, 0.0], 'claw', n=8)
    for k in range(4):
        box((s * (1.9 - k * 0.3), 4.35, 33.0 + k * 2.2), (0.35, 0.6, 0.35), 'claw')  # teeth
tube(bezier((0, 7.2, 40.5), (0, 8.2, 38.5), (0, 8.8, 36.0), 3), [0.35, 0.25, 0.12, 0.0], 'claw', n=8)

# ---------------------------------------------------------------- tail
TAIL = bezier((0, 5.0, -15.0), (0, 3.4, -30.0), (0, 0.2, -44.5), 9)
radii = [(2.9 - 2.8 * t, 2.5 - 2.4 * t) for t in (i / 9 for i in range(10))]
tube(TAIL, radii[:-1] + [0.0], 'dark', n=14, name='tail')
for i, p in enumerate(TAIL[:-2]):
    q = TAIL[i + 1]
    d = Vector(q) - Vector(p)
    rx, ry = radii[i]
    tube([tuple(Vector(p) - d * 0.08), tuple(Vector(p) + d * 0.35), tuple(Vector(p) + d * 0.8)],
         [(rx * 1.02, ry * 1.02), (rx * 1.12, ry * 1.12), (rx * 0.95, ry * 0.95)], 'hull', n=14, name='tail-plate')
    if i < 7:
        plate([(0.25, p[1] + ry, p[2] + 1.0), (0.25, p[1] + ry, p[2] - 0.9), (0.25, p[1] + ry + 1.6 - i * 0.15, p[2] - 1.2)],
              0.5, 'hull')


# ---------------------------------------------------------------- legs
def claws(side, x, y, z, count, length, spread=0.9, drop=1.4):
    for k in range(count):
        cx = x + side * (k - (count - 1) / 2) * spread
        tube(bezier((cx, y, z), (cx, y - 0.2, z + length * 0.6), (cx, y - drop, z + length), 4),
             [0.42, 0.36, 0.26, 0.14, 0.0], 'claw', n=8, name='claw')


def hind_leg(side):
    x = side * 9.2
    # Thigh under the hip armour, then a long foot laid forward (as in the original).
    tube([(x, 5.0, -10.5), (x + side * 0.6, 1.5, -10.0), (x + side * 0.8, -2.2, -9.4)], [(2.6, 2.2), (2.5, 2.1), (2.1, 1.8)],
         'hull', n=14, name='thigh')
    sphere((x + side * 0.8, -3.0, -9.6), 2.0, 'dark', segments=14)
    zloft([(-12.5, x + side * 0.8, 1.9, -6.6, -3.6), (-10.0, x + side * 0.8, 2.3, -6.9, -3.0), (-2.0, x + side * 0.8, 2.3, -6.9, -3.6),
           (1.5, x + side * 0.8, 2.0, -6.8, -4.4)], 'hull', n=16, p=3.2, name='foot')
    box((x + side * 0.8, -3.4, -6.0), (3.4, 0.3, 7.0), 'teal')
    claws(side, x + side * 0.8, -5.6, 1.2, 3, 3.4)
    tube([(x + side * 0.8, -5.4, -12.3), (x + side * 0.8, -6.0, -14.2)], [0.5, 0.0], 'claw', n=8)


def fore_leg(side):
    x = side * 10.2
    tube([(x, 5.4, 6.5), (x + side * 0.8, 1.6, 8.0), (x + side * 1.0, -1.8, 9.6)], [(2.2, 1.9), (2.0, 1.7), (1.6, 1.5)],
         'hull', n=14, name='arm')
    sphere((x + side * 1.0, -2.2, 9.8), 1.6, 'dark', segments=12)
    zloft([(8.4, x + side * 1.0, 1.7, -3.9, -1.4), (11.5, x + side * 1.0, 1.8, -4.2, -1.8)], 'hull', n=14, p=3.0, name='hand')
    claws(side, x + side * 1.0, -3.2, 11.3, 3, 2.6, spread=0.8, drop=1.8)


mirrored(hind_leg)
mirrored(fore_leg)

# Reactor vents on the flanks behind the fore legs (three plasma fusion reactors).
for s in (-1, 1):
    for i in range(3):
        box((s * 9.95, 5.6, -2.0 + i * 2.2), (0.3, 2.0, 1.2), 'dark')
        box((s * 10.05, 5.6, -2.0 + i * 2.2), (0.12, 1.6, 0.25), 'glow')

KIT.export(OUT, 'Gandall', previews=PREVIEWS, reference=5595)
