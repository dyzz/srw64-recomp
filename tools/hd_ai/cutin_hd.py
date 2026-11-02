"""HD frames of the battle cut-ins and the units' props, by the settled local ESRGAN recipe.

The close-up cut-ins are battle_scenes entries 984-1038 (CUTIN_REGISTRY): ordinary
actors, drawn by the scene sprite engine like the units, so the host replaces them
the same way, one whole HD picture per (scene, atlas, palette, frame) over the parts
the frame draws (native_sprite.cpp; docs/design/battle-animation-rendering.md §6.3).
Every frame the scenes step through is rendered, cut to its parts' box, and run through
the unit poses' recipe (esrgan_pose.py: 4x-UltraSharpV2 and 4x-PixelPerfectV4 at 8x,
alpha through the model separately, 50/50 blend). Identical frames run once.

The props are the other images in the units' atlases (rifles, sabres, shields, fists,
missiles, the odd flame or flash): every battle_scenes entry drawn from a unit pose's
atlas that is neither a pose, nor one of the extra images that unit_extra_derive.py and
unit_extra_imagegen.py rebuild from the pose (unit-extra-analysis.json), nor a cut-in.
They are replaced the same way and share the pack (`battle_sprites`).

The shields are the units' shield defence pictures (battle unit record +2..4, one triplet
per unit, loaded by func_801C4574 for reaction 0x11), mostly in atlases of their own; the
defence effects are what the same function loads for the other reactions (barriers, a
parry's slash, a shoot-down), two of them drawn in the attacker's colours (`any_palette`);
the reaction actors are what the reaction bank itself puts on (the light a barrier
gathers, the afterimage, the decoy).
The brush characters are the special moves' calligraphy (atlas 4519); the map movies are
the combining and changing sequences played on the map (802176A8); the explosions are what
a unit shot down goes up in (battle_scenes 1042-1045, docs/design/battle-animation-rendering.md §6.4);
the hits are what the hit bank puts on the target when an attack lands (sparks, smoke, debris,
the small blasts), shared by every weapon whose header names that hit script; the weapon effects
are everything else the weapon bank's actors draw (muzzle flashes, shells and missiles, sabre arcs,
energy balls, the special moves' own pictures), less the generic beams, which are drawn by
program (BEAM_SCENES); the unscripted are the battle_scenes entries left over, which no animation
script names (palette variants of pictures already done, a few pictures of their own): done too,
in case code puts them on; the follow-ons are the scenes actors step on to at run time, which
are in no triplet (FOLLOW_ON_REACH).

    .venv/bin/python tools/hd_ai/cutin_hd.py export
    build/esrgan-venv/bin/python tools/hd_ai/cutin_hd.py run [--models build/esrgan-models] [--only SET ...] [--fast [1|2]]
    build/esrgan-venv/bin/python tools/hd_ai/cutin_hd.py pack [--bind]
    .venv/bin/python tools/hd_ai/cutin_hd.py extend --into assets/hd-ai/cutins/pack-vN [--only SET ...] [--replace] [--bind]
    .venv/bin/python tools/hd_ai/cutin_hd.py scenes [--bind]
    .venv/bin/python tools/hd_ai/cutin_hd.py share --into assets/hd-ai/cutins/pack-vN [--bind]
    .venv/bin/python tools/hd_ai/cutin_hd.py rescale --into assets/hd-ai/cutins/pack-vN [--bind]

`extend` adds frames.json rows the bound pack has no picture for to a copy of it, leaving
its pictures and scene thumbnails as they are; with --replace it packs the rows of the
--only sets afresh even where the bound pack has a picture.

Output under assets/hd-ai/cutins: inputs/<digest>.png (the original frames),
frames.json, hd/<digest>.png (8x masters), review-*.jpg, and pack/ (battle-sprites.json,
srw64.unit-extra-images.v1, one PNG per frame at PACK_SCALE, 5x) that --bind points
the art manifest's `battle_sprites` at (the index other battle sprites will share).

The pack also carries the battle viewer's scene thumbnails (`scenes`: one daytime render
per scene of its list, from the battle backgrounds catalog), which `pack` adds and
`scenes` adds alone.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import (CUTIN_REGISTRY, Scene, decode_atlas, parse_scene, read_animation_bank,  # noqa: E402
                                          read_battle_units, read_map_movies, read_triplets, render_scene)

OUT = ROOT / 'assets/hd-ai/cutins'
MANIFEST = ROOT / 'content/art/stage1-hd.json'
ANALYSIS = ROOT / 'assets/hd-ai/battle-backgrounds/unit-extra-analysis.json'
RECIPE = ('4x-UltraSharpV2', '4x-PixelPerfectV4')
K = 8
# The battle viewer's scenes (src/host/battle_viewer.cpp scenes_table): key, a, b; the
# background record is D_800C5940[a] x 101 + b (docs/design/battle-viewer.md §2).
VIEWER_SCENES = (('plains', 0, 0), ('wasteland', 0, 7), ('rocks', 0, 21), ('village', 0, 22), ('mountains', 0, 24),
                 ('forest', 0, 25), ('sea', 0, 31), ('city', 0, 37), ('sky', 0, 39), ('ruins', 0, 41), ('base', 0, 51),
                 ('bridge', 0, 54), ('harbor', 0, 58), ('snow', 6, 32), ('desert', 10, 7), ('cave', 15, 15), ('moon', 17, 21),
                 ('space', 17, 100), ('fortress', 20, 15), ('other', 20, 83), ('otherworld', 22, 61))
SCENE_GROUPS = 0x800C5940 - 0x80075610
DEFENCE_TABLE = 0x2166A0     # func_801C31AC: the defence effects by reaction code
# Defence effects whose actor steps on to the next scenes of the same atlas (seen at run
# time: the barrier 4170 -> 4171, 4172; the attacker-coloured burst 4264 -> 4265, 4266).
DEFENCE_FOLLOW_ONS = {4170: 2, 4264: 2}
CATALOG = ROOT / 'assets/hd-ai/battle-backgrounds/catalog'
# Palette slots a battle fills at run time and the ROM holds as white (0xFFFF) between
# magenta (0xF83F) fences: palette -> {first slot: RGBA16 colours}. Read from the loaded
# palette in the battle viewer once the weapon's fade from white has settled (issue #15):
# ゴーショーグン's ゴーサーベル (scene 2616, slots 65-77) and スペースバズーカ (2618, 81-91).
RUNTIME_PALETTES = {
    2079: {65: (0xDEF7, 0xCE73, 0xC631, 0xB5AD, 0xA529, 0x94A5, 0x8421, 0x739D, 0x6319, 0x5295, 0x4A53, 0x39CF, 0x1085),
           81: (0xCE73, 0xBDEF, 0xA529, 0x94A5, 0x8421, 0x739D, 0x6319, 0x5295, 0x4A53, 0x39CF, 0x1085)},
}


def runtime_palette(number: int, data: bytes) -> bytes:
    """The palette resource with the colours a battle writes over its placeholder slots."""
    data = bytearray(data)
    for first, colours in RUNTIME_PALETTES.get(number, {}).items():
        for n, colour in enumerate(colours):
            data[8 + 2 * (first + n):10 + 2 * (first + n)] = colour.to_bytes(2, 'big')
    return bytes(data)


def digest(image: Image.Image) -> str:
    return hashlib.sha256(image.tobytes() + repr(image.size).encode()).hexdigest()[:16]


def prop_registries(rom: bytes) -> list[int]:
    scenes = read_triplets(rom, 'battle_scenes')
    poses = {k for k in read_triplets(rom, 'unit_poses') if k[0]}
    atlases = {a for _, a, _ in poses}
    extras = {(r['scene'], r['palette']) for r in json.loads(ANALYSIS.read_text())}
    return [r for r, (s, a, p) in enumerate(scenes)
            if s and a in atlases and (s, a, p) not in poses and (s, p) not in extras
            and not CUTIN_REGISTRY[0] <= r <= CUTIN_REGISTRY[1]]


def shield_triplets(rom: bytes) -> list[tuple[int, tuple[int, int, int]]]:
    """(unit, (scene, atlas, palette)) of the units' shields: the battle unit record's
    +2..4 is one triplet, which func_801C4574 loads onto the defender's actor for a shield
    defence (reaction 0x11) without going through battle_scenes. Poses and the extra
    images rebuilt from them are left out."""
    poses = {k for k in read_triplets(rom, 'unit_poses') if k[0]}
    extras = {(r['scene'], r['palette']) for r in json.loads(ANALYSIS.read_text())}
    return [(unit, tuple(u['raw'][2:5])) for unit, u in enumerate(read_battle_units(rom))
            if u['raw'][2] and tuple(u['raw'][2:5]) not in poses and (u['raw'][2], u['raw'][4]) not in extras]


def defence_triplets(rom: bytes) -> list[tuple[int, tuple[int, int, int], bool]]:
    """(reaction code, (scene, atlas, palette), any palette) of the defence effects
    func_801C4574 puts on the defender for the other reactions: ROM DEFENCE_TABLE, 6 bytes
    a code (codes 2 and 0xD take the next entry, except for units 0x32 and 0x126). Below
    0x11 the palette becomes that of the attacker's first live actor, so those pictures
    are matched on any palette and recoloured from their own (native_sprite.cpp)."""
    rows = []
    for code in range(0x14):
        triplet = struct.unpack_from('>3H', rom, DEFENCE_TABLE + 6 * code)
        if triplet[0]:
            recoloured = 3 <= code < 0x11 and code != 0xD
            rows += [(code, (triplet[0] + step, *triplet[1:]), recoloured) for step in range(DEFENCE_FOLLOW_ONS.get(triplet[0], 0) + 1)]
    return rows


def reaction_registries(rom: bytes) -> list[int]:
    """The battle_scenes entries the reaction bank's own actors draw: BeamShield's gathering
    light (17) and orbiting balls (861), the afterimage (0), the decoy's burst and
    explosion (969, 115)."""
    return sorted({actor.registry for _, script in read_animation_bank(rom, 'reaction') for actor in script.actors})


def hit_registries(rom: bytes) -> list[int]:
    """The battle_scenes entries the hit bank's actors draw (the weapon header's hit_script;
    156-158 are the kill explosions, already their own set)."""
    return sorted({actor.registry for _, script in read_animation_bank(rom, 'hit') for actor in script.actors})


# The eleven generic beams (docs/design/battle-animation-rendering.md §16, the beam catalogue) and
# the scenes they change to on a hit: drawn by the HD beam shader, not by pictures.
BEAM_SCENES = frozenset(s + step for s in (3650, 3587, 3558, 4375, 3709, 3624, 3627, 3612, 3661, 4809, 4807, 3649)
                        for step in range(3))


def weapon_registries(rom: bytes) -> list[int]:
    """The battle_scenes entries the weapon bank's actors draw, less the generic beams and the
    units' poses and extra images (their own layers)."""
    scenes = read_triplets(rom, 'battle_scenes')
    poses = {k for k in read_triplets(rom, 'unit_poses') if k[0]}
    extras = {(r['scene'], r['palette']) for r in json.loads(ANALYSIS.read_text())}
    return sorted({actor.registry for _, script in read_animation_bank(rom, 'weapon') for actor in script.actors
                   if scenes[actor.registry][0] not in BEAM_SCENES and scenes[actor.registry] not in poses
                   and (scenes[actor.registry][0], scenes[actor.registry][2]) not in extras})


