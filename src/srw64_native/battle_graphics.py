"""Decode battle sprite scenes (poses, parts, effects, cut-ins, movies) and weapon animation records.

A sprite scene is a resource that lays 32x32-ish pieces of one atlas out into
frames and plays them as a step sequence. Every table that names a scene pairs
it with its atlas and palette as a 6-byte ``(scene, atlas, palette)`` triplet;
the tables and their readers are pinned in ``TRIPLET_TABLES``. Scene layout,
all big-endian::

    u8  step_count, u8 vertex_mode
    step_count x (u8 frame, u8 ticks)    frame 0xFF shows nothing for ``ticks``
    u8  0xFF, u8 loop_step
    u16 frame_offsets[n]                  n = (frame_offsets[0] - table start) / 2
    frame = 16-byte parts until flags & 0x8000:
        u16 flags (0x0010 horizontal flip), u16 s, u16 t, u8 w, u8 h,
        s16 x, s16 y (screen, y down), u32 vertex_offset

The resident sprite tick 80098738 plays the steps; the rectangle drawer
80096CD8 uses only the part records, the vertex drawer 8009761C reads
vertex_mode: 0 = 8 vertices per part (the drawn quad, then its mirror),
1 = 4 vertices, 2 = none. The mode-0 vertices agree with the part rectangle
and flip flag on every part in the ROM, so frames are rendered from the parts.
"""
from __future__ import annotations

from dataclasses import dataclass
from io import BytesIO
import struct

from PIL import Image, ImageOps

from .catalog import sha
from .original_data import checked_slice
from .original_images import decode_indexed

FLIP_X = 0x0010
END_PART = 0x8000
BLANK_FRAME = 0xFF

# Every table ends with two bytes of padding after its last slot.
TRIPLET_TABLES = {
    "unit_poses": {"rom_offset": 0x84E40, "count": 365, "reader": "resident func_8009C864",
                   "index": "unit id", "sha256": "8950905f63bbbf99bda4c6f38c461a6de55cfb7adc051c14d10d417cc660066a"},
    "battle_scenes": {"rom_offset": 0x11E3D0, "count": 1053, "reader": "load_00121560 func_801C3170",
                      "index": "battle scene id",
                      "sha256": "5874f46660adbe2c52a8424a9217ec23d7652f5f24ad70e6c3530aca04bbd0a2"},
    "chapter_titles": {"rom_offset": 0x84B20, "count": 133, "reader": "resident func_8009C8EC",
                       "index": "chapter title id",
                       "sha256": "f2d108fbbd8192a6757e22158f39122537f7d1df23b09e391b1649b51ffaad7b"},
}

# Map movies: load_000AB160 keeps one triplet run per movie (followed by its
# step-function pointers) at ROM 0x106F20..0x107200; its player 802176A8(id)
# starts movie `id`, ids 5-10 each take five triplets from one shared run.
# The resident table at 0x847D0 holds the same art but its reader 8009C7DC
# has no caller, so it is not used here.
MAP_MOVIE_BLOCK = {"rom_offset": 0x106F20, "size": 0x2E0,
                   "sha256": "54f618d6ee8bd79b2ccf967630b4f76ddbacda9762afda90b0625ea47659cb0f"}
MAP_MOVIES = (
    (0, 0x106F20, 13, "コン・バトラーV 合体", "script 3D67 0; unit 196 combine"),
    (1, 0x106F90, 7, "オーラロード · ズワァース", "script 3D67 1"),
    (2, 0x106FD4, 9, "オーラロード（未使用）", "no caller found"),
    (3, 0x107028, 9, "オーラロード · トッド", "script 3D67 3"),
    (4, 0x10707C, 9, "オーラロード · ジェリル", "script 3D67 4"),
    (5, 0x1070D0, 5, "チェンジ · ゲッタードラゴン", "unit 174 first form change"),
    (6, 0x1070EE, 5, "チェンジ · ゲッターライガー", "unit 176 first form change"),
    (7, 0x10710C, 5, "チェンジ · ゲッターポセイドン", "unit 175 first form change"),
    (8, 0x10712A, 5, "チェンジ · ゲッター1", "unit 171 first form change"),
    (9, 0x107148, 5, "チェンジ · ゲッター2", "unit 172 first form change"),
    (10, 0x107166, 5, "チェンジ · ゲッター3", "unit 173 first form change"),
    (11, 0x107194, 18, "ゴッドマーズ 合体", "script 3D6A"),
)


