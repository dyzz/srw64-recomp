"""Author the HD battle background 5837: a harbour city (Yokohama) — quay, skyline, two ships.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \\
      --python tools/models/battle_city_blender.py -- OUTPUT_DIRECTORY

Writes mesh.json and texture.png (lighting baked by battle_bake.py; --flat for plain vertex
colours) in the original resource's local frame (+y up, water at y = 0, the
camera looks from +z). The battle draws this model three times, at x -1000, 0 and
+1000 (docs/design/battle-animation-rendering.md §2.3), so everything stays inside
x -500..500 and the two edges meet.

What the original holds (resource 5837, 443 triangles), kept in place and size:
  - a quay: a deck at y 26 from z -500 to -200 and a dark wall from the water up at z -200;
  - the skyline: one 1000-wide card of low buildings at z -500 (y 17..77) and two tower
    cards at z -475, x -10..110 and -500..-350, up to y 147;
  - a white passenger liner named 横浜丸, stern x -180, bow x 31, beam z -177..-129,
    hull to y 21, decks to y 40, funnel to y 54;
  - a black-hulled work ship, stern x 101, bow x 318, beam z -183..-121: a gantry crane
    at x 147..187, two red and white derricks, hatch covers and cargo, a white deckhouse.
The cards become buildings with real depth: banded office towers, apartment slabs with
balconies, punched-window mid-rises; the camera only sees the +z faces and the sides.
"""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

OUT, PREVIEWS = args()
COLORS = {
    'water_line': (30, 36, 52, 255), 'quay': (92, 90, 88, 255), 'quay_dark': (58, 57, 58, 255),
    'coping': (176, 174, 168, 255), 'deck': (150, 150, 146, 255), 'asphalt': (70, 72, 76, 255),
    'paint': (220, 220, 210, 255), 'fender': (28, 28, 30, 255), 'bollard': (60, 64, 70, 255),
    'trunk': (92, 70, 52, 255), 'leaf': (58, 104, 58, 255), 'leaf2': (74, 120, 64, 255),
    'lamp': (120, 124, 130, 255),
    'white': (232, 233, 228, 255), 'offwhite': (212, 212, 204, 255), 'beige': (206, 194, 170, 255),
    'cream': (222, 214, 190, 255), 'concrete': (170, 170, 166, 255), 'grey': (150, 154, 160, 255),
    'darkgrey': (104, 108, 114, 255), 'glass': (118, 138, 160, 255), 'glass2': (140, 158, 176, 255),
    'window': (44, 56, 74, 255), 'roof': (128, 128, 124, 255), 'rail': (190, 192, 190, 255),
    'hull_white': (236, 236, 232, 255), 'navy': (34, 46, 84, 255), 'boot': (150, 40, 36, 255),
    'orange': (232, 128, 44, 255), 'funnel_band': (30, 70, 150, 255), 'black': (26, 27, 30, 255),
    'ship_deck': (120, 122, 118, 255), 'crane': (176, 178, 176, 255), 'red': (196, 36, 32, 255),
    'hatch': (96, 110, 92, 255), 'cont_o': (196, 96, 44, 255), 'cont_b': (52, 86, 140, 255),
    'cont_g': (70, 120, 84, 255), 'cont_r': (150, 48, 44, 255), 'sign': (30, 30, 34, 255),
}
KIT = Kit(COLORS)


