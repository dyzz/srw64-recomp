"""Author the HD Barge (world-map fortress, original resource 5596, part 0) in Blender.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/models/barge_blender.py -- OUTPUT_DIRECTORY [--previews]

Writes mesh.json (triangles in the original model's local frame: +Z toward the
space port, +Y up, units of the N64 mesh), model.glb and optional previews with
the original part 0 rendered from the same cameras (compare.png). Part 1 of the
resource, the バルジ name plate, stays original and is not modelled here.

Design reference: OZ space fortress Barge (宇宙要塞バルジ) from Mobile Suit Gundam
Wing, original mechanical design Hajime Katoki. Official description (Gundam
Channel mechanic manual): a thick disc-shaped gravity block with a cylindrical
engine block through its centre and a rectangular non-rotating section across
the front. Katoki's labelled line art adds: the space port and hangar in a
cylinder at the front centre, the military section at the top of the front
section, twelve agricultural plants (round pods, three at each end of the front
section on either side), two beam cannons at the bottom, an artificial-gravity
block of about 1/6 G, and six rocket engines at the stern; long stays run from
booms over the disc back to the engine block. Overall length 8.0 km (MAHQ).
Colours follow the TV series (blue-grey steel) and the G Generation Cross Rays
render of the fortress (dark blue pod faces). The original spans 57 units along
Z, so one unit is roughly 140 m.

Sources:
  https://www.gundam-c.com/manual/mechanic/w/bulge.html
  https://www.mahq.net/barge/ (specifications, Katoki line art)
  https://gundam.fandom.com/wiki/Bulge
Reference images were only viewed for comparison; none are in the repository.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_kit import Kit, args  # noqa: E402

OUT, PREVIEWS = args()
KIT = Kit(colors={
    'steel': (160, 168, 186, 255),     # blue-grey hull (TV colours)
    'light': (198, 204, 214, 255),
    'mid': (122, 130, 150, 255),
    'deep': (84, 90, 108, 255),
    'pod': (36, 52, 92, 255),          # agricultural plant faces
    'lamp': (236, 204, 120, 255),
})
box, cylinder, sphere, mirrored = KIT.box, KIT.cylinder, KIT.sphere, KIT.mirrored


def polar(r, a, fwd):
    return (r * math.cos(a), r * math.sin(a), fwd)


# ---------------------------------------------------------------- gravity block
# Thick rotating disc: two raised rim bands split by a groove, plated faces.
cylinder((0, 0, 3.6), (0, 0, 15.8), 14.5, 'mid', segments=48, bevel=0.2)
cylinder((0, 0, 4.2), (0, 0, 9.2), 15.0, 'steel', segments=64, bevel=0.2)
cylinder((0, 0, 10.2), (0, 0, 15.2), 15.0, 'steel', segments=64, bevel=0.2)
for i in range(32):
    a = 2 * math.pi * (i + 0.5) / 32
    for f0, f1 in ((4.3, 9.1), (10.3, 15.1)):
        cylinder(polar(15.02, a, f0), polar(15.02, a, f1), 0.22, 'deep', segments=4)
    if i % 2 == 0:  # window lights along the groove
        cylinder(polar(14.55, a, 9.5), polar(14.55, a, 9.9), 0.35, 'lamp', segments=6)
# Faces: a stepped ring at the front, a plated cone stepping down to the engine block aft.
cylinder((0, 0, 15.6), (0, 0, 16.4), 12.0, 'light', segments=48, bevel=0.15)
cylinder((0, 0, 4.0), (0, 0, 2.6), 12.5, 'light', segments=48, bevel=0.15, radius_b=10.0)
cylinder((0, 0, 2.7), (0, 0, -1.5), 9.2, 'steel', segments=40, bevel=0.15, radius_b=7.2)
for i in range(8):
    a = 2 * math.pi * i / 8 + math.pi / 8
    box(polar(13.2, a, 16.5), (1.6, 1.6, 0.4), 'deep')
# Radial plate seams on both disc faces.
for i in range(16):
    a = 2 * math.pi * i / 16
    cylinder(polar(5.2, a, 16.45), polar(11.8, a, 16.45), 0.16, 'mid', segments=4)
    cylinder(polar(12.6, a, 15.85), polar(14.4, a, 15.85), 0.2, 'deep', segments=4)
    cylinder(polar(12.6, a, 3.55), polar(14.4, a, 3.55), 0.2, 'deep', segments=4)

# ---------------------------------------------------------------- engine block
# Long cylindrical block through the disc: six rocket engines under ribbed
# casings, wide clamp bands, the stern collar carrying six nozzles.
cylinder((0, 0, -1.0), (0, 0, -29.0), 6.3, 'steel', segments=40, bevel=0.15)
cylinder((0, 0, 0.5), (0, 0, -3.0), 7.4, 'mid', segments=48, bevel=0.2, radius_b=6.6)
for f in (-6.2, -15.4):
    cylinder((0, 0, f + 1.3), (0, 0, f - 1.3), 6.8, 'mid', segments=48, bevel=0.15)
    for d in (-1.3, 1.3):
        cylinder((0, 0, f + d + 0.2), (0, 0, f + d - 0.2), 6.9, 'light', segments=40)
for i in range(6):
    a = 2 * math.pi * i / 6 + math.pi / 6
    cylinder(polar(6.3, a, -1.5), polar(6.3, a, -25.0), 0.95, 'light', segments=12, bevel=0.05)
    cylinder(polar(6.2, a + 0.52, -3.5), polar(6.2, a + 0.52, -24.5), 0.35, 'deep', segments=8)
# Stern collar with four brackets for the stays, six nozzles in its aft face.
cylinder((0, 0, -24.6), (0, 0, -29.2), 7.7, 'light', segments=48, bevel=0.25)
cylinder((0, 0, -26.4), (0, 0, -27.4), 7.8, 'mid', segments=48)
cylinder((0, 0, -29.1), (0, 0, -29.5), 7.0, 'deep', segments=48)
for a in (math.pi / 2, -math.pi / 2, 0.0, math.pi):
    c, s = math.cos(a), math.sin(a)
    box((c * 8.4, s * 8.4, -26.5), (2.2 if s else 2.4, 2.4 if s else 2.2, 3.6), 'mid', bevel=0.12)
for i in range(6):
    a = 2 * math.pi * i / 6
    cylinder(polar(4.3, a, -29.3), polar(4.3, a, -33.4), 1.6, 'mid', segments=20, radius_b=2.0, cap=False)
    cylinder(polar(4.3, a, -29.4), polar(4.3, a, -33.3), 1.35, 'nozzle', segments=20, radius_b=1.75, cap=False)
    cylinder(polar(4.3, a, -30.0), polar(4.3, a, -30.1), 1.4, 'glow', segments=20)
cylinder((0, 0, -29.3), (0, 0, -31.0), 1.6, 'mid', segments=16, bevel=0.1)
# Diagonal braces from the disc's aft face onto the first clamp band.
for i in range(4):
    a = math.pi / 4 + i * math.pi / 2
    cylinder(polar(11.0, a, 3.0), polar(6.9, a, -5.4), 0.6, 'mid', segments=8, bevel=0.05)

# ---------------------------------------------------------------- front section
# Non-rotating rectangular section across the front of the disc.
box((0, 0, 18.3), (6.6, 25.0, 4.6), 'light', bevel=0.25)
for s in (-1, 1):
    # Military section (top) and the lower block with the beam cannons.
    box((0, s * 16.6, 18.4), (8.6, 8.8, 6.2), 'light', bevel=0.3)
    box((0, s * 21.0, 18.0), (6.4, 1.2, 4.8), 'steel', bevel=0.2)
    for k in range(3):
        box((0, s * (14.2 + k * 2.2), 21.53), (7.0, 0.45, 0.1), 'window')
    mirrored(lambda sx: box((sx * 4.35, s * 16.6, 18.4), (0.12, 6.0, 4.2), 'deep'))
    # Agricultural plants: three pods each side at both ends of the section.
    box((0, s * 18.8, 17.6), (24.8, 2.6, 2.6), 'mid', bevel=0.15)
    for sx in (-1, 1):
        for k in range(3):
            x = sx * (6.3 + k * 2.95)
            cylinder((x, s * 18.8, 15.6), (x, s * 18.8, 19.8), 1.4, 'light', segments=18)
            cylinder((x, s * 18.8, 19.75), (x, s * 18.8, 19.95), 1.0, 'pod', segments=12)
            cylinder((x, s * 18.8, 15.65), (x, s * 18.8, 15.45), 1.0, 'pod', segments=12)
    # Boom running aft over the disc to the stay anchor.
    box((0, s * 18.2, 6.5), (3.6, 3.4, 18.0), 'steel', bevel=0.2, taper=(1.0, 1.15))
    box((0, s * 18.0, -3.0), (5.0, 4.4, 4.0), 'mid', bevel=0.2)
    box((0, s * 20.3, 6.0), (2.2, 0.1, 14.0), 'light')
    # Stays from the anchor to the stern collar brackets.
    for sx in (-1.6, 0.0, 1.6):
        cylinder((sx, s * 17.4, -4.8), (sx * 0.8, s * 9.2, -26.0), 0.22, 'deep', segments=6)
    # Triangular brace between the boom and the front section.
    KIT.plate([(0.7 * s, s * 12.5, 15.4), (0.7 * s, s * 16.4, 15.4), (0.7 * s, s * 16.4, 8.0)], 1.4, 'mid', name='brace')

# Twin beam cannons under the lower block, trained forward.
mirrored(lambda sx: cylinder((sx * 2.2, -20.6, 18.0), (sx * 2.2, -20.6, 22.6), 0.55, 'mid', segments=12, bevel=0.04))
mirrored(lambda sx: cylinder((sx * 2.2, -20.6, 22.5), (sx * 2.2, -20.6, 22.9), 0.4, 'nozzle', segments=12))

# Space port and hangar: the cylinder at the front centre with a square port.
cylinder((0, 0, 15.8), (0, 0, 21.8), 4.8, 'light', segments=40, bevel=0.2)
cylinder((0, 0, 21.6), (0, 0, 23.0), 5.1, 'steel', segments=40, bevel=0.2)
for f in (17.4, 19.6):
    cylinder((0, 0, f - 0.25), (0, 0, f + 0.25), 4.95, 'mid', segments=40)
box((0, 0, 23.02), (4.0, 4.0, 0.1), 'dark')
box((0, 0, 22.95), (4.8, 4.8, 0.1), 'mid')
box((0, 0, 23.06), (2.6, 0.5, 0.1), 'lamp')

KIT.export(OUT, 'Barge', previews=PREVIEWS, reference=5596, reference_parts=[0])