# The brush-written special-move characters (鳳 凰 光 覇 ...): battle_scenes entries drawn from
# atlas 4519, kept in Japanese and only sharpened (docs/data/hd-asset-inventory.md).
BRUSH_REGISTRY = (865, 881)
# The kill explosions: the fireballs, the big flash, the warships' white ball and the sparks.
EXPLOSION_REGISTRY = (1042, 1045)
SETS = ('cutin', 'prop', 'shield', 'defence', 'reaction', 'brush', 'movie', 'explosion', 'hit', 'weapon', 'unscripted',
        'follow_on')
# Many actor behaviours step the sprite on to the next scenes of its atlas (the tracer 3555 ->
# 3556, 3557; the comet run 3821 -> 3822 ... 3844), now and then the atlas and palette with it
# (3878/3141/3404 -> 3880/3142/3405). A scene resource in no triplet is matched to the nearest
# registered scene before it (within this many ids, over scenes only) whose atlas, as it is or
# moved on by up to FOLLOW_ON_SHIFT with its palette, holds every part of it.
FOLLOW_ON_REACH, FOLLOW_ON_SHIFT = 40, 3
# The effects (no face, mostly soft glow) are packed at 3x instead of PACK_SCALE: side by side at
# the largest scale a battle shows them (1080p) the two cannot be told apart, at a third of the size.
EFFECT_SETS = frozenset({'weapon', 'hit', 'follow_on', 'unscripted', 'explosion', 'reaction', 'defence'})
EFFECT_SCALE = 3
EFFECT_LABELS = ('weapon effect ', 'hit ', 'follow-on ', 'unscripted ', 'explosion ', 'reaction ', 'defence ')