@dataclass(frozen=True)
class Part:
    flags: int
    s: int
    t: int
    w: int
    h: int
    x: int
    y: int
    vertex_offset: int

    @property
    def flip_x(self) -> bool:
        return bool(self.flags & FLIP_X)


@dataclass(frozen=True)
class Scene:
    vertex_mode: int
    steps: tuple[tuple[int, int], ...]
    loop_step: int
    frames: tuple[tuple[Part, ...], ...]

    def bounds(self) -> tuple[int, int, int, int]:
        parts = [p for frame in self.frames for p in frame]
        if not parts:
            return 0, 0, 1, 1
        return (min(p.x for p in parts), min(p.y for p in parts),
                max(p.x + p.w for p in parts), max(p.y + p.h for p in parts))


def decode_atlas(image: bytes, palette: bytes) -> tuple[Image.Image, int]:
    """Decode an atlas; returns the image and the palette colours beyond what it can index.

    A few effect palettes hold 592 colours. CI8 indexes only the first 256; the
    rest is kept in the resource untouched (palette animation is unconfirmed).
    """
    kind, size = struct.unpack(">2H", checked_slice(palette, 0, 4))
    limit = 32 if struct.unpack(">H", checked_slice(image, 0, 2))[0] in (5, 14) else 512
    if kind != 3 or size <= limit or len(palette) != 8 + size:
        return decode_indexed(image, palette), 0
    head = struct.pack(">2H", kind, limit) + palette[4:8]
    return decode_indexed(image, head + palette[8:8 + limit]), (size - limit) // 2


def read_triplets(rom: bytes, name: str) -> list[tuple[int, int, int]]:
    spec = TRIPLET_TABLES[name]
    raw = checked_slice(rom, spec["rom_offset"], spec["count"] * 6 + 2)
    if sha(raw) != spec["sha256"]:
        raise ValueError(f"Triplet table changed: {name}")
    if raw[-2:] != b"\0\0":
        raise ValueError(f"Triplet table {name} lost its padding")
    return [struct.unpack_from(">3H", raw, i * 6) for i in range(spec["count"])]


def read_map_movies(rom: bytes) -> list[tuple[int, str, str, list[tuple[int, int, int]]]]:
    base, size = MAP_MOVIE_BLOCK["rom_offset"], MAP_MOVIE_BLOCK["size"]
    if sha(checked_slice(rom, base, size)) != MAP_MOVIE_BLOCK["sha256"]:
        raise ValueError("Map movie table changed")
    movies = []
    for movie, offset, count, title, trigger in MAP_MOVIES:
        if not (base <= offset and offset + count * 6 <= base + size):
            raise ValueError("Map movie outside its block")
        movies.append((movie, title, trigger,
                       [struct.unpack_from(">3H", rom, offset + i * 6) for i in range(count)]))
    return movies

def parse_scene(data: bytes) -> Scene:
    count, mode = checked_slice(data, 0, 2)
    if mode > 2:
        raise ValueError("Unknown scene vertex mode")
    raw_steps = checked_slice(data, 2, count * 2 + 2)
    steps = tuple((raw_steps[i], raw_steps[i + 1]) for i in range(0, count * 2, 2))
    end, loop_step = raw_steps[-2:]
    if end != BLANK_FRAME or loop_step > count:
        raise ValueError("Scene step list is not terminated")
    table = 2 + count * 2 + 2
    (first,) = struct.unpack_from(">H", checked_slice(data, table, 2))
    if first <= table or (first - table) % 2:
        raise ValueError("Invalid scene frame table")
    offsets = struct.unpack_from(f">{(first - table) // 2}H", checked_slice(data, table, first - table))
    if any(f != BLANK_FRAME and f >= len(offsets) for f, _ in steps):
        raise ValueError("Scene step names a missing frame")
    frames = []
    for offset in offsets:
        parts = []
        while True:
            fields = struct.unpack(">3H2B2hI", checked_slice(data, offset, 16))
            offset += 16
            if fields[0] & END_PART:
                break
            part = Part(*fields)
            if part.flags & ~FLIP_X or not (part.w and part.h):
                raise ValueError("Unsupported scene part")
            if mode < 2:
                checked_slice(data, part.vertex_offset, (4 if mode else 8) * 16)
            parts.append(part)
        frames.append(tuple(parts))
    return Scene(mode, steps, loop_step, tuple(frames))


