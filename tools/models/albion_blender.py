"""Author the HD Albion (world-map ship, original resource 5584) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/albion_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 63 units = 305 m), model.glb and optional previews with the
original rendered from the same cameras (compare.png).

Design reference: Pegasus-class assault landing ship Albion (seventh ship of the
class) from Mobile Suit Gundam 0083 STARDUST MEMORY (styling Shoji Kawamori).
Official figures used: length 305 m, width 210 m, height 82 m; two forward hulls
("legs") holding the MS decks, with the MS catapults moved onto their upper
surface (four catapults: two MS, two aircraft); a bridge styled after a Gundam
face with a yellow V antenna; laser propulsion receiver mirrors (gold dish, blue
rim) on both flanks; four variable wings in an X instead of the two of earlier
Pegasus-class ships; twin mega particle cannons on the hull sides and under the
catapults, four large bow missile launchers and 18 twin laser cannons. Colours:
white hull, red lower hull and wing leading edges, blue-grey undersides, dark
engine clusters of seven nozzles. Proportions follow the top, front and rear
views of the licensed Cosmo Fleet Collection figure; the original's 66-unit wing
span is not kept, the official 305 x 210 m plan is.

Sources: ガンダムチャンネル 设定页 (gundam-c.com/manual/mechanic/0083/albion.html),
ガンダムWiki アルビオン (gundam.wiki.cre.jp), MegaHouse Cosmo Fleet Collection
Pegasus Class Albion product photos (en.megahobby.jp).
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'gold': (214, 170, 58, 255), 'rim': (44, 88, 178, 255), 'underside': (146, 156, 182, 255),
    'jet': (58, 74, 112, 255),
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


def wing(root, tip, chords, thick, color='hull', edge='red', edge_frac=0.3, name='wing'):
    """Tapered wing from the root to the tip leading edge (x, up, fwd); chords and
    thicknesses are (root, tip). The chord runs astern; the leading edge band is
    coloured separately."""
    (rx, ry, rz), (tx, ty, tz) = root, tip
    length = math.hypot(tx - rx, ty - ry)
    nx, ny = -(ty - ry) / length, (tx - rx) / length
    if ny < 0:
        nx, ny = -nx, -ny

    def half(f):  # half thickness along the chord: blunt nose, thickest at 30 %
        return 0.5 * (0.35 + 0.65 * min(1.0, f / 0.3)) * (1.0 - 0.75 * max(0.0, f - 0.3) / 0.7)

    def ring(p, chord, t, fractions):
        x, y, z = p
        top = [(x + nx * half(f) * t, y + ny * half(f) * t, z - f * chord) for f in fractions]
        bottom = [(x - nx * half(f) * t, y - ny * half(f) * t, z - f * chord) for f in reversed(fractions)]
        return top + bottom

    for fractions, col in (((0.0, 0.08, edge_frac), edge), ((edge_frac, 0.65, 1.0), color)):
        solid([ring(root, chords[0], thick[0], fractions), ring(tip, chords[1], thick[1], fractions)],
              col, bevel=0.03, name=name)


# ------------------------------------------------------------ forward hulls ("legs")
# MS decks: white upper hull, red lower hull, a wedge nose with the catapult exit.
LEG = [(5.0, 4.5, 2.6, -5.0), (30.0, 4.5, 2.6, -5.0), (34.0, 4.4, 2.2, -4.8),
       (39.0, 4.1, 1.2, -4.2), (42.0, 3.6, 0.2, -3.4)]
SPLIT = -2.0


def leg(side):
    xc = side * 9.3

    def upper(w, top):
        return [(xc - w, SPLIT), (xc + w, SPLIT), (xc + w, top - 0.9), (xc + 0.72 * w, top),
                (xc - 0.72 * w, top), (xc - w, top - 0.9)]

    def lower(w, bottom):
        return [(xc - 0.8 * w, bottom), (xc + 0.8 * w, bottom), (xc + w, bottom + 1.0), (xc + w, SPLIT + 0.02),
                (xc - w, SPLIT + 0.02), (xc - w, bottom + 1.0)]

    loft([(f, upper(w, t)) for f, w, t, b in LEG], 'hull', bevel=0.12, name='leg')
    loft([(f, lower(w, b)) for f, w, t, b in LEG], 'red', bevel=0.1, name='leg-red')
    # Blue-grey keel plate.
    box((xc, -5.02, 19.0), (6.4, 0.08, 26.0), 'underside')
    # MS catapult on the upper deck: dark track with a centre rail, running to the nose.
    loft([(10.0, [(xc - 1.1, 2.6), (xc + 1.1, 2.6), (xc + 1.1, 2.68), (xc - 1.1, 2.68)]),
          (30.0, [(xc - 1.1, 2.6), (xc + 1.1, 2.6), (xc + 1.1, 2.68), (xc - 1.1, 2.68)]),
          (34.0, [(xc - 1.05, 2.2), (xc + 1.05, 2.2), (xc + 1.05, 2.28), (xc - 1.05, 2.28)]),
          (39.0, [(xc - 1.0, 1.2), (xc + 1.0, 1.2), (xc + 1.0, 1.28), (xc - 1.0, 1.28)])], 'dark', name='catapult')
    box((xc, 2.72, 20.0), (0.18, 0.06, 20.0), 'yellow')
    # Hangar hatch hump and panel inserts on the deck.
    loft([(10.5, [(xc - 3.0, 2.5), (xc + 3.0, 2.5), (xc + 2.4, 3.5), (xc - 2.4, 3.5)]),
          (16.5, [(xc - 3.0, 2.5), (xc + 3.0, 2.5), (xc + 2.4, 3.5), (xc - 2.4, 3.5)])], 'hull', bevel=0.25, segments=3, name='hatch')
    box((xc, 3.52, 13.5), (3.6, 0.05, 4.6), 'panel')
    for f in (24.0, 28.0):
        box((xc - side * 2.4, 2.63, f), (1.4, 0.05, 2.6), 'panel')
    # Nose: dark catapult exit on the front face and two large missile launchers.
    box((xc, 0.0, 41.3), (4.4, 1.4, 1.2), 'dark', bevel=0.05)
    for s in (-1, 1):
        box((xc + s * 1.6, -2.4, 41.5), (1.1, 0.9, 0.9), 'nozzle')
    # Twin mega particle cannon under the catapult.
    box((xc, -4.8, 37.5), (2.4, 0.9, 3.0), 'dark', bevel=0.08)
    for s in (-1, 1):
        cylinder((xc + s * 0.5, -4.9, 38.8), (xc + s * 0.5, -4.9, 43.0), 0.22, 'dark', segments=10)
    # Red trim and a hangar door line on the outer flank.
    box((xc + side * 4.53, 0.3, 21.0), (0.06, 1.8, 7.0), 'panel')
    box((xc + side * 4.53, 1.4, 12.0), (0.06, 0.25, 12.0), 'red')


mirrored(leg)

# ------------------------------------------------------------ central body
# Between the legs: aircraft catapult openings forward, a raised spine up to the bridge.
loft([(20.0, [(-4.8, -3.0), (4.8, -3.0), (4.8, 1.4), (-4.8, 1.4)]),
      (18.0, [(-4.8, -3.6), (4.8, -3.6), (4.8, 2.3), (-4.8, 2.3)]),
      (-2.0, [(-4.8, -3.6), (4.8, -3.6), (4.8, 2.3), (-4.8, 2.3)])], 'hull', bevel=0.1, name='body')
mirrored(lambda s: box((s * 2.2, -1.2, 20.0), (3.2, 1.8, 0.2), 'dark'))
box((0, -3.62, 9.0), (9.4, 0.06, 20.0), 'underside')
loft([(17.0, [(-1.4, 2.2), (1.4, 2.2), (1.2, 2.9), (-1.2, 2.9)]),
      (13.0, [(-2.2, 2.2), (2.2, 2.2), (1.9, 3.8), (-1.9, 3.8)]),
      (5.0, [(-2.6, 2.2), (2.6, 2.2), (2.3, 4.4), (-2.3, 4.4)])], 'hull', bevel=0.12, name='spine')
box((0, 3.85, 10.0), (2.4, 0.06, 5.0), 'panel')

# ------------------------------------------------------------ mid-section and mirrors
# The widest block: legs, nacelles and both laser receiver pods meet here.
loft([(9.5, [(-14.0, -3.0), (14.0, -3.0), (14.4, 2.2), (-14.4, 2.2)]),
      (8.0, [(-14.6, -3.6), (14.6, -3.6), (14.6, 2.9), (-14.6, 2.9)]),
      (-2.0, [(-14.6, -3.6), (14.6, -3.6), (14.6, 2.9), (-14.6, 2.9)])], 'hull', bevel=0.14, name='mid')
box((0, -3.62, 3.5), (26.0, 0.06, 10.0), 'underside')


def mirror_pod(side):
    cy, cf = -0.3, 3.8
    cylinder((side * 13.8, cy, cf), (side * 19.0, cy, cf), 3.3, 'hull', segments=28, bevel=0.1)
    cylinder((side * 18.9, cy, cf), (side * 19.9, cy, cf), 3.45, 'rim', segments=28)
    cylinder((side * 19.8, cy, cf), (side * 20.25, cy, cf), 2.75, 'gold', segments=28, radius_b=2.3)
    # Receiver grill bars across the dish.
    for u in (-1.4, -0.7, 0.0, 0.7, 1.4):
        box((side * 20.12, cy + u, cf), (0.3, 0.1, 2.9 * math.sqrt(1 - (u / 2.3) ** 2)), 'deckline')
    # Vent row on top of the pod.
    for i in range(4):
        box((side * (14.8 + i * 1.05), cy + 3.3, cf + 0.6), (0.7, 0.3, 1.6), 'panel', bevel=0.04)


mirrored(mirror_pod)

# ------------------------------------------------------------ bridge
# Gundam-faced bridge on a tower amidships: chin grille, window band, red cheeks,
# red forehead crest and the yellow V antenna raked back over the head.
loft([(6.0, [(-3.0, 2.8), (3.0, 2.8), (2.7, 6.6), (-2.7, 6.6)]),
      (-3.0, [(-3.6, 2.8), (3.6, 2.8), (3.2, 7.0), (-3.2, 7.0)])], 'hull', bevel=0.14, name='tower')
box((0, 4.6, 6.05), (2.6, 2.4, 0.12), 'panel')
for u in (3.9, 4.6, 5.3):
    box((0, u, 6.12), (2.2, 0.16, 0.06), 'dark')
mirrored(lambda s: box((s * 2.95, 5.4, 4.6), (1.1, 1.8, 2.4), 'red', bevel=0.1))
loft([(5.0, [(-2.4, 6.6), (2.4, 6.6), (2.2, 8.9), (-2.2, 8.9)]),
      (4.0, [(-2.9, 6.6), (2.9, 6.6), (2.6, 9.8), (-2.6, 9.8)]),
      (-1.8, [(-3.1, 6.8), (3.1, 6.8), (2.7, 10.0), (-2.7, 10.0)])], 'hull', bevel=0.12, name='bridge')
box((0, 8.1, 4.98), (4.2, 0.6, 0.1), 'window')
mirrored(lambda s: box((s * 2.92, 8.2, 2.0), (0.1, 0.5, 3.4), 'window'))
box((0, 9.7, 4.3), (1.0, 0.9, 1.1), 'red', bevel=0.06)
mirrored(lambda s: KIT.plate([(s * 0.3, 10.5, 4.7), (s * 4.3, 13.0, 2.6), (s * 4.4, 12.6, 2.4), (s * 0.3, 9.3, 4.3)][::s],
                             0.2, 'yellow', bevel=0.04))
box((0, 10.05, 0.8), (3.4, 0.1, 3.4), 'panel')
cylinder((0, 10.0, -0.9), (0, 12.0, -0.9), 0.15, 'grey', segments=8)
box((0, 11.5, -0.9), (2.2, 0.18, 0.22), 'grey')
sphere((0, 10.5, -1.2), 0.6, 'panel', segments=12)

# ------------------------------------------------------------ engine nacelles


def nacelle(side):
    xc = side * 9.5

    def ring(w, bottom, top):
        return [(xc - 0.8 * w, bottom), (xc + 0.8 * w, bottom), (xc + w, bottom + 1.0), (xc + w, top - 1.0),
                (xc + 0.75 * w, top), (xc - 0.75 * w, top), (xc - w, top - 1.0), (xc - w, bottom + 1.0)]

    loft([(4.0, ring(3.4, -3.4, 2.9)), (-2.0, ring(3.6, -4.4, 4.7)), (-15.0, ring(3.8, -4.6, 5.4)),
          (-18.2, ring(3.6, -4.2, 5.0))], 'hull', bevel=0.16, name='nacelle')
    box((xc, -4.62, -9.0), (5.4, 0.06, 15.0), 'underside')
    box((xc, 5.42, -9.5), (4.2, 0.06, 9.0), 'panel')
    box((xc + side * 3.82, 0.4, -9.0), (0.06, 0.4, 13.0), 'red')
    # Nozzle cluster: dark housing, seven nozzles (one centre, six around).
    cylinder((xc, 0.4, -17.8), (xc, 0.4, -20.2), 3.15, 'dark', segments=28, radius_b=3.0, bevel=0.08)
    for k in range(7):
        a = k * math.pi / 3
        x, y = (xc, 0.4) if k == 6 else (xc + 1.85 * math.cos(a), 0.4 + 1.85 * math.sin(a))
        cylinder((x, y, -19.9), (x, y, -20.7), 0.82, 'jet', segments=14, cap=False)
        cylinder((x, y, -20.1), (x, y, -20.2), 0.8, 'glow', segments=14)
    # Horizontal stabiliser on the outer flank.
    wing((side * 13.1, 0.4, -9.5), (side * 18.6, 0.2, -14.0), (6.0, 2.8), (0.5, 0.25),
         color='hull', edge='panel', name='stabiliser')
    # Dorsal and ventral fins, canted outward.
    wing((side * 10.6, 5.2, -9.0), (side * 12.4, 9.4, -14.6), (6.5, 2.6), (0.45, 0.22),
         color='hull', edge='panel', name='dorsal-fin')
    wing((side * 10.8, -4.4, -10.5), (side * 13.0, -8.4, -15.0), (5.5, 2.4), (0.45, 0.22),
         color='hull', edge='red', name='ventral-fin')


mirrored(nacelle)

# Central aft block between the nacelles: aft deck on top, main nozzles on the stern face.
loft([(-2.0, [(-5.9, -3.3), (5.9, -3.3), (5.9, 3.4), (-5.9, 3.4)]),
      (-15.5, [(-5.9, -3.0), (5.9, -3.0), (5.9, 3.0), (-5.9, 3.0)])], 'hull', bevel=0.12, name='aft')
box((0, 3.3, -9.0), (7.0, 0.3, 11.0), 'grey', bevel=0.05)
for f in (-5.0, -8.0, -11.0):
    box((0, 3.48, f), (6.4, 0.05, 0.14), 'grey')
for x, y in ((-2.6, 1.4), (2.6, 1.4), (-2.6, -1.5), (2.6, -1.5), (0.0, 0.0)):
    cylinder((x, y, -15.0), (x, y, -16.9), 1.35, 'dark', segments=18, radius_b=1.25, bevel=0.05)
    cylinder((x, y, -16.8), (x, y, -17.0), 1.0, 'glow', segments=16)

# ------------------------------------------------------------ variable wings
# Four wings in an X: the upper pair from the nacelle shoulders, the lower pair
# from under the mirror pods; white with a red leading edge.
mirrored(lambda s: wing((s * 12.6, 4.2, -0.5), (s * 26.5, 13.6, -12.5), (7.5, 3.2), (0.75, 0.35), name='upper-wing'))
mirrored(lambda s: wing((s * 13.2, -3.0, 3.0), (s * 24.5, -10.4, -7.5), (7.0, 3.0), (0.7, 0.35), name='lower-wing'))
mirrored(lambda s: box((s * 26.6, 13.7, -14.2), (0.5, 0.5, 2.6), 'dark'))
mirrored(lambda s: box((s * 24.6, -10.5, -9.0), (0.5, 0.5, 2.4), 'dark'))

# ------------------------------------------------------------ twin mega particle cannons (hull sides)


def main_gun(side):
    x = side * 14.9
    box((x, -1.6, 12.5), (1.6, 1.6, 4.0), 'dark', bevel=0.08)
    for s in (-1, 1):
        cylinder((x, -1.6 + s * 0.4, 14.3), (x, -1.6 + s * 0.4, 18.0), 0.22, 'dark', segments=10)


mirrored(main_gun)

# ------------------------------------------------------------ twin laser cannons (18)


def laser(x, up, fwd, facing=1):
    box((x, up + 0.2, fwd), (0.8, 0.4, 0.8), 'grey', bevel=0.04)
    for s in (-1, 1):
        cylinder((x + s * 0.16, up + 0.3, fwd), (x + s * 0.16, up + 0.3, fwd + facing * 1.1), 0.06, 'dark', segments=6)


LASERS = [(s * 9.3 + s * 2.3, 2.6, f) for s in (-1, 1) for f in (30.5, 22.0)]      # forward, leg decks
LASERS += [(s * 9.3 - s * 2.6, 2.6, 27.0) for s in (-1, 1)]                        # leg decks, inboard
LASERS += [(s * 11.0, 2.9, f) for s in (-1, 1) for f in (7.0, -0.8)]               # mid-section
LASERS += [(s * 9.5, 5.4, f) for s in (-1, 1) for f in (-4.0, -15.0)]              # nacelle tops
LASERS += [(s * 3.2, 2.3, 16.5) for s in (-1, 1)]                                  # central body
LASERS += [(s * 4.2, 3.4, -13.5) for s in (-1, 1)]                                 # aft block
assert len(LASERS) == 18, len(LASERS)
for x, up, f in LASERS:
    laser(x, up, f, facing=1 if f > -10 else -1)

KIT.export(OUT, 'Albion', previews=PREVIEWS, reference=5584)