class Batch:
    """Boxes and other simple solids merged into one bmesh per colour (thousands of parts)."""

    def __init__(self):
        self.meshes = {}

    def bm(self, color):
        if color not in self.meshes:
            self.meshes[color] = bmesh.new()
        return self.meshes[color]

    def box(self, lo, hi, color):
        """Axis-aligned box from corner lo to corner hi, original axes (x, y up, z)."""
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        if x1 - x0 <= 0 or y1 - y0 <= 0 or z1 - z0 <= 0:
            return
        bm = self.bm(color)
        v = [bm.verts.new(P(x, y, z)) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
        # index = xi*4 + yi*2 + zi
        for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            bm.faces.new([v[i] for i in f])

    def prism(self, ring, y0, y1, color):
        """Vertical prism over a polygon ring [(x, z)] (counter-clockwise seen from above)."""
        bm = self.bm(color)
        lo = [bm.verts.new(P(x, y0, z)) for x, z in ring]
        hi = [bm.verts.new(P(x, y1, z)) for x, z in ring]
        bm.faces.new(list(reversed(lo)))
        bm.faces.new(hi)
        n = len(ring)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))

    def cylinder(self, a, b, r, color, segments=8, r_b=None):
        a, b = Vector(P(*a)), Vector(P(*b))
        axis = b - a
        tmp = bmesh.new()
        bmesh.ops.create_cone(tmp, cap_ends=True, cap_tris=False, segments=segments, radius1=r,
                              radius2=r if r_b is None else r_b, depth=axis.length)
        rot = axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
        bmesh.ops.transform(tmp, matrix=Matrix.Translation((a + b) / 2) @ rot, verts=tmp.verts)
        self.merge(tmp, color)

    def sphere(self, c, r, color, scale=(1, 1, 1), segments=10):
        tmp = bmesh.new()
        bmesh.ops.create_uvsphere(tmp, u_segments=segments, v_segments=max(4, segments // 2), radius=r)
        sx, sy, sz = scale
        bmesh.ops.transform(tmp, matrix=Matrix.Translation(P(*c)) @ Matrix.Diagonal((sx, sz, sy, 1)), verts=tmp.verts)
        self.merge(tmp, color)

    def merge(self, tmp, color):
        bm = self.bm(color)
        mapping = {v: bm.verts.new(v.co) for v in tmp.verts}
        for f in tmp.faces:
            bm.faces.new([mapping[v] for v in f.verts])
        tmp.free()

    def flush(self):
        for color, bm in self.meshes.items():
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
            KIT.add_object(bm, color, name=f'batch-{color}')
        self.meshes = {}


B = Batch()
rng = random.Random(5837)


def loft_x(sections, color, bevel=0.0, name='hull'):
    """Loft along x: sections [(x, [(z, y), ...])], equal counts, closed rings."""
    bm = bmesh.new()
    rings = [[bm.verts.new(P(x, y, z)) for z, y in pts] for x, pts in sections]
    n = len(rings[0])
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return KIT.add_object(bm, color, bevel, name=name)


# ------------------------------------------------------------------------------- quay
DECK = 26.0
WALL = -200.0
B.box((-500, -8, -214), (500, DECK - 1.5, WALL), 'quay')
B.box((-500, DECK - 1.5, -202.5), (500, DECK + 0.6, WALL + 1.2), 'coping')
B.box((-500, -8, WALL - 0.01), (500, 1.2, WALL + 0.25), 'quay_dark')       # wet band at the water
for x in range(-490, 500, 20):
    B.box((x - 0.35, 1.2, WALL - 0.05), (x + 0.35, DECK - 1.5, WALL + 0.12), 'quay_dark')   # panel joints
for x in range(-480, 500, 40):
    B.box((x - 2.2, 6, WALL), (x + 2.2, 18, WALL + 2.2), 'fender')
    B.cylinder((x, DECK, -205), (x, DECK + 2.2, -205), 1.1, 'bollard', segments=10)
    B.cylinder((x, DECK + 2.2, -205), (x, DECK + 2.8, -205), 1.6, 'bollard', segments=10)
B.box((-500, DECK - 1, -500), (500, DECK, -202.5), 'deck')
for z in range(-490, -205, 24):
    B.box((-500, DECK, z - 0.15), (500, DECK + 0.05, z + 0.15), 'quay_dark')   # slab seams
B.box((-500, DECK, -236), (500, DECK + 0.08, -220), 'asphalt')                 # harbour road
for x in range(-495, 500, 12):
    B.box((x, DECK + 0.08, -228.3), (x + 6, DECK + 0.12, -227.7), 'paint')
for x in range(-490, 500, 16):                                                   # street trees
    h = 6 + rng.random() * 2
    B.cylinder((x, DECK, -244), (x, DECK + h * 0.55, -244), 0.5, 'trunk', segments=6)
    B.sphere((x, DECK + h * 0.72, -244), 3.4 + rng.random() * 0.8, 'leaf' if rng.random() < 0.6 else 'leaf2',
             scale=(1, 0.9, 1), segments=10)
for x in range(-470, 500, 60):                                                   # lamp posts
    B.cylinder((x, DECK, -217), (x, DECK + 10, -217), 0.3, 'lamp', segments=6)
    B.box((x - 0.4, DECK + 9.6, -217), (x + 0.4, DECK + 10.1, -214.5), 'lamp')


# --------------------------------------------------------------------------- buildings
def office_tower(x0, x1, z0, z1, top, wall='white', glass='glass'):
    """Curtain-wall tower: glass core, light spandrels per floor, pilasters, a crown."""
    floor = 3.6
    B.box((x0 + 0.6, DECK, z0 + 0.6), (x1 - 0.6, top, z1 - 0.6), glass)
    y = DECK + 4.5
    B.box((x0, DECK, z0), (x1, DECK + 4.5, z1), 'darkgrey')                       # lobby floor
    while y + floor <= top - 3:
        B.box((x0, y, z0), (x1, y + 1.1, z1), wall)                               # spandrel band
        y += floor
    step = 6.0 if x1 - x0 > 40 else 4.5
    n = max(2, round((x1 - x0) / step))
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        B.box((x - 0.45, DECK + 4.5, z1 - 1.0), (x + 0.45, top - 3, z1 + 0.3), wall)  # front pilasters
    m = max(2, round((z1 - z0) / step))
    for side in (x0, x1):
        for i in range(m + 1):
            z = z0 + (z1 - z0) * i / m
            d = -0.3 if side == x0 else 0.3
            B.box((min(side, side + d) - 0.4, DECK + 4.5, z - 0.45), (max(side, side + d) + 0.4, top - 3, z + 0.45), wall)
    B.box((x0 - 0.3, top - 3, z0 - 0.3), (x1 + 0.3, top, z1 + 0.3), wall)          # crown
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    B.box((cx - (x1 - x0) * 0.3, top, cz - (z1 - z0) * 0.25), (cx + (x1 - x0) * 0.3, top + 4.5, cz + (z1 - z0) * 0.25), 'roof')
    B.box((cx - 3, top + 4.5, cz - 3), (cx + 3, top + 7, cz + 3), 'darkgrey')
    B.cylinder((cx + (x1 - x0) * 0.2, top + 4.5, cz), (cx + (x1 - x0) * 0.2, top + 14, cz), 0.25, 'red', segments=6)


def apartment(x0, x1, z0, z1, top, wall='beige'):
    """Apartment slab: balconies on every floor of the front, stair cores, punched side windows."""
    floor = 2.9
    B.box((x0, DECK, z0), (x1, top, z1), wall)
    y = DECK + 3.2
    while y + floor <= top - 1.5:
        B.box((x0 + 0.8, y + 0.5, z1 - 0.1), (x1 - 0.8, y + 2.4, z1 + 0.05), 'window')      # openings
        B.box((x0 + 0.5, y - 0.25, z1), (x1 - 0.5, y + 0.05, z1 + 1.6), 'offwhite')          # balcony slab
        B.box((x0 + 0.5, y + 0.05, z1 + 1.35), (x1 - 0.5, y + 1.15, z1 + 1.6), 'rail')      # parapet
        for side in (x0, x1):
            for zc in [z0 + (z1 - z0) * k / 4 for k in (1, 2, 3)]:
                d = -0.1 if side == x0 else 0.1
                B.box((min(side, side + d), y + 0.7, zc - 0.8), (max(side, side + d), y + 2.2, zc + 0.8), 'window')
        y += floor
    for xs in [x0 + (x1 - x0) * k / 3 for k in (1, 2)]:                                     # dividing walls
        B.box((xs - 0.3, DECK + 3, z1), (xs + 0.3, top - 1.5, z1 + 1.7), wall)
    B.box((x0 - 0.2, top - 1, z0 - 0.2), (x1 + 0.2, top + 0.6, z1 + 0.2), 'roof')
    cx = (x0 + x1) / 2
    B.box((cx - 3, top + 0.6, (z0 + z1) / 2 - 2.5), (cx + 3, top + 4, (z0 + z1) / 2 + 2.5), wall)  # lift house
    B.cylinder((x1 - 4, top + 0.6, z0 + 3), (x1 - 4, top + 3.5, z0 + 3), 1.4, 'concrete', segments=10)  # water tank


def midrise(x0, x1, z0, z1, top, wall='concrete'):
    """Office mid-rise with ribbon windows and a set-back top floor."""
    floor = 3.4
    B.box((x0, DECK, z0), (x1, top, z1), wall)
    y = DECK + 4.0
    while y + floor <= top - 2:
        B.box((x0 + 1.2, y + 0.8, z1 - 0.1), (x1 - 1.2, y + 2.6, z1 + 0.08), 'glass2')
        for side in (x0, x1):
            d = -0.08 if side == x0 else 0.08
            B.box((min(side, side + d), y + 0.8, z0 + 1.2), (max(side, side + d), y + 2.6, z1 - 1.2), 'glass2')
        y += floor
    B.box((x0, DECK, z1 - 0.1), (x1, DECK + 3.4, z1 + 0.1), 'darkgrey')
    B.box((x0 - 0.3, top - 0.8, z0 - 0.3), (x1 + 0.3, top + 0.8, z1 + 0.3), 'offwhite')
    B.box((x0 + 3, top + 0.8, z0 + 3), (x1 - 3, top + 3.5, z1 - 3), 'roof')


# The two towers of the original cards (x -10..110 and -500..-350, tops at y 147) and
# rows of lower buildings standing where the low card's buildings stood (up to y 77).
office_tower(15, 85, -485, -445, 147, wall='white', glass='glass2')
office_tower(-492, -408, -490, -452, 138, wall='offwhite', glass='glass2')
office_tower(-402, -362, -470, -440, 118, wall='white')


def row(z0, depth, lo, hi, kinds, gap=(3, 9), avoid=()):
    x = -498 + rng.random() * 6
    while x < 490:
        w = rng.uniform(26, 62)
        x1 = min(x + w, 498)
        if x1 - x < 16:
            break
        if not any(a < x1 and x < b for a, b in avoid):
            top = DECK + rng.uniform(lo, hi)
            kind = rng.choice(kinds)
            d = depth * rng.uniform(0.7, 1.0)
            wall = rng.choice({'apt': ['beige', 'cream', 'offwhite'], 'mid': ['concrete', 'grey', 'offwhite'],
                               'tower': ['white', 'offwhite']}[kind])
            {'apt': apartment, 'mid': midrise, 'tower': office_tower}[kind](x, x1, z0 - d, z0, top, wall)
        x = x1 + rng.uniform(*gap)


row(-420, 34, 34, 64, ['apt', 'mid', 'tower'], avoid=((5, 95), (-500, -355)))
row(-360, 30, 22, 46, ['apt', 'mid', 'apt'])
row(-300, 26, 12, 30, ['mid', 'apt', 'mid'], gap=(6, 16))


# ------------------------------------------------------------------------ the liner
LZ = -153.0  # centre line


def liner():
    # half-breadth at the deck (y 21) and at the waterline, per station x
    st = [(-180, 7, 0.5), (-177, 13, 5), (-171, 17, 13), (-160, 20, 17), (-140, 21.5, 18.5), (-60, 23.2, 21),
          (-21, 23.5, 20.5), (-3, 19.5, 12), (8, 14, 5), (20, 7, 1), (31, 0.8, 0.3)]
    secs = []
    for x, hd, hw in st:
        bow = max(0.0, (x - 10) / 21)                       # the stem rakes forward above the water
        wl = hw
        ring = [(LZ - hd, 21.4), (LZ - hd * 0.99, 12), (LZ - wl, 1.5), (LZ - wl * 0.8, -3),
                (LZ + wl * 0.8, -3), (LZ + wl, 1.5), (LZ + hd * 0.99, 12), (LZ + hd, 21.4)]
        if bow > 0:
            ring = [(z, y) for z, y in ring]
        secs.append((x, ring))
    loft_x(secs, 'hull_white', bevel=0.35, name='liner-hull')
    # boot-top and the navy sheer line, slightly proud of the hull
    loft_x([(x, [(LZ - hw - 0.25, 2.2), (LZ - hw * 0.8 - 0.2, -2.5), (LZ + hw * 0.8 + 0.2, -2.5), (LZ + hw + 0.25, 2.2)])
            for x, _, hw in st[1:-2]], 'navy', name='liner-boot')
    for x, hd, _ in st[2:-1]:
        pass
    # portholes: two rows along both sides where the side is straight
    for x in range(-165, -5, 3):
        for y in (11.5, 15.5):
            for s in (-1, 1):
                hd = 23.0 if x < -25 else 21
                z = LZ + s * (hd + 0.05)
                B.box((x - 0.55, y - 0.55, min(z, z + s * 0.25)), (x + 0.55, y + 0.55, max(z, z + s * 0.25)), 'window')
    # superstructure tiers: (x_aft, x_fore, y0, y1, half width), each with a window band
    tiers = [(-172, -18, 21, 26, 20.5), (-160, -28, 26, 31, 19.0), (-146, -38, 31, 35.5, 17.0),
             (-132, -46, 35.5, 39.5, 14.0)]
    for xa, xf, y0, y1, hw in tiers:
        nose = hw * 0.55
        ring = [(xa, LZ - hw), (xf - nose, LZ - hw), (xf, LZ - hw * 0.35), (xf, LZ + hw * 0.35), (xf - nose, LZ + hw),
                (xa, LZ + hw)]
        B.prism([(x, z) for x, z in ring], y0, y1, 'hull_white')
        band = [(xa + 1, LZ - hw - 0.1), (xf - nose, LZ - hw - 0.1), (xf + 0.1, LZ - hw * 0.35), (xf + 0.1, LZ + hw * 0.35),
                (xf - nose, LZ + hw + 0.1), (xa + 1, LZ + hw + 0.1)]
        B.prism(band, y0 + (y1 - y0) * 0.3, y1 - (y1 - y0) * 0.22, 'window')
        B.prism([(xa - 0.5, LZ - hw - 0.4), (xf - nose, LZ - hw - 0.4), (xf + 0.5, LZ - hw * 0.35), (xf + 0.5, LZ + hw * 0.35),
                 (xf - nose, LZ + hw + 0.4), (xa - 0.5, LZ + hw + 0.4)], y1 - 0.25, y1 + 0.15, 'offwhite')
    # bridge with wings, forward on the top tier
    B.box((-58, 39.5, LZ - 12), (-44, 43.5, LZ + 12), 'hull_white')
    B.box((-44.1, 41, LZ - 11), (-43.9, 43, LZ + 11), 'window')
    B.box((-54, 41.5, LZ - 21.5), (-46, 42.2, LZ + 21.5), 'offwhite')
    B.box((-47, 42.2, LZ - 21.5), (-46, 43, LZ + 21.5), 'rail')
    B.cylinder((-52, 43.5, LZ), (-52, 51, LZ), 0.35, 'offwhite', segments=6)                  # mast
    B.box((-54, 49.5, LZ - 3), (-50, 49.9, LZ + 3), 'darkgrey')                               # radar
    # funnel: raked, company band
    B.prism([(-66, LZ - 5.5), (-56, LZ - 5.5), (-54, LZ - 3), (-54, LZ + 3), (-56, LZ + 5.5), (-66, LZ + 5.5)],
            39.5, 49.5, 'hull_white')
    B.prism([(-66.3, LZ - 5.8), (-55.8, LZ - 5.8), (-53.7, LZ - 3.1), (-53.7, LZ + 3.1), (-55.8, LZ + 5.8), (-66.3, LZ + 5.8)],
            47.5, 52, 'funnel_band')
    B.prism([(-66.4, LZ - 5.9), (-55.7, LZ - 5.9), (-53.6, LZ - 3.1), (-53.6, LZ + 3.1), (-55.7, LZ + 5.9), (-66.4, LZ + 5.9)],
            52, 54, 'black')
    # lifeboats along both sides of the second tier
    for x in range(-150, -56, 13):
        for s in (-1, 1):
            B.sphere((x, 29.2, LZ + s * 20.2), 1.0, 'orange', scale=(5.0, 1.2, 1.4), segments=12)
            B.cylinder((x - 3.5, 29.5, LZ + s * 19), (x - 3.5, 32.5, LZ + s * 20.2), 0.18, 'grey', segments=4)
            B.cylinder((x + 3.5, 29.5, LZ + s * 19), (x + 3.5, 32.5, LZ + s * 20.2), 0.18, 'grey', segments=4)
    # deck railings on the open bow and stern decks
    for x0, x1 in ((-176, -172), (-18, 26)):
        for s in (-1, 1):
            B.box((x0, 21.4, LZ + s * 19 - 0.1), (x1, 22.6, LZ + s * 19 + 0.1), 'rail')
    B.cylinder((-2, 21.4, LZ), (-2, 23.4, LZ), 1.2, 'darkgrey', segments=10)                # windlass
    # the name 横浜丸 on both bows
    for s in (-1, 1):
        name_text('横浜丸', x=-16, y=14.5, z=LZ + s * 21.45, side=s, size=4.2)


def name_text(text, x, y, z, side, size):
    font = None
    for path in ('/System/Library/Fonts/Hiragino Sans GB.ttc', '/System/Library/Fonts/STHeiti Medium.ttc'):
        try:
            font = bpy.data.fonts.load(path)
            break
        except Exception:
            pass
    if font is None:
        return
    curve = bpy.data.curves.new('name', 'FONT')
    curve.body = text
    curve.font = font
    curve.size = size
    curve.extrude = 0.08
    curve.align_x = 'CENTER'
    obj = bpy.data.objects.new('name', curve)
    bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.update()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    bpy.data.objects.remove(obj, do_unlink=True)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    # text lies in Blender's xy plane reading along +x; stand it up on the hull side
    # facing out (original +z side faces the camera and reads left to right)
    rot = Matrix.Rotation(math.radians(90), 4, 'X')
    if side < 0:
        rot = Matrix.Rotation(math.radians(180), 4, 'Z') @ rot
    bmesh.ops.transform(bm, matrix=Matrix.Translation(P(x, y, z)) @ rot, verts=bm.verts)
    B.merge(bm, 'navy')


# -------------------------------------------------------------------- the work ship
WZ = -153.0


def workship():
    st = [(101, 18, 0.5), (104, 21.5, 12), (110, 23.5, 17.5), (141, 25.5, 19.5), (250, 28, 20), (280, 26, 15),
          (296, 21, 8), (308, 13, 2), (318, 1.5, 0.3)]
    secs = [(x, [(WZ - hd, 19.5), (WZ - hd * 0.97, 9), (WZ - hw, 1), (WZ - hw * 0.8, -3),
                 (WZ + hw * 0.8, -3), (WZ + hw, 1), (WZ + hd * 0.97, 9), (WZ + hd, 19.5)]) for x, hd, hw in st]
    loft_x(secs, 'black', bevel=0.3, name='work-hull')
    loft_x([(x, [(WZ - hd + 0.6, 19.4), (WZ + hd - 0.6, 19.4), (WZ + hd - 0.6, 20.2), (WZ - hd + 0.6, 20.2)])
            for x, hd, _ in st[1:-1]], 'ship_deck', name='work-deck')
    for x, hd, _ in st[1:-2]:
        pass
    # bulwark rails
    for (xa, ha), (xb, hb) in zip([(s[0], s[1]) for s in st[1:-1]], [(s[0], s[1]) for s in st[2:]]):
        for s in (-1, 1):
            B.cylinder((xa, 21.6, WZ + s * (ha - 0.3)), (xb, 21.6, WZ + s * (hb - 0.3)), 0.12, 'rail', segments=4)
    B.box((104, 19.5, WZ - 24), (318, 19.8, WZ - 23.2), 'offwhite')      # sheer stripe (camera side)
    # aft working deck and the gantry (A-frame) crane at x 147..187
    B.box((112, 20.2, WZ - 20), (140, 20.4, WZ + 20), 'hatch')
    for x in (150, 172):
        for s in (-1, 1):
            z = WZ + s * 11
            B.box((x - 1.2, 20.2, z - 1.2), (x + 1.2, 48, z + 1.2), 'crane')
            for y in range(24, 46, 6):                                      # lattice bracing
                B.cylinder((x - 1.2, y, z - s * 1.2), (x - 1.2, y + 6, z + s * 1.2), 0.18, 'crane', segments=4)
                B.cylinder((x + 1.2, y, z - s * 1.2), (x + 1.2, y + 6, z + s * 1.2), 0.18, 'crane', segments=4)
        B.box((x - 1.4, 45, WZ - 13), (x + 1.4, 48.5, WZ + 13), 'crane')
    B.box((150, 46.5, WZ - 1.5), (172, 48.5, WZ + 1.5), 'crane')
    B.box((158, 38, WZ - 2), (164, 44, WZ + 2), 'orange')                   # hoist block
    B.cylinder((161, 38, WZ), (161, 30, WZ), 0.15, 'black', segments=4)
    # open hold with a green-lit panel (the original's dark hold)
    B.box((152, 20.2, WZ - 8), (170, 20.3, WZ + 8), 'window')
    B.box((175, 20.2, WZ - 6), (190, 25, WZ + 6), 'grey')                    # machinery house
    # cargo: hatch covers and container stacks
    for x0 in range(196, 246, 16):
        pass
    B.box((190, 20.2, WZ - 18), (262, 21.2, WZ + 18), 'hatch')
    for i, x0 in enumerate(range(191, 258, 13)):
        for j, zc in enumerate((WZ - 10, WZ + 0.5, WZ + 11)):
            layers = 1 + (i + j) % 3
            for k in range(layers):
                c = ('cont_o', 'cont_b', 'cont_g', 'cont_r', 'grey')[(i * 3 + j + k) % 5]
                B.box((x0, 21.2 + k * 5.2, zc - 4.8), (x0 + 12.2, 21.2 + k * 5.2 + 5.0, zc + 4.8), c)
                for rib in range(1, 6):
                    xr = x0 + rib * 12.2 / 6
                    B.box((xr - 0.12, 21.2 + k * 5.2 + 0.3, zc + 4.8), (xr + 0.12, 21.2 + k * 5.2 + 4.7, zc + 4.95), 'darkgrey')
    # white deckhouse forward of the cargo, bridge windows, mast (x 231..244 in the original)
    B.box((262, 20.2, WZ - 12), (278, 34, WZ + 12), 'hull_white')
    B.box((264, 34, WZ - 14), (278, 39, WZ + 14), 'hull_white')
    B.box((277.95, 35.5, WZ - 13), (278.1, 38, WZ + 13), 'window')
    for y in (23, 27, 31):
        B.box((264, y, WZ + 12), (276, y + 2, WZ + 12.1), 'window')
    B.cylinder((270, 39, WZ), (270, 50, WZ), 0.35, 'offwhite', segments=6)
    B.box((268, 47, WZ - 3), (272, 47.4, WZ + 3), 'darkgrey')
    # derricks: red and white striped booms on pedestals, aft and forward
    for base, tip in (((122, 21, WZ + 8), (112, 51, WZ + 15)), ((282, 21, WZ - 6), (296, 50, WZ - 14))):
        B.cylinder((base[0], 20.2, base[2]), (base[0], 24, base[2]), 2.2, 'grey', segments=10)
        a, b = Vector(base) + Vector((0, 3, 0)), Vector(tip)
        n = 8
        for i in range(n):
            p0, p1 = a.lerp(b, i / n), a.lerp(b, (i + 1) / n)
            B.cylinder(tuple(p0), tuple(p1), 0.8 - 0.3 * i / n, 'red' if i % 2 == 0 else 'white', segments=8,
                       r_b=0.8 - 0.3 * (i + 1) / n)
        B.cylinder(tuple(b), (b.x, 30, b.z), 0.1, 'black', segments=4)
    # bow: windlass and bitts
    B.cylinder((300, 20.2, WZ - 3), (300, 22, WZ - 3), 0.9, 'darkgrey', segments=8)
    B.cylinder((300, 20.2, WZ + 3), (300, 22, WZ + 3), 0.9, 'darkgrey', segments=8)


liner()
workship()
B.flush()
if '--flat' in sys.argv:
    KIT.export(OUT, 'Battle harbour city (5837)', previews=False)   # vertex colours only
else:
    from battle_bake import bake  # noqa: E402
    bake(KIT, OUT)