def follow_on_triplets(rom: bytes, table: ResourceTable) -> list[tuple[int, tuple[int, int, int]]]:
    """(base scene, (scene, atlas, palette)) of the follow-on scenes, one row per palette the
    base is registered with; the units' poses and the generic beams keep to their own layers.
    A guess by fit: a scene matched to an atlas no actor shows it with is a picture never asked for."""
    poses = [t for t in read_triplets(rom, 'unit_poses') if t[0]]
    registered: dict[int, set] = {}
    for s, a, p in [t for t in read_triplets(rom, 'battle_scenes') if t[0]] + poses:
        registered.setdefault(s, set()).add((a, p))
    pose_atlases = {a for _, a, _ in poses}
    parsed, sizes = {}, {}

    def scene_of(n):
        if n not in parsed:
            try:
                parsed[n] = parse_scene(table.extract(n)[0])
            except Exception:
                parsed[n] = None
        return parsed[n]

    def fits(scene, a, p):
        if (a, p) not in sizes:
            try:
                sizes[a, p] = decode_atlas(table.extract(a)[0], table.extract(p)[0])[0].size
            except Exception:
                sizes[a, p] = None
        size = sizes[a, p]
        return size and all(q.s + q.w <= size[0] and q.t + q.h <= size[1] for parts in scene.frames for q in parts)

    rows = []
    for n in range(min(registered), max(registered) + 1):
        scene = scene_of(n) if n not in registered and n not in BEAM_SCENES else None
        if not scene:
            continue
        found = None
        for shift in range(FOLLOW_ON_SHIFT + 1):
            k = n - 1
            while not found and n - k <= FOLLOW_ON_REACH and (k in registered or scene_of(k)):
                for a, p in sorted(registered.get(k, ())):
                    if fits(scene, a + shift, p + shift):
                        found = (k, a, shift)
                        break
                k -= 1
            if found:
                break
        if not found or found[1] in pose_atlases or found[0] in BEAM_SCENES:
            continue
        base, a, shift = found
        rows += [(base, (n, a + shift, p + shift)) for p in sorted({p for a2, p in registered[base] if a2 == a})]
    return rows