def mode0_vertex_quad(data: bytes, part: Part) -> tuple[int, int, int, int, bool]:
    """Rectangle and horizontal flip of the first mode-0 vertex quad (screen coordinates)."""
    vertices = [struct.unpack_from(">3hH2h", data, part.vertex_offset + i * 16) for i in range(4)]
    left = min(v[0] for v in vertices)
    top = max(v[1] for v in vertices)
    corner = next(v for v in vertices if v[0] == left and v[1] == top)
    width = max(v[0] for v in vertices) - left
    height = top - min(v[1] for v in vertices)
    return left, -top, width, height, corner[4] != 0


def render_frame(frame: tuple[Part, ...], atlas: Image.Image,
                 bounds: tuple[int, int, int, int]) -> tuple[Image.Image, int]:
    """Composite one frame onto the scene canvas; returns the image and clipped-texel parts."""
    x0, y0, x1, y1 = bounds
    canvas = Image.new("RGBA", (x1 - x0, y1 - y0))
    clipped = 0
    for part in frame:
        piece = atlas.crop((part.s, part.t, part.s + part.w, part.t + part.h))
        if part.s + part.w > atlas.width or part.t + part.h > atlas.height:
            clipped += 1  # outside texels read as transparent here
        if part.flip_x:
            piece = ImageOps.mirror(piece)
        canvas.alpha_composite(piece, (part.x - x0, part.y - y0))
    return canvas, clipped


def render_scene(scene: Scene, atlas: Image.Image) -> tuple[list[Image.Image], int]:
    bounds = scene.bounds()
    images, clipped = [], 0
    for frame in scene.frames:
        image, count = render_frame(frame, atlas, bounds)
        images.append(image)
        clipped += count
    return images, clipped


def frame_strip(images: list[Image.Image], gap: int = 2) -> Image.Image:
    width = sum(i.width for i in images) + gap * (len(images) - 1)
    strip = Image.new("RGBA", (max(width, 1), max(i.height for i in images)))
    x = 0
    for image in images:
        strip.alpha_composite(image, (x, 0))
        x += image.width + gap
    return strip


def animated_png(scene: Scene, images: list[Image.Image], tick_ms: int) -> bytes:
    """Play the step list once (blank steps included) as an APNG; one frame stays a PNG."""
    blank = Image.new("RGBA", images[0].size)
    sequence = [(blank if f == BLANK_FRAME else images[f], max(ticks, 1) * tick_ms)
                for f, ticks in scene.steps] or [(images[0], tick_ms)]
    stream = BytesIO()
    if len(sequence) == 1 or len(images) == 1 and all(f != BLANK_FRAME for f, _ in scene.steps):
        images[0].save(stream, format="PNG")
    else:
        first, *rest = [image for image, _ in sequence]
        first.save(stream, format="PNG", save_all=True, append_images=rest, loop=0,
                   duration=[ms for _, ms in sequence], disposal=0, blend=0)
    return stream.getvalue()


# Weapon animation records (load_00121560). Not bytecode: a fixed header, a
# sound list and up to 24 actors; each actor names a battle_scenes registry
# entry and one of ~270 native behaviour routines (jump table 0x80225030).
# 801C3C9C passes the weapon id straight to the 0x11FC80 reader; the DMA
# window is 0x3C0 bytes.
ANIMATION_BANKS = {
    "weapon": {"offsets": 0x11FC80, "count": 1329, "bank": 0x184990, "bank_end": 0x19AA12,
               "reader": "load_00121560 func_801C3128", "index": "weapon id",
               "offsets_sha256": "551bdb86c6e1dafbc85e7eba8fc43f64fce8aa6327b722383d1cbf7edd54c846",
               "bank_sha256": "89a4fd6333a1e40d6880f3c25be22233af2a541a4904056ff1bdbd35a890ab71"},
    "hit": {"offsets": 0x216150, "count": 159, "bank": 0x2129F0, "bank_end": 0x215ECE,
            "reader": "load_00121560 func_801C31E8", "index": "weapon header hit_script; 156-158 destruction",
            "offsets_sha256": "10df1c58da32a324d56c0f74629f5680b74edf4f8eb13661f39b112a6d9d99f6",
            "bank_sha256": "c43cfb56a9ba9116347e4649f4b9b5f8f7c6a8c017fe9d831571b326477fe29d"},
    "reaction": {"offsets": 0x216630, "count": 28, "bank": 0x2163D0, "bank_end": 0x2165B8,
                 "reader": "load_00121560 func_801C3230", "index": "defender reaction code",
                 "offsets_sha256": "8e4c2be9d7f4c70bc31d70a721c1af9e25de5d809e6c2570272f2e26f5fed43b",
                 "bank_sha256": "08c882cda1f5f3a75e80c2def55460c13b9c358ae3811100da50e9692de0ae60"},
}
SCRIPT_WINDOW = 0x3C0
MAX_SOUNDS, MAX_ACTORS = 8, 24


