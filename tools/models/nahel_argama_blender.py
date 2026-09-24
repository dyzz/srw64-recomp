"""Author the HD Nahel Argama (world-map ship, original resource 5588) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/nahel_argama_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 81 units = 380 m), model.glb and optional previews with the
original rendered from the same cameras (compare.png).

Design reference: Nahel Argama from Mobile Suit Gundam ZZ (mechanical design
Mika Akitaka), the Argama's successor as flagship. Official figures used:
length 380 m; five forward catapults (three upper, two lower) and an aft landing
deck; the hyper mega particle cannon (18 m bore, 50 m with its energy condenser)
under the central catapult; twin mega particle turrets fore, above and below;
single beam guns at the forward ends of the side catapult decks, two sub guns
and 16 AA mounts; thermonuclear engines. Colours follow the ZZ setting art: white
hull with vermilion bands, grey-lavender catapult decks, orange spherical
housings on the flanks. Plan and side proportions follow the licensed Cosmo Fleet
Special figure (the later UC redesign, which keeps the ZZ layout; its centre
catapult is drawn longer than the ZZ one), cross-checked with the ZZ setting art
and the original's own layout (centre deck to the bow, long thin wings high on
the bridge, rounded engine section, landing deck astern).

Sources: ガンダムチャンネル 设定页 (gundam-c.com/manual/mechanic/zz/nehel-ahgama.html),
Wikipedia ネェル・アーガマ, ガンダムWiki ネェル・アーガマ, MegaHouse Cosmo Fleet
Special 機動戦士ガンダムUC ネェル・アーガマ product photos (megahobby.jp).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'deck': (118, 114, 142, 255), 'vermilion': (222, 92, 48, 255), 'amber': (228, 130, 50, 255),
    'amberdark': (160, 78, 30, 255), 'black': (36, 38, 44, 255),
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


def wing(root, tip, chords, thick, color='hull', edge='vermilion', edge_frac=0.3, name='wing'):
    """Tapered wing from the root to the tip leading edge (x, up, fwd); chords and
    thicknesses are (root, tip); the chord runs astern, the leading edge band is
    coloured separately."""
    (rx, ry, rz), (tx, ty, tz) = root, tip
    length = math.hypot(tx - rx, ty - ry)
    nx, ny = -(ty - ry) / length, (tx - rx) / length
    if ny < 0:
        nx, ny = -nx, -ny

    def half(f):
        return 0.5 * (0.35 + 0.65 * min(1.0, f / 0.3)) * (1.0 - 0.75 * max(0.0, f - 0.3) / 0.7)

    def ring(p, chord, t, fractions):
        x, y, z = p
        top = [(x + nx * half(f) * t, y + ny * half(f) * t, z - f * chord) for f in fractions]
        bottom = [(x - nx * half(f) * t, y - ny * half(f) * t, z - f * chord) for f in reversed(fractions)]
        return top + bottom

    for fractions, col in (((0.0, 0.08, edge_frac), edge), ((edge_frac, 0.65, 1.0), color)):
        solid([ring(root, chords[0], thick[0], fractions), ring(tip, chords[1], thick[1], fractions)],
              col, bevel=0.03, name=name)


def rounded(fwd, half_w, bottom, top, n=20, power=0.45):
    """Superellipse section (x, up) at one station, for the rounded engine section."""
    cy, h = (top + bottom) / 2, (top - bottom) / 2
    pts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        c, s = math.cos(a), math.sin(a)
        pts.append((half_w * math.copysign(abs(c) ** power, c), cy + h * math.copysign(abs(s) ** power, s)))
    return (fwd, pts)


# ------------------------------------------------------------ upper catapults
# Three upper decks: the long centre deck over the hyper mega particle cannon and
# two shorter side decks; grey-lavender decks with white edges.


def deck(stations, top, name='deck', rail=True):
    """stations: [(fwd, x_centre, half_width, thickness)] from bow to root."""
    ring = lambda xc, hw, t: [(xc - hw, top - t), (xc + hw, top - t), (xc + hw, top), (xc - hw, top)]
    loft([(f, ring(xc, hw, t)) for f, xc, hw, t in stations], 'hull', bevel=0.06, name=name)
    inset = [(f - (0.3 if i == 0 else 0), xc, hw - 0.45) for i, (f, xc, hw, t) in enumerate(stations)]
    loft([(f, [(xc - hw, top), (xc + hw, top), (xc + hw, top + 0.07), (xc - hw, top + 0.07)]) for f, xc, hw in inset],
         'deck', name=name + '-surface')
    if rail:
        loft([(f, [(xc - 0.12, top + 0.07), (xc + 0.12, top + 0.07), (xc + 0.12, top + 0.11), (xc - 0.12, top + 0.11)])
              for f, xc, hw in inset], 'black', name=name + '-rail')
        for f, xc, hw in inset[1:-1]:
            for u in (xc - hw + 0.6, xc + hw - 0.6):
                box((u, top + 0.08, f), (0.3, 0.03, 0.3), 'yellow')


deck([(43.0, 0.0, 2.5, 1.0), (30.0, 0.0, 3.0, 1.3), (12.0, 0.0, 3.5, 1.8), (6.0, 0.0, 3.6, 1.8)], 1.1, name='centre-deck')
mirrored(lambda s: deck([(32.0, s * 8.7, 2.7, 1.1), (20.0, s * 8.95, 2.95, 1.4), (6.0, s * 9.3, 3.3, 1.8),
                         (2.0, s * 9.3, 3.3, 1.8)], 0.6, name='side-deck'))
# Lower catapults under the side decks.
mirrored(lambda s: deck([(27.5, s * 8.9, 2.1, 0.9), (4.0, s * 9.4, 2.6, 1.2)], -2.6, name='lower-deck', rail=False))
# Deck support webs between the upper and lower decks.
mirrored(lambda s: box((s * 9.3, -1.9, 12.0), (0.5, 1.6, 14.0), 'panel'))
# Single beam guns hung under the forward ends of the side decks.


def deck_gun(side):
    x = side * 11.0
    box((x, -0.9, 30.5), (1.1, 0.8, 1.8), 'black', bevel=0.06)
    cylinder((x, -0.9, 31.2), (x, -0.9, 34.5), 0.2, 'black', segments=8)


mirrored(deck_gun)

# ------------------------------------------------------------ hyper mega particle cannon
# Under the centre deck: barrel housing with acceleration rings and a dark muzzle.
cylinder((0, -2.4, 7.0), (0, -2.4, 22.0), 2.2, 'panel', segments=24, bevel=0.08)
for f in (10.0, 13.0, 16.0, 19.0):
    cylinder((0, -2.4, f - 0.35), (0, -2.4, f + 0.35), 2.35, 'grey', segments=24)
cylinder((0, -2.4, 22.0), (0, -2.4, 23.4), 2.0, 'dark', segments=24, radius_b=1.8)
cylinder((0, -2.4, 23.3), (0, -2.4, 23.45), 1.3, 'nozzle', segments=20)
box((0, -0.3, 14.0), (2.4, 1.4, 16.0), 'panel')

# ------------------------------------------------------------ forward hull
# Hexagonal-section hull behind the decks, hangar blocks at the deck roots.


def fore_ring(w, bottom, top, chine_lo, chine_hi):
    return [(-(w - 3.0), bottom), (w - 3.0, bottom), (w, chine_lo), (w, chine_hi), (w - 3.4, top),
            (-(w - 3.4), top), (-w, chine_hi), (-w, chine_lo)]


loft([(11.0, fore_ring(12.6, -4.4, 1.4, -2.8, 0.6)), (7.0, fore_ring(13.6, -7.2, 4.6, -4.2, 2.4)),
      (-6.0, fore_ring(13.6, -7.8, 5.4, -4.2, 2.6)), (-10.0, fore_ring(13.0, -7.4, 5.0, -4.0, 2.4))],
     'hull', bevel=0.18, name='fore-hull')
box((0, -7.86, -1.0), (15.0, 0.08, 12.0), 'panel')
# Hangar blocks with the catapult exits.
box((0, 3.3, 8.0), (7.6, 4.4, 5.6), 'hull', bevel=0.14)
box((0, 3.0, 10.82), (5.0, 2.4, 0.1), 'dark')
for s in (-1, 1):
    box((s * 9.4, 2.4, 5.5), (7.0, 3.6, 5.6), 'hull', bevel=0.14)
    box((s * 9.3, 1.9, 8.32), (5.2, 1.8, 0.1), 'dark')
    box((s * 9.4, 4.22, 5.2), (5.0, 0.06, 3.6), 'panel')
# Vermilion bands along the upper hull.
mirrored(lambda s: loft([(4.0, [(s * 10.5, 5.1), (s * 11.3, 4.75), (s * 11.4, 4.9), (s * 10.6, 5.25)]),
                         (-9.0, [(s * 10.4, 5.35), (s * 11.1, 5.0), (s * 11.2, 5.15), (s * 10.5, 5.5)])], 'vermilion', name='band'))

# Orange spherical housings in dark sockets on both flanks.


def flank_sphere(side):
    x, y, f = side * 14.3, -1.5, -2.8
    cylinder((side * 13.4, y, f), (side * 14.4, y, f), 4.3, 'dark', segments=32, bevel=0.1)
    sphere((x, y, f), 3.7, 'amber', scale=(0.8, 1, 1), segments=28)
    for u in (-2.4, -1.2, 0.0, 1.2, 2.4):
        r = math.sqrt(max(0.0, 3.7 ** 2 - u ** 2))
        cylinder((x + side * 0.8 * r * 0.55, y + u, f - r * 0.85), (x + side * 0.8 * r * 0.55, y + u, f + r * 0.85), 0.07,
                 'amberdark', segments=6)


mirrored(flank_sphere)

# Sub guns in pods on the forward flanks.


def sub_gun(side):
    x = side * 14.4
    box((x, 1.2, 6.5), (1.8, 1.8, 3.6), 'panel', bevel=0.1)
    cylinder((x, 1.3, 8.3), (x, 1.3, 12.0), 0.24, 'black', segments=8)


mirrored(sub_gun)

# Twin mega particle turrets, forward above and below.


def turret(fwd, up, under=False):
    sign = -1 if under else 1
    cylinder((0, up, fwd), (0, up + sign * 0.5, fwd), 1.5, 'grey', segments=20)
    hy = up + sign * 1.0
    box((0, hy, fwd), (2.4, 1.0, 2.8), 'black', bevel=0.12)
    for s in (-1, 1):
        cylinder((s * 0.5, hy, fwd + 1.2), (s * 0.5, hy, fwd + 5.2), 0.2, 'black', segments=10)


turret(1.5, 5.4)
turret(3.5, -7.8, under=True)

# ------------------------------------------------------------ bridge
loft([(4.0, [(-3.4, 5.2), (3.4, 5.2), (3.2, 7.4), (-3.2, 7.4)]),
      (-6.0, [(-3.6, 5.2), (3.6, 5.2), (3.4, 7.6), (-3.4, 7.6)])], 'hull', bevel=0.12, name='tower')
# Vermilion sloped front, as on the original.
loft([(7.0, [(-3.0, 5.2), (3.0, 5.2), (3.0, 5.4), (-3.0, 5.4)]),
      (3.9, [(-3.2, 5.2), (3.2, 5.2), (3.1, 7.4), (-3.1, 7.4)])], 'vermilion', bevel=0.06, name='bridge-front')
loft([(4.6, [(-3.4, 7.4), (3.4, 7.4), (3.1, 8.8), (-3.1, 8.8)]),
      (3.6, [(-3.9, 7.4), (3.9, 7.4), (3.6, 9.5), (-3.6, 9.5)]),
      (-4.5, [(-3.9, 7.4), (3.9, 7.4), (3.6, 9.5), (-3.6, 9.5)])], 'hull', bevel=0.1, name='bridge')
box((0, 8.3, 4.35), (5.8, 0.6, 1.0), 'window')
mirrored(lambda s: box((s * 3.92, 8.4, 0.5), (0.1, 0.5, 4.8), 'window'))
mirrored(lambda s: box((s * 4.1, 8.4, 3.0), (0.5, 0.6, 1.0), 'yellow', bevel=0.04))
box((0, 9.55, -0.5), (5.0, 0.08, 6.0), 'panel')
cylinder((0, 9.5, -2.5), (0, 11.6, -2.5), 0.16, 'grey', segments=8)
box((0, 11.0, -2.5), (2.4, 0.16, 0.2), 'grey')
mirrored(lambda s: cylinder((s * 1.4, 9.5, -3.8), (s * 1.6, 11.0, -1.6), 0.08, 'grey', segments=6))

# ------------------------------------------------------------ wings
# Long thin swept wings high on the bridge, vermilion leading edges, drooped tips.
mirrored(lambda s: wing((s * 3.6, 8.5, -1.0), (s * 27.4, 10.0, -10.5), (7.0, 2.6), (0.7, 0.3), name='wing'))
mirrored(lambda s: KIT.plate([(s * 27.3, 10.0, -10.3), (s * 27.3, 10.0, -13.0), (s * 27.6, 8.2, -13.6), (s * 27.6, 8.6, -11.6)][::s],
                             0.2, 'vermilion', bevel=0.03))

# ------------------------------------------------------------ engine section
# Rounded engine section with two engine pods on top, a landing deck astern.
loft([rounded(-8.0, 12.4, -7.2, 4.6), rounded(-12.0, 14.4, -7.6, 5.0), rounded(-20.0, 14.8, -7.4, 4.8),
      rounded(-26.0, 13.4, -6.6, 4.0), rounded(-29.0, 11.2, -5.4, 3.0)], 'hull', bevel=0.1, name='engine-section')
# Dark vent row on the flanks, like the ports of the setting art.
for s in (-1, 1):
    for i in range(4):
        f = -14.0 - i * 3.0
        cylinder((s * 14.2, -2.4, f), (s * 14.95, -2.4, f), 1.0, 'dark', segments=16)
        cylinder((s * 14.9, -2.4, f), (s * 15.0, -2.4, f), 0.7, 'black', segments=14)


def engine_pod(side):
    x0, x1 = side * 4.8, side * 12.6
    lo, hi = min(x0, x1), max(x0, x1)
    loft([(-6.0, [(lo, 4.0), (hi, 4.0), (hi, 4.8), (lo, 4.8)]),
          (-9.0, [(lo, 3.6), (hi, 3.6), (hi - 0.4, 7.3), (lo + 0.4, 7.3)]),
          (-26.5, [(lo, 2.4), (hi, 2.4), (hi - 0.4, 7.3), (lo + 0.4, 7.3)])], 'hull', bevel=0.16, name='engine-pod')
    xc = (x0 + x1) / 2
    for dx in (-0.9, 0.9):
        box((xc + dx * side, 7.34, -17.0), (0.7, 0.06, 13.0), 'vermilion')
    box((xc, 7.34, -10.5), (5.0, 0.06, 2.6), 'panel')
    # Two main nozzles per pod.
    for dx in (-1.9, 1.9):
        x = xc + dx
        cylinder((x, 5.0, -26.2), (x, 5.0, -28.4), 1.6, 'dark', segments=20, radius_b=1.7, cap=False)
        cylinder((x, 5.0, -26.3), (x, 5.0, -28.3), 1.3, 'nozzle', segments=20, cap=False)
        cylinder((x, 5.0, -26.8), (x, 5.0, -26.9), 1.3, 'glow', segments=20)
    # Three lower nozzles on the engine section face.
    for dx in (-2.6, 0.0, 2.6):
        x = xc + dx
        cylinder((x, -2.6, -27.0), (x, -2.6, -30.0), 1.2, 'dark', segments=18, radius_b=1.3, cap=False)
        cylinder((x, -2.6, -27.1), (x, -2.6, -29.9), 0.95, 'nozzle', segments=18, cap=False)
        cylinder((x, -2.6, -28.4), (x, -2.6, -28.5), 0.95, 'glow', segments=18)
    # Small fins: dorsal on the pod, ventral under the engine section, both canted outward.
    wing((xc + side * 1.5, 7.2, -18.0), (xc + side * 3.2, 10.6, -23.0), (6.0, 2.4), (0.4, 0.2),
         color='hull', edge='vermilion', name='dorsal-fin')
    wing((side * 10.0, -6.4, -17.5), (side * 14.0, -10.0, -23.0), (6.5, 2.6), (0.45, 0.2),
         color='hull', edge='vermilion', name='ventral-fin')


mirrored(engine_pod)

# Aft landing deck on the centreline, running out past the engines.
loft([(-18.0, [(-3.8, -0.6), (3.8, -0.6), (3.8, 1.4), (-3.8, 1.4)]),
      (-33.0, [(-3.8, -0.4), (3.8, -0.4), (3.8, 1.4), (-3.8, 1.4)]),
      (-38.0, [(-2.8, 0.4), (2.8, 0.4), (2.8, 1.4), (-2.8, 1.4)])], 'hull', bevel=0.1, name='landing-deck')
loft([(-20.0, [(-3.2, 1.4), (3.2, 1.4), (3.2, 1.48), (-3.2, 1.48)]),
      (-37.6, [(-2.3, 1.4), (2.3, 1.4), (2.3, 1.48), (-2.3, 1.48)])], 'deck', name='landing-surface')
loft([(-20.0, [(-0.12, 1.48), (0.12, 1.48), (0.12, 1.52), (-0.12, 1.52)]),
      (-37.4, [(-0.12, 1.48), (0.12, 1.48), (0.12, 1.52), (-0.12, 1.52)])], 'black', name='landing-rail')
for f in (-24.0, -28.0, -32.0):
    box((0, 1.5, f), (4.2, 0.04, 0.3), 'vermilion')
box((0, 1.2, -18.6), (5.4, 2.0, 1.4), 'dark')
# Central upper hull between the pods, down to the landing deck.
loft([(-6.0, [(-4.8, 1.0), (4.8, 1.0), (4.8, 5.8), (-4.8, 5.8)]),
      (-18.0, [(-4.8, 1.0), (4.8, 1.0), (4.8, 5.0), (-4.8, 5.0)])], 'hull', bevel=0.12, name='aft-spine')
box((0, 5.84, -11.0), (3.4, 0.06, 7.0), 'panel')

# ------------------------------------------------------------ AA mounts (16)


def aa(x, up, fwd, facing=1):
    box((x, up + 0.18, fwd), (0.7, 0.36, 0.7), 'grey', bevel=0.04)
    for s in (-1, 1):
        cylinder((x + s * 0.14, up + 0.26, fwd), (x + s * 0.14, up + 0.26, fwd + facing * 1.0), 0.06, 'dark', segments=6)


AA = [(s * 11.8, 4.0, f) for s in (-1, 1) for f in (1.0, -4.0)]       # forward hull shoulders
AA += [(s * 2.8, 5.5, f) for s in (-1, 1) for f in (-7.5,)]            # behind the bridge
AA += [(s * 8.7, 7.3, f) for s in (-1, 1) for f in (-7.0, -24.0)]      # engine pods
AA += [(s * 12.9, 3.4, f) for s in (-1, 1) for f in (-13.0, -23.0)]    # engine section shoulders
AA += [(s * 5.5, 5.3, 7.5) for s in (-1, 1)]                           # hangar roofs
assert len(AA) == 16, len(AA)
for x, up, f in AA:
    aa(x, up, f, facing=1 if f > -15 else -1)

KIT.export(OUT, 'Nahel Argama', previews=PREVIEWS, reference=5588)