def unscripted_registries(rom: bytes) -> list[int]:
    """Every battle_scenes entry but the generic beams, the poses and the extra images; export
    keeps those whose triplet no earlier set has (no animation bank names them)."""
    scenes = read_triplets(rom, 'battle_scenes')
    poses = {k for k in read_triplets(rom, 'unit_poses') if k[0]}
    extras = {(r['scene'], r['palette']) for r in json.loads(ANALYSIS.read_text())}
    return [r for r, (s, a, p) in enumerate(scenes)
            if s and s not in BEAM_SCENES and (s, a, p) not in poses and (s, p) not in extras]


def movie_triplets(rom: bytes) -> list[tuple[int, tuple[int, int, int]]]:
    """(movie, (scene, atlas, palette)) of the map movies 802176A8 plays (コン・バトラーV and
    ゴッドマーズ combining, the Aura Roads, the Getter changes), drawn by the same scene sprite
    engine as the battles."""
    return [(movie, triplet) for movie, _, _, triplets in read_map_movies(rom) for triplet in triplets]


def palette_indices(table: ResourceTable, scene: Scene, atlas: int, palette: int) -> list[list[int]]:
    """Per frame, the opaque palette entries its pixels use: the atlas decoded with a palette
    whose entry i is a colour of its own. An any-palette picture is recoloured from these
    only, since a palette can repeat a colour at an entry the picture never shows (4259
    holds white at 1 and 15; 15 is unused, and an attacker's palette has green there)."""
    data = bytearray(table.extract(palette)[0])
    for i in range((len(data) - 8) // 2):
        opaque = data[9 + 2 * i] & 1
        data[8 + 2 * i:10 + 2 * i] = ((i + 1) << 1 | 1 if opaque else 0).to_bytes(2, 'big')
    coded, _ = decode_atlas(table.extract(atlas)[0], bytes(data))
    images, _ = render_scene(scene, coded)
    rows = []
    for image in images:
        pixels = np.asarray(image.convert('RGBA')).reshape(-1, 4)
        pixels = pixels[pixels[:, 3] > 0]
        # rgb5551 -> 8 bit as decode_indexed does; green and blue carry (i + 1)
        values = {(round(int(g) * 31 / 255) << 5 | round(int(b) * 31 / 255)) - 1 for _, g, b, _ in np.unique(pixels, axis=0)}
        rows.append(sorted(v for v in values if v >= 0))
    return rows


def export() -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    scenes = read_triplets(rom, 'battle_scenes')
    (OUT / 'inputs').mkdir(parents=True, exist_ok=True)
    rows, seen = [], set()
    sets = ([('cutin', r, scenes[r]) for r in range(CUTIN_REGISTRY[0], CUTIN_REGISTRY[1] + 1)]
            + [('prop', r, scenes[r]) for r in prop_registries(rom)]
            + [('shield', unit, triplet) for unit, triplet in shield_triplets(rom)]
            + [('defence', code, triplet) for code, triplet, _ in defence_triplets(rom)]
            + [('reaction', r, scenes[r]) for r in reaction_registries(rom)]
            + [('brush', r, scenes[r]) for r in range(BRUSH_REGISTRY[0], BRUSH_REGISTRY[1] + 1)]
            + [('movie', movie, triplet) for movie, triplet in movie_triplets(rom)]
            + [('explosion', r, scenes[r]) for r in range(EXPLOSION_REGISTRY[0], EXPLOSION_REGISTRY[1] + 1)]
            + [('hit', r, scenes[r]) for r in hit_registries(rom)]
            + [('weapon', r, scenes[r]) for r in weapon_registries(rom)]
            + [('unscripted', r, scenes[r]) for r in unscripted_registries(rom)]
            + [('follow_on', base, triplet) for base, triplet in follow_on_triplets(rom, table)])
    any_palette = {triplet for _, triplet, recoloured in defence_triplets(rom) if recoloured}
    for kind, number, (s, a, p) in sets:
        if not s or (s, a, p) in seen:
            continue
        seen.add((s, a, p))
        scene = parse_scene(table.extract(s)[0])
        atlas, _ = decode_atlas(table.extract(a)[0], runtime_palette(p, table.extract(p)[0]))
        images, _ = render_scene(scene, atlas)
        used = palette_indices(table, scene, a, p) if (s, a, p) in any_palette else None
        x0, y0 = scene.bounds()[:2]
        for frame in sorted({f for f, _ in scene.steps if f != 0xFF}):
            parts = scene.frames[frame]
            if not parts:
                continue
            box = (min(q.x for q in parts), min(q.y for q in parts), max(q.x + q.w for q in parts), max(q.y + q.h for q in parts))
            image = images[frame].convert('RGBA').crop((box[0] - x0, box[1] - y0, box[2] - x0, box[3] - y0))
            key = digest(image)
            if not (OUT / 'inputs' / f'{key}.png').exists():
                image.save(OUT / 'inputs' / f'{key}.png')
            field = {'shield': 'unit', 'defence': 'code', 'movie': 'movie', 'follow_on': 'base'}.get(kind, 'registry')
            rows.append({'set': kind, field: number, 'scene': s, 'atlas': a, 'palette': p,
                         'frame': frame, 'box': list(box), 'input': key}
                        | ({'any_palette': True, 'palette_indices': used[frame]} if used else {}))
    (OUT / 'frames.json').write_text(json.dumps({'schema': 'srw64.cutin-frames.v1', 'frames': rows}, indent=1) + '\n')
    for kind in SETS:
        own = [r for r in rows if r['set'] == kind]
        print(kind, len(own), 'frames,', len({r['input'] for r in own}), 'distinct')


def run_fast(models: list, source: Image.Image, device, passes: int = 2) -> Image.Image:
    """The recipe made cheaper for effects, where nothing has a face to change: each model's
    second pass starts from its first pass shrunk to 2x (a quarter of the pixels), or with
    `passes` 1 there is no second pass and Lanczos takes the 4x to 8x; the alpha takes one
    PixelPerfectV4 pass and Lanczos (about 4x faster per frame, 10x with one pass)."""
    from tools.hd_ai.build_unit_images import clean_alpha
    from tools.hd_ai.esrgan_pose import WORK, fill_transparent, upscale
    colour = fill_transparent(source)
    twice = (source.width * 2, source.height * 2)
    size = (source.width * WORK, source.height * WORK)
    ends = []
    for model in models:
        image = upscale(model, colour, device)
        if passes > 1:
            image = upscale(model, image.resize(twice, Image.Resampling.LANCZOS), device)
        ends.append(image if image.size == size else image.resize(size, Image.Resampling.LANCZOS))
    alpha = upscale(models[1], source.getchannel('A').convert('RGB'), device).resize(size, Image.Resampling.LANCZOS)
    result = Image.blend(ends[0], ends[1], .5)
    result.putalpha(alpha.convert('L'))
    return clean_alpha(result, source)


def run(models_dir: Path, only: list[str] | None = None, fast: int = 0) -> None:
    import numpy as np
    import spandrel
    import torch
    from tools.hd_ai.esrgan_pose import WORK, run_pose
    device = torch.device('mps' if torch.backends.mps.is_available() else 'cpu')
    models = [spandrel.ModelLoader().load_from_file(str(next(models_dir.glob(f'{name}.*')))).to(device).eval() for name in RECIPE]
    keys = sorted({r['input'] for r in json.loads((OUT / 'frames.json').read_text())['frames'] if not only or r.get('set') in only})
    (OUT / 'hd').mkdir(exist_ok=True)
    for n, key in enumerate(keys, 1):
        target = OUT / 'hd' / f'{key}.png'
        if target.exists():
            continue
        start = time.monotonic()
        source = Image.open(OUT / 'inputs' / f'{key}.png').convert('RGBA')
        if not source.getchannel('A').getbbox():  # all clear (a hit's empty step): nothing to fill from
            Image.new('RGBA', (source.width * WORK, source.height * WORK)).save(target)
            continue
        if fast:
            run_fast(models, source, device, fast).save(target)
        else:
            a, b = (run_pose(model, source, device) for model in models)
            Image.blend(a, b, .5).save(target)
        print(f'{n}/{len(keys)} {key} {source.size} {time.monotonic() - start:.1f}s', flush=True)
    del np


def pack_row(r: dict, folder: Path) -> dict:
    """One frames.json row packed into `folder` at PACK_SCALE, and its index entry."""
    name = f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png"
    redrawn = OUT / 'imagegen' / name            # cutin_imagegen.py compose: small faces redrawn
    source = redrawn if redrawn.exists() else OUT / 'hd' / f"{r['input']}.png"
    if not source.exists():
        raise SystemExit(f'no HD frame for {r} (run first)')
    from tools.hd_ai.unit_extra_derive import PACK_SCALE, pack_image
    kind = r.get('set', 'cutin')
    scale = EFFECT_SCALE if kind in EFFECT_SETS else PACK_SCALE
    image = pack_image(Image.open(source), K * PACK_SCALE / scale)   # pack_image resizes to PACK_SCALE / its scale
    image.save(folder / name, optimize=True)
    label = {'cutin': f"cut-in {r.get('registry')}", 'prop': f"prop {r.get('registry')}", 'shield': f"shield {r.get('unit')}",
             'defence': f"defence {r.get('code')}", 'reaction': f"reaction {r.get('registry')}",
             'brush': f"brush {r.get('registry')}", 'movie': f"map movie {r.get('movie')}",
             'explosion': f"explosion {r.get('registry')}", 'hit': f"hit {r.get('registry')}",
             'weapon': f"weapon effect {r.get('registry')}", 'unscripted': f"unscripted {r.get('registry')}",
             'follow_on': f"follow-on of scene {r.get('base')}"}[kind]
    return {'scene': r['scene'], 'atlas': r['atlas'], 'palette': r['palette'], 'frame': r['frame'], 'file': name,
            'sha256': hashlib.sha256((folder / name).read_bytes()).hexdigest(),
            'width': image.width, 'height': image.height, 'method': 'image_gen' if source == redrawn else 'esrgan', 'unit': label} \
        | ({'scale': scale} if scale != PACK_SCALE else {}) \
        | ({'any_palette': True, 'palette_indices': r['palette_indices']} if r.get('any_palette') else {})


def pack(bind: bool) -> None:
    frames = json.loads((OUT / 'frames.json').read_text())['frames']
    folder = OUT / 'pack'
    folder.mkdir(exist_ok=True)
    for old in folder.iterdir():
        old.unlink()
    rows = [pack_row(r, folder) for r in frames]
    from tools.hd_ai.unit_extra_derive import PACK_SCALE
    index = {'schema': 'srw64.unit-extra-images.v1', 'scale': PACK_SCALE,
             'recipe': f'50% {RECIPE[0]} + 50% {RECIPE[1]} (tools/hd_ai/cutin_hd.py)', 'images': rows}
    share_files(folder, index)
    print(len(rows), 'frames packed')
    scenes(bind)


def extend(into: Path, bind: bool, only: list[str] | None = None, replace: bool = False) -> None:
    """A new pack folder: the bound pack hard-linked as it is (its frames, scene thumbnails
    and index), plus the frames.json rows it has no picture for, packed the usual way, or
    with `replace` every row of the `only` sets."""
    into = into.absolute()                       # not resolved: assets/ may be a link into another checkout
    current = ROOT / json.loads(MANIFEST.read_text())['battle_sprites']['path']
    into.mkdir(parents=True, exist_ok=False)
    for path in current.iterdir():
        if path.is_file() and path.name != 'battle-sprites.json':
            os.link(path, into / path.name)
    index = json.loads((current / 'battle-sprites.json').read_text())
    have = {frame_key(r) for r in index['images']}
    rows = [r for r in json.loads((OUT / 'frames.json').read_text())['frames'] if not only or r.get('set') in only]
    if not replace:
        rows = [r for r in rows if frame_key(r) not in have]
    for r in rows:                                # a hard link: drop it rather than write through to the bound pack
        (into / f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png").unlink(missing_ok=True)
    added = [pack_row(r, into) for r in rows]
    keys = {frame_key(r) for r in added}
    index['images'] = [r for r in index['images'] if frame_key(r) not in keys] + added
    share_files(into, index)
    print(len(added), 'frames added to', current.name, '->', into.name)
    if bind:
        bind_pack(into)


def frame_key(r: dict) -> tuple[int, int, int, int]:
    return r['scene'], r['atlas'], r['palette'], r['frame']


def share_files(folder: Path, index: dict) -> None:
    """Rows whose pictures are byte for byte the same (another palette that draws the same
    colours, a scene repeated under another id: a fifth of the pack) point at one file,
    same-<sha256 prefix>.png, and the files no row names any more go. A row's own
    <scene>-<atlas>-<palette>-f<frame>.png is never shared, so `extend` can rewrite it."""
    groups: dict[str, list[dict]] = {}
    for r in index['images']:
        groups.setdefault(r['sha256'], []).append(r)
    for digest_, rows in groups.items():
        if len(rows) < 2:
            continue
        name = f'same-{digest_[:16]}.png'
        if not (folder / name).exists():
            os.link(folder / rows[0]['file'], folder / name)
        for r in rows:
            r['file'] = name
    named = {r['file'] for r in index['images']} | {r['file'] for r in index.get('scenes', [])}
    for path in folder.glob('*.png'):
        if path.name not in named:
            path.unlink()
    (folder / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')


def bind_pack(folder: Path) -> None:
    manifest = json.loads(MANIFEST.read_text())
    manifest['battle_sprites'] = {'path': str(folder.relative_to(ROOT)),
                                  'manifest_sha256': hashlib.sha256((folder / 'battle-sprites.json').read_bytes()).hexdigest()}
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print('bound', MANIFEST.relative_to(ROOT))


def rescale(into: Path, bind: bool) -> None:
    """A new pack folder: the bound pack hard-linked, its effect rows (EFFECT_LABELS) at
    PACK_SCALE shrunk from their packed pictures to EFFECT_SCALE (colour and alpha apart,
    pack_image), then shared (share_files)."""
    from tools.hd_ai.unit_extra_derive import PACK_SCALE, pack_image
    into = into.absolute()
    current = ROOT / json.loads(MANIFEST.read_text())['battle_sprites']['path']
    into.mkdir(parents=True, exist_ok=False)
    for path in current.iterdir():
        if path.is_file() and path.name != 'battle-sprites.json':
            os.link(path, into / path.name)
    index = json.loads((current / 'battle-sprites.json').read_text())
    done, count = {}, 0
    for r in index['images']:
        if not r['unit'].startswith(EFFECT_LABELS) or r.get('scale', PACK_SCALE) == EFFECT_SCALE:
            continue
        if r['file'] not in done:
            image = pack_image(Image.open(current / r['file']), PACK_SCALE * PACK_SCALE / EFFECT_SCALE)
            name = f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png"
            (into / name).unlink(missing_ok=True)       # a hard link: never write through to the bound pack
            image.save(into / name, optimize=True)
            done[r['file']] = {'file': name, 'sha256': hashlib.sha256((into / name).read_bytes()).hexdigest(),
                               'width': image.width, 'height': image.height, 'scale': EFFECT_SCALE}
        r.update(done[r['file']])
        count += 1
    share_files(into, index)
    print(count, 'effect rows at', EFFECT_SCALE, 'x,', len({r['file'] for r in index['images']}), 'files ->', into.name)
    if bind:
        bind_pack(into)


def share(into: Path, bind: bool) -> None:
    """A new pack folder: the bound pack hard-linked, with its identical pictures shared (share_files)."""
    into = into.absolute()
    current = ROOT / json.loads(MANIFEST.read_text())['battle_sprites']['path']
    into.mkdir(parents=True, exist_ok=False)
    for path in current.iterdir():
        if path.is_file() and path.name != 'battle-sprites.json':
            os.link(path, into / path.name)
    index = json.loads((current / 'battle-sprites.json').read_text())
    before = len({r['file'] for r in index['images']})
    share_files(into, index)
    print(len(index['images']), 'rows,', before, '->', len({r['file'] for r in index['images']}), 'files')
    if bind:
        bind_pack(into)


def scenes(bind: bool, shots: Path | None = None, into: Path | None = None) -> None:
    """The viewer's scene thumbnails into the pack (scene-<key>.jpg, index `scenes`): the
    catalogue's offline renders, or with `shots` the HD captures of viewer_scenes.py. With
    `into`, a new pack folder hard-linked from the current one takes them, so the pack an
    older commit names keeps its files."""
    folder = OUT / 'pack'
    if into is not None:
        into = into.resolve()
        into.mkdir(parents=True, exist_ok=False)
        for path in folder.iterdir():
            if path.is_file():
                os.link(path, into / path.name)
        folder = into
    rom = (ROOT / 'rom.z64').read_bytes()
    sets = json.loads((CATALOG / 'catalog.json').read_text())['sets']
    index = json.loads((folder / 'battle-sprites.json').read_text())
    (folder / 'battle-sprites.json').unlink()
    rows = []
    for key, a, b in VIEWER_SCENES:
        record = rom[SCENE_GROUPS + a] * 101 + b
        found = next(s for s in sets if record in s['records'] and not s['night'])
        name = f'scene-{key}.jpg'
        source = shots / name if shots is not None else CATALOG / 'thumbs' / f"{found['thumb']}.jpg"
        data = source.read_bytes()
        (folder / name).unlink(missing_ok=True)          # a hard link stays the older pack's
        (folder / name).write_bytes(data)
        with Image.open(folder / name) as image:
            rows.append({'key': key, 'record': record, 'file': name, 'sha256': hashlib.sha256(data).hexdigest(),
                         'width': image.width, 'height': image.height})
    index['scenes'] = rows
    (folder / 'battle-sprites.json').write_text(json.dumps(index, ensure_ascii=False, indent=1) + '\n')
    print(len(rows), 'scene thumbnails')
    if bind:
        manifest = json.loads(MANIFEST.read_text())
        manifest['battle_sprites'] = {'path': str(folder.relative_to(ROOT)),
                              'manifest_sha256': hashlib.sha256((folder / 'battle-sprites.json').read_bytes()).hexdigest()}
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT))


def review() -> None:
    """Original beside HD for the middle frame of every cut-in and prop, eight to a sheet
    (review-<set>-N.jpg)."""
    frames = json.loads((OUT / 'frames.json').read_text())['frames']
    for kind in SETS:
        by_registry = {}
        for r in frames:
            if r.get('set', 'cutin') == kind:
                by_registry.setdefault(r.get('registry', r.get('unit', r.get('code', r.get('movie')))), []).append(r)
        picks = [rs[len(rs) // 2] for rs in by_registry.values()]
        W, H = (320, 240) if kind == 'cutin' else (200, 150)
        cols, rows = (2, 4) if kind == 'cutin' else (4, 8)
        per = cols * rows
        for page in range(0, len(picks), per):
            chunk = picks[page:page + per]
            sheet = Image.new('RGB', (cols * (2 * W + 16) + 8, rows * (H + 26) + 8), (28, 30, 36))
            draw = ImageDraw.Draw(sheet)
            for i, r in enumerate(chunk):
                x, y = 8 + (i % cols) * (2 * W + 16), 6 + (i // cols) * (H + 26)
                draw.text((x, y), f"{r.get('registry', r.get('unit', r.get('code', r.get('movie'))))}  scene {r['scene']} f{r['frame']}", fill='white')
                hd = OUT / 'imagegen' / f"{r['scene']}-{r['atlas']}-{r['palette']}-f{r['frame']}.png"
                for j, path in enumerate((OUT / 'inputs' / f"{r['input']}.png", hd if hd.exists() else OUT / 'hd' / f"{r['input']}.png")):
                    image = Image.open(path).convert('RGBA')
                    k = min(W / image.width, H / image.height, 6)
                    image = image.resize((max(1, round(image.width * k)), max(1, round(image.height * k))),
                                         Image.Resampling.NEAREST if j == 0 else Image.Resampling.LANCZOS)
                    tile = Image.new('RGBA', (W, H), (70, 74, 88, 255))
                    tile.alpha_composite(image, ((W - image.width) // 2, (H - image.height) // 2))
                    sheet.paste(tile.convert('RGB'), (x + j * (W + 8), y + 16))
            sheet.save(OUT / f'review-{kind}-{page // per + 1}.jpg', quality=88)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('export', 'run', 'pack', 'extend', 'scenes', 'share', 'rescale', 'review'))
    parser.add_argument('--models', type=Path, default=ROOT / 'build/esrgan-models')
    parser.add_argument('--only', nargs='+', help='run, extend: just these sets (brush, movie, ...)')
    parser.add_argument('--fast', type=int, nargs='?', const=2, default=0, choices=(1, 2),
                        help='run: the cheaper recipe for effects (run_fast), with 2 (default) or 1 model passes')
    parser.add_argument('--replace', action='store_true', help='extend: repack the --only sets even where the bound pack has them')
    parser.add_argument('--bind', action='store_true', help='point the art manifest at the pack')
    parser.add_argument('--shots', type=Path, help='scenes: HD captures (tools/hd_ai/viewer_scenes.py) instead of the catalogue renders')
    parser.add_argument('--into', type=Path, help='scenes, extend, share, rescale: a new pack folder, hard-linked from the current one')
    args = parser.parse_args()
    if args.step == 'export':
        export()
    elif args.step == 'run':
        run(args.models, args.only, args.fast)
    elif args.step == 'pack':
        pack(args.bind)
    elif args.step == 'extend':
        if args.into is None:
            parser.error('extend needs --into')
        if args.replace and not args.only:
            parser.error('--replace needs --only')
        extend(args.into, args.bind, args.only, args.replace)
    elif args.step == 'scenes':
        scenes(args.bind, args.shots, args.into)
    elif args.step in ('share', 'rescale'):
        if args.into is None:
            parser.error(f'{args.step} needs --into')
        (share if args.step == 'share' else rescale)(args.into, args.bind)
    else:
        review()


if __name__ == '__main__':
    main()