@dataclass(frozen=True)
class Actor:
    registry: int    # battle_scenes index
    x: int
    y: int
    z: int
    h4: int          # behaviour-specific: usually a start delay or role
    h5: int          # copied to sprite byte +0x35 every frame
    h6: int          # 1 = never auto-hidden, 2 = never auto-shown
    behavior: int


@dataclass(frozen=True)
class AnimationScript:
    camera: int           # staging mode 0-3
    hit_script: int       # "hit" bank entry
    action: int           # attacker main-sprite action
    defender_action: int  # defender action on a plain hit
    sounds: tuple[int, ...]
    actors: tuple[Actor, ...]
    size: int


def parse_animation(data: bytes) -> AnimationScript:
    header = struct.unpack(">4h", checked_slice(data, 0, 8))
    offset, sounds = 8, []
    while struct.unpack(">H", checked_slice(data, offset, 2))[0] != 0xFFFF:
        sounds.append(struct.unpack_from(">h", data, offset)[0])
        offset += 2
    offset += 2
    actors = []
    while struct.unpack(">H", checked_slice(data, offset, 2))[0] != 0xFFFF:
        actors.append(Actor(*struct.unpack(">H3h3Hh", checked_slice(data, offset, 16))))
        offset += 16
    offset += 2
    if len(sounds) > MAX_SOUNDS or len(actors) > MAX_ACTORS or offset > SCRIPT_WINDOW:
        raise ValueError("Animation record exceeds the battle loader's limits")
    return AnimationScript(*header, tuple(sounds), tuple(actors), offset)


def read_animation_bank(rom: bytes, name: str) -> list[tuple[int, AnimationScript]]:
    spec = ANIMATION_BANKS[name]
    table = checked_slice(rom, spec["offsets"], spec["count"] * 4)
    bank = checked_slice(rom, spec["bank"], spec["bank_end"] - spec["bank"])
    if sha(table) != spec["offsets_sha256"] or sha(bank) != spec["bank_sha256"]:
        raise ValueError(f"Animation bank changed: {name}")
    scripts = []
    for (offset,) in struct.iter_unpack(">I", table):
        script = parse_animation(bank[offset:offset + SCRIPT_WINDOW])
        if offset + script.size > len(bank):
            raise ValueError("Animation record runs past its bank")
        scripts.append((offset, script))
    return scripts


# battle_scenes entries 984-1038 are the close-up cut-ins (atlases 1337-1364);
# weapon records reach them through ordinary actors.
CUTIN_REGISTRY = (984, 1038)

# Per-weapon battle record, 7 x s16 (load_00121560 func_801C3278). Field
# meanings come from static reads of their consumers and are unverified at run time.
BATTLE_WEAPONS = {"rom_offset": 0x119970, "count": 1329,
                  "sha256": "96a8978bf178b3fc905b7823de29eb02e78841255021c74634fdd2f4c21a2317"}
# Per-unit battle record, 7 x s16 (func_801C30D0); only 354 rows, units 354-362 have none.
BATTLE_UNITS = {"rom_offset": 0x118610, "count": 354,
                "sha256": "89fbbeff7a2f03bf8f8f6429f7023ee95e0c5ccf7b25dbd5c03afd4c5e3c124d"}


def _rows(rom: bytes, spec: dict) -> list[tuple[int, ...]]:
    raw = checked_slice(rom, spec["rom_offset"], spec["count"] * 14)
    if sha(raw) != spec["sha256"]:
        raise ValueError("Battle record table changed")
    return [struct.unpack_from(">7h", raw, i * 14) for i in range(spec["count"])]


def read_battle_weapons(rom: bytes) -> list[dict]:
    return [{"raw": list(r), "dialogue_weapon": r[0], "contact": r[3], "hit_pattern": r[5], "phase_code": r[6]}
            for r in _rows(rom, BATTLE_WEAPONS)]


def read_battle_units(rom: bytes) -> list[dict]:
    return [{"raw": list(r), "sprite_scale_percent": r[1], "shield_sprite": list(r[2:5]),
             "extra_scene": r[5], "explosion": r[6]} for r in _rows(rom, BATTLE_UNITS)]
