"""Author the HD Ra Cailum (world-map ship, original resource 5591) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/ra_cailum_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z bow, +Y up,
units of the N64 mesh, 76 units = 487 m), model.glb and optional previews with the
original rendered from the same cameras (compare.png).

Design reference: Ra Cailum-class battleship from Char's Counterattack
(mechanical design Shoichi Masuo). Official figures used: length 487 m, beam
165 m; four twin mega particle turrets (three forward, one aft); six bow missile
tubes; 22 AA mounts; one launch catapult on each side and an aft landing deck;
two bridges, the combat bridge directly beneath the regular one; an engine
block with long radiator fins. Proportions follow side, top and rear views of
the licensed Cosmo Fleet Special figure.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, P, args  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit()
loft, box, cylinder, sphere, mirrored, add_object = KIT.loft, KIT.box, KIT.cylinder, KIT.sphere, KIT.mirrored, KIT.add_object


# ---------------------------------------------------------------- forward hull
# Salamis-style wedge bow: flat deck, chined sides, narrow keel, forked stem.
def hull_section(hw, top, bottom, chine):
    return [(-hw * 0.55, bottom), (hw * 0.55, bottom), (hw, chine), (hw * 0.92, top),
            (-hw * 0.92, top), (-hw, chine)]


BOW = [(36.0, 3.4, 1.5, -0.2, 0.5), (32.0, 4.1, 2.0, -1.6, 0.4), (24.0, 4.7, 2.4, -3.0, 0.0),
       (12.0, 5.3, 2.7, -3.7, -0.4), (0.0, 6.0, 3.0, -4.0, -0.6), (-9.5, 6.6, 3.2, -4.1, -0.8)]
loft([(f, hull_section(hw, t, b, c)) for f, hw, t, b, c in BOW], 'hull', bevel=0.18, name='bow')
# Forked stem: two prongs either side of a recessed slot.
mirrored(lambda s: box((s * 2.35, 0.9, 36.9), (1.5, 1.6, 2.2), 'hull', bevel=0.12, taper=(0.8, 0.7)))
box((0, 0.6, 35.9), (2.6, 1.2, 0.4), 'dark')
# Bow skirts: side walls hanging below the deck plate, giving the U section seen from the bow.
mirrored(lambda s: loft([(34.5, [(s * 3.2, -0.9), (s * 3.55, -0.9), (s * 3.55, 1.0), (s * 3.2, 1.0)]),
                         (20.0, [(s * 4.4, -3.2), (s * 4.85, -3.2), (s * 4.85, 1.9), (s * 4.4, 1.9)])],
                        'panel', bevel=0.08, name='skirt'))
# Six bow missile tubes (three per side) and deck plating.
for s in (-1, 1):
    for i in range(3):
        box((s * 3.62, 0.45, 31.0 - i * 1.6), (0.12, 0.7, 1.0), 'dark')
for i, f in enumerate((29.0, 22.0, 15.0)):
    box((0, 2.52 + 0.02 * i, f), (5.2 - i * 0.2, 0.06, 4.8), 'panel', bevel=0.02)
box((0, 2.48, 33.0), (3.6, 0.06, 2.4), 'panel')
# Keel strake under the bow.
loft([(33.0, [(-0.5, -0.4), (0.5, -0.4), (0.5, 0.2), (-0.5, 0.2)]),
      (6.0, [(-0.9, -4.6), (0.9, -4.6), (0.9, -3.4), (-0.9, -3.4)])], 'grey', bevel=0.08, name='keel')

# ------------------------------------------------------------ turret deck
loft([(19.0, [(-2.4, 2.6), (2.4, 2.6), (2.6, 3.1), (-2.6, 3.1)]),
      (14.0, [(-3.4, 2.8), (3.4, 2.8), (3.4, 4.0), (-3.4, 4.0)]),
      (-9.0, [(-4.2, 3.0), (4.2, 3.0), (4.2, 4.6), (-4.2, 4.6)])], 'hull', bevel=0.12, name='turret-deck')
box((0, 4.62, 2.0), (3.2, 0.06, 16.0), 'panel')


def turret(fwd, up, facing=1, under=False, raised=0.0):
    sign = -1 if under else 1
    base = up + sign * raised
    cylinder((0, up, fwd), (0, base + sign * 0.45, fwd), 1.35, 'grey', segments=20, bevel=0.05)
    hy = base + sign * 0.95
    # Housing: sloped glacis toward the barrels.
    front, back = fwd + facing * 1.4, fwd - facing * 1.3
    loft([(back, [(-1.2, hy - 0.5), (1.2, hy - 0.5), (1.2, hy + 0.5), (-1.2, hy + 0.5)]),
          (front, [(-1.05, hy - 0.5), (1.05, hy - 0.5), (0.9, hy + 0.1), (-0.9, hy + 0.1)])]
         if facing > 0 else
         [(front, [(-1.05, hy - 0.5), (1.05, hy - 0.5), (0.9, hy + 0.1), (-0.9, hy + 0.1)]),
          (back, [(-1.2, hy - 0.5), (1.2, hy - 0.5), (1.2, hy + 0.5), (-1.2, hy + 0.5)])],
         'navy', bevel=0.1, name='turret')
    for s in (-1, 1):
        start = (s * 0.45, hy - 0.1, fwd + facing * 0.9)
        end = (s * 0.45, hy - 0.1, fwd + facing * 4.4)
        cylinder(start, end, 0.2, 'navy', segments=12, radius_b=0.17)
        cylinder((s * 0.45, hy - 0.1, fwd + facing * 4.3), (s * 0.45, hy - 0.1, fwd + facing * 4.75), 0.24, 'dark', segments=12)


turret(11.5, 4.0)                  # forward dorsal
turret(4.8, 4.5, raised=0.5)       # superfiring dorsal
turret(23.5, -3.3, under=True)     # ventral, under the bow

# ------------------------------------------------------------ catapults
def catapult(side):
    aft, fore = (side * 7.5, -9.5), (side * 11.8, 6.5)
    def ring(x, f, w=1.25):
        return (f, [(x - w, 0.2), (x + w, 0.2), (x + w, 1.55), (x - w, 1.55)])
    loft([ring(*aft), ring(*fore)], 'panel', bevel=0.1, name='catapult')
    # Orange launch deck with a darker centre rail.
    def deck(x, f, w, up):
        return (f, [(x - w, up - 0.04), (x + w, up - 0.04), (x + w, up), (x - w, up)])
    loft([deck(aft[0], aft[1] + 0.3, 1.1, 1.62), deck(fore[0], fore[1] - 0.2, 1.1, 1.62)], 'orange', name='deck')
    loft([deck(aft[0], aft[1] + 0.3, 0.12, 1.66), deck(fore[0], fore[1] - 0.2, 0.12, 1.66)], 'deckline', name='rail')
    for t in (0.2, 0.5, 0.8):
        x = aft[0] + (fore[0] - aft[0]) * t
        f = aft[1] + (fore[1] - aft[1]) * t
        box((x, 1.65, f), (2.2, 0.04, 0.12), 'deckline')
    # Launch mouth and support struts back to the hull.
    box((fore[0], 0.85, fore[1] + 0.05), (2.3, 1.0, 0.12), 'dark')
    for t in (0.25, 0.7):
        x = aft[0] + (fore[0] - aft[0]) * t
        f = aft[1] + (fore[1] - aft[1]) * t
        hx = 6.2 - t * 0.9
        loft([(f - 0.6, [(side * hx, -0.6), (x - side * 0.2, 0.2), (x - side * 0.2, 0.9), (side * hx, 0.4)]),
              (f + 0.6, [(side * hx, -0.6), (x - side * 0.2, 0.2), (x - side * 0.2, 0.9), (side * hx, 0.4)])],
             'grey', name='strut')


mirrored(catapult)

# ------------------------------------------------------------ aft block
AFT = [(-9.5, 7.2, -4.2, 5.4), (-11.0, 7.6, -4.4, 6.2), (-27.0, 7.8, -4.4, 6.6), (-29.5, 7.4, -4.0, 6.2)]
loft([(f, [(-hw, b), (hw, b), (hw, t), (hw - 1.0, t + 0.5), (-hw + 1.0, t + 0.5), (-hw, t)]) for f, hw, b, t in AFT],
     'hull', bevel=0.2, name='aft')
# Red collar where the hull meets the engine block, and red trims.
loft([(-9.0, [(-7.35, -4.35), (7.35, -4.35), (7.35, 5.55), (6.4, 6.05), (-6.4, 6.05), (-7.35, 5.55)]),
      (-11.2, [(-7.75, -4.55), (7.75, -4.55), (7.75, 6.35), (6.75, 6.85), (-6.75, 6.85), (-7.75, 6.35)])],
     'panel', bevel=0.06, name='collar')
mirrored(lambda s: box((s * 7.8, 1.2, -10.1), (0.12, 7.6, 2.0), 'red'))
mirrored(lambda s: box((s * 5.2, 6.62, -10.2), (2.4, 0.12, 1.8), 'red'))
# Side shoulders (the widest part of the hull) with the navy capsule row.
def shoulder(side):
    x0, x1 = side * 7.2, side * 11.3
    lo, hi = -0.2, 4.6
    loft([(-13.5, [(min(x0, x1), lo + 0.6), (max(x0, x1), lo + 0.6), (max(x0, x1), hi - 0.6), (min(x0, x1), hi - 0.6)]),
          (-15.0, [(min(x0, x1), lo), (max(x0, x1), lo), (max(x0, x1), hi), (min(x0, x1), hi)]),
          (-29.0, [(min(x0, x1), lo), (max(x0, x1), lo), (max(x0, x1), hi), (min(x0, x1), hi)])],
         'hull', bevel=0.16, name='shoulder')
    box((side * 9.25, lo - 0.08, -22.0), (4.2, 0.16, 14.0), 'red')
    for i in range(4):
        f = -16.6 - i * 3.6
        cylinder((side * 11.0, 2.2, f), (side * 11.85, 2.2, f), 1.5, 'navy', segments=20, bevel=0.12)
        cylinder((side * 11.84, 2.2, f), (side * 11.95, 2.2, f), 0.6, 'dark', segments=12)
    box((side * 9.25, hi + 0.02, -21.5), (3.6, 0.05, 12.0), 'panel')


mirrored(shoulder)
# Hull plating and hangar doors on the aft flanks.
mirrored(lambda s: box((s * 7.72, 1.0, -12.4), (0.06, 3.2, 1.8), 'dark'))
box((0, 7.0, -20.0), (9.0, 0.06, 13.0), 'panel')
for f in (-14.0, -18.0, -22.0, -26.0):
    box((0, 7.05, f), (10.5, 0.05, 0.12), 'grey')

# ------------------------------------------------------------ bridge
loft([(-14.2, [(-2.9, 7.0), (2.9, 7.0), (2.4, 9.4), (-2.4, 9.4)]),
      (-22.5, [(-3.1, 7.0), (3.1, 7.0), (2.7, 9.4), (-2.7, 9.4)])], 'hull', bevel=0.12, name='tower')
# Combat bridge (lower) and main bridge (upper), each with a dark window band.
loft([(-13.6, [(-2.6, 7.6), (2.6, 7.6), (2.6, 8.4), (-2.6, 8.4)]),
      (-15.6, [(-2.8, 7.4), (2.8, 7.4), (2.8, 8.6), (-2.8, 8.6)])], 'hull', bevel=0.08, name='combat-bridge')
box((0, 8.05, -13.55), (4.8, 0.35, 0.1), 'window')
loft([(-14.8, [(-3.3, 9.4), (3.3, 9.4), (3.1, 10.3), (-3.1, 10.3)]),
      (-18.8, [(-3.5, 9.3), (3.5, 9.3), (3.3, 10.5), (-3.3, 10.5)])], 'hull', bevel=0.1, name='bridge')
box((0, 9.95, -14.75), (6.0, 0.4, 0.1), 'window')
mirrored(lambda s: box((s * 3.42, 9.95, -16.5), (0.1, 0.4, 2.8), 'window'))
box((0, 10.55, -17.0), (4.2, 0.12, 2.6), 'grey')
# Mast, sensor bar (yellow in the reference art) and antennas.
cylinder((0, 10.5, -18.2), (0, 12.6, -18.2), 0.28, 'grey', segments=10)
box((0, 12.1, -18.2), (3.6, 0.28, 0.3), 'yellow', bevel=0.04)
mirrored(lambda s: cylinder((s * 1.6, 12.2, -18.2), (s * 1.6, 13.1, -18.2), 0.07, 'grey', segments=6))
sphere((0, 11.1, -19.4), 0.55, 'panel', segments=14)

turret(-24.8, 6.9, facing=-1)      # aft dorsal, trained astern

# ------------------------------------------------------------ engines
box((0, 3.3, -27.0), (6.4, 4.6, 5.0), 'hull', bevel=0.18)
box((0, 1.1, -29.52), (6.6, 0.3, 0.2), 'red')
for s in (-1, 1):
    x = s * 1.45
    cylinder((x, 3.4, -29.2), (x, 3.4, -31.0), 1.2, 'red', segments=20, radius_b=1.28, cap=False)
    cylinder((x, 3.4, -29.3), (x, 3.4, -30.9), 0.95, 'nozzle', segments=20, cap=False)
    cylinder((x, 3.4, -29.75), (x, 3.4, -29.85), 0.95, 'glow', segments=20)


def nacelle(side):
    cx = side * 9.3
    # Rounded red nose, grey body, three nozzles astern.
    sphere((cx, -1.9, -15.4), 1.9, 'panel', scale=(1.12, 1.05, 1.6), segments=20)
    cylinder((cx, -1.9, -15.9), (cx, -1.9, -16.5), 2.2, 'red', segments=24)
    loft([(-15.4, [(cx - 2.1, -3.9), (cx + 2.1, -3.9), (cx + 2.1, 0.1), (cx - 2.1, 0.1)]),
          (-29.8, [(cx - 2.2, -4.0), (cx + 2.2, -4.0), (cx + 2.2, 0.2), (cx - 2.2, 0.2)])], 'panel', bevel=0.35, segments=3, name='nacelle')
    box((cx, -0.05, -24.0), (4.5, 0.25, 11.0), 'red')
    for i in (-1, 0, 1):
        x = cx + i * 1.45
        cylinder((x, -1.95, -29.6), (x, -1.95, -31.4), 0.72, 'red', segments=18, radius_b=0.78, cap=False)
        cylinder((x, -1.95, -29.7), (x, -1.95, -31.3), 0.55, 'nozzle', segments=18, cap=False)
        cylinder((x, -1.95, -30.0), (x, -1.95, -30.1), 0.55, 'glow', segments=18)


mirrored(nacelle)

# Aft landing deck: orange tongue between the nacelles, framed in grey.
box((0, -3.2, -32.0), (4.8, 0.6, 7.0), 'grey', bevel=0.08)
box((0, -2.88, -32.1), (4.0, 0.06, 6.6), 'orange')
box((0, -2.84, -32.1), (0.14, 0.04, 6.4), 'deckline')
for f in (-30.0, -32.0, -34.0):
    box((0, -2.84, f), (3.8, 0.04, 0.1), 'deckline')
box((0, -3.4, -28.8), (4.4, 1.6, 0.4), 'dark')

# Swept stabiliser fins with anhedral at the stern.
def fin(side):
    root_lo = (side * 11.0, 0.8)
    tip = (side * 14.6, -2.6)
    bm = bmesh.new()
    pts = [P(root_lo[0], root_lo[1], -22.5), P(root_lo[0], root_lo[1], -29.0),
           P(tip[0], tip[1], -39.5), P(tip[0], tip[1], -36.5)]
    top = [p + Vector((0, 0, 0.35)) for p in pts]
    a = [bm.verts.new(p) for p in pts]
    b = [bm.verts.new(p) for p in top]
    bm.faces.new(a[::-1]); bm.faces.new(b)
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((a[i], a[j], b[j], b[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    add_object(bm, 'panel', bevel=0.05, name='fin')
    box((side * 13.2, -1.0, -33.5), (0.5, 0.5, 1.2), 'red')


mirrored(fin)

# ------------------------------------------------------------ radiator fins
def radiator(side):
    x = side * 6.8
    top, bottom = -4.35, -8.3
    fore, aft = 8.0, -26.0
    splay = side * 0.7
    loft([(aft, [(x - 0.18, top), (x + 0.18, top), (x + splay + 0.18, bottom), (x + splay - 0.18, bottom)]),
          (fore - 1.5, [(x - 0.18, top), (x + 0.18, top), (x + splay + 0.18, bottom), (x + splay - 0.18, bottom)]),
          (fore, [(x - 0.14, top), (x + 0.14, top), (x + splay + 0.14, bottom + 1.2), (x + splay - 0.14, bottom + 1.2)])],
         'panel', bevel=0.05, name='radiator')
    # Cooling ribs on both faces.
    for i in range(24):
        f = aft + 1.0 + i * (fore - aft - 3.0) / 23
        for face in (-1, 1):
            loft([(f - 0.12, [(x + face * 0.16, top - 0.1), (x + face * 0.3, top - 0.1),
                              (x + splay + face * 0.3, bottom + 0.15), (x + splay + face * 0.16, bottom + 0.15)]),
                  (f + 0.12, [(x + face * 0.16, top - 0.1), (x + face * 0.3, top - 0.1),
                              (x + splay + face * 0.3, bottom + 0.15), (x + splay + face * 0.16, bottom + 0.15)])],
                 'grey', name='rib')
    for f in (4.0, -10.0, -22.0):
        box((x, -4.3, f), (0.6, 0.5, 1.4), 'grey')


mirrored(radiator)

# ------------------------------------------------------------ AA mounts (22)
def aa(x, up, fwd, facing=1):
    box((x, up + 0.18, fwd), (0.7, 0.36, 0.7), 'grey', bevel=0.04)
    for s in (-1, 1):
        cylinder((x + s * 0.14, up + 0.26, fwd), (x + s * 0.14, up + 0.26, fwd + facing * 1.0), 0.06, 'dark', segments=6)


AA = [(s * 3.2, 3.1, f) for s in (-1, 1) for f in (17.0, 9.0, 1.0, -5.0)]
AA += [(s * 6.4, 5.7, f) for s in (-1, 1) for f in (-12.8, -26.5)]
AA += [(s * 9.2, 4.6, f) for s in (-1, 1) for f in (-16.0, -27.5)]
AA += [(s * 4.4, 2.45, f) for s in (-1, 1) for f in (27.0,)]
AA += [(s * 4.0, 9.4, -20.8) for s in (-1, 1)]
AA += [(s * 2.2, 6.7, -28.5) for s in (-1, 1)]
assert len(AA) == 22, len(AA)
for x, up, f in AA:
    aa(x, up, f, facing=1 if f > -20 else -1)

KIT.export(OUT, 'Ra Cailum', previews=PREVIEWS, reference=5591)
