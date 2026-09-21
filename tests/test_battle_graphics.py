from pathlib import Path
import struct
import unittest

from PIL import Image

from srw64_native.battle_graphics import (ANIMATION_BANKS, CUTIN_REGISTRY, TRIPLET_TABLES, animated_png,
                                          decode_atlas, mode0_vertex_quad, parse_animation, parse_scene,
                                          read_animation_bank, read_battle_units, read_battle_weapons,
                                          read_map_movies, read_triplets, render_scene)
from srw64_rom.resources import ResourceTable

ROOT = Path(__file__).resolve().parents[1]


def part(flags, s, t, w, h, x, y, vertex=0):
    return struct.pack(">3H2B2hI", flags, s, t, w, h, x, y, vertex)


def scene(steps, frames, loop=0, mode=2):
    head = bytes([len(steps), mode]) + b"".join(bytes(s) for s in steps) + bytes([0xFF, loop])
    table = len(head) + 2 * len(frames)
    offsets, body = [], b""
    for frame in frames:
        offsets.append(table + len(body))
        body += b"".join(frame) + part(0x8000, 0, 0, 0, 0, 0, 0)
    return head + struct.pack(f">{len(offsets)}H", *offsets) + body


class SceneDecoderTests(unittest.TestCase):
    def setUp(self):
        self.atlas = Image.new("RGBA", (4, 2))
        self.atlas.putpixel((0, 0), (255, 0, 0, 255))
        self.atlas.putpixel((1, 0), (0, 255, 0, 255))

    def test_steps_blank_frames_loop_and_flip(self):
        data = scene([(0, 2), (0xFF, 3), (1, 1)],
                     [[part(0, 0, 0, 2, 1, -1, -1)], [part(0x10, 0, 0, 2, 1, 0, -1)]], loop=1)
        parsed = parse_scene(data)
        self.assertEqual(parsed.steps, ((0, 2), (0xFF, 3), (1, 1)))
        self.assertEqual(parsed.loop_step, 1)
        self.assertEqual(parsed.bounds(), (-1, -1, 2, 0))
        images, clipped = render_scene(parsed, self.atlas)
        self.assertEqual(clipped, 0)
        self.assertEqual([images[0].getpixel((x, 0)) for x in range(3)],
                         [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 0, 0)])
        self.assertEqual([images[1].getpixel((x, 0)) for x in range(3)],
                         [(0, 0, 0, 0), (0, 255, 0, 255), (255, 0, 0, 255)])
        animation = Image.open(__import__("io").BytesIO(animated_png(parsed, images, 10)))
        self.assertEqual(animation.n_frames, 3)

    def test_rejects_malformed_scenes(self):
        good = scene([(0, 1)], [[part(0, 0, 0, 1, 1, 0, 0)]])
        for bad in (good[:1],                                   # truncated header
                    bytes([1, 3]) + good[2:],                   # unknown vertex mode
                    good[:4] + b"\x00\x00" + good[6:],          # unterminated steps
                    scene([(2, 1)], [[part(0, 0, 0, 1, 1, 0, 0)]]),  # missing frame
                    scene([(0, 1)], [[part(0x20, 0, 0, 1, 1, 0, 0)]]),  # unknown part flag
                    good[:-4]):                                 # frame runs off the end
            with self.assertRaises(ValueError):
                parse_scene(bad)

    def test_wide_palette_keeps_indexable_colours(self):
        image = struct.pack(">4H", 7, 1, 1, 0) + b"\x01"
        palette = struct.pack(">4H", 3, 600 * 2, 0, 0) + struct.pack(">600H", *([0xF801] * 600))
        atlas, unused = decode_atlas(image, palette)
        self.assertEqual((atlas.getpixel((0, 0)), unused), ((255, 0, 0, 255), 344))

    def test_animation_record_layout_and_limits(self):
        actor = struct.pack(">H3h3Hh", 56, -65, 28, 0, 0, 255, 0, 118)
        data = struct.pack(">4h", 1, 6, 113, 125) + struct.pack(">3hH", -2, 1, 5, 0xFFFF) + actor + b"\xff\xff"
        script = parse_animation(data)
        self.assertEqual((script.camera, script.hit_script, script.action, script.defender_action),
                         (1, 6, 113, 125))
        self.assertEqual(script.sounds, (-2, 1, 5))
        self.assertEqual((script.actors[0].registry, script.actors[0].x, script.actors[0].behavior), (56, -65, 118))
        self.assertEqual(script.size, len(data))
        too_many = struct.pack(">4h", 0, 0, 0, 0) + b"\xff\xff" + actor * 25 + b"\xff\xff"
        for bad in (data[:-2], too_many):
            with self.assertRaises(ValueError):
                parse_animation(bad)


@unittest.skipUnless((ROOT / "rom.z64").exists(), "Local original ROM required")
class BattleGraphicsROMTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (ROOT / "rom.z64").read_bytes()
        cls.resources = ResourceTable(cls.rom)

    def test_tables_bindings_and_every_scene_parses(self):
        tables = {name: read_triplets(self.rom, name) for name in TRIPLET_TABLES}
        self.assertEqual(tables["unit_poses"][0], (2213, 1612, 1908))  # ガンダムシュピーゲル
        self.assertEqual(tables["unit_poses"][5], (4972, 4930, 4931))  # ドモン・カッシュ on foot
        self.assertEqual(tables["unit_poses"][362], (2216, 1615, 1911))  # placeholder → Shining Gundam
        self.assertEqual(tables["unit_poses"][363:], [(0, 0, 0)] * 2)
        self.assertEqual(sum(1 for t in tables["battle_scenes"] if t[0]), 1051)
        mode0_parts = 0
        for triplets in tables.values():
            for sid, aid, pid in {t for t in triplets if t[0]}:
                data = self.resources.extract(sid)[0]
                parsed = parse_scene(data)
                atlas, _ = decode_atlas(self.resources.extract(aid)[0], self.resources.extract(pid)[0])
                self.assertGreater(atlas.width, 0)
                if parsed.vertex_mode == 0:
                    for frame in parsed.frames:
                        for p in frame:
                            self.assertEqual(mode0_vertex_quad(data, p), (p.x, p.y, p.w, p.h, p.flip_x))
                            mode0_parts += 1
        self.assertGreater(mode0_parts, 19000)

    def test_animation_banks_reference_real_registry_scenes(self):
        registry = read_triplets(self.rom, "battle_scenes")
        banks = {name: read_animation_bank(self.rom, name) for name in ANIMATION_BANKS}
        self.assertEqual({k: len(v) for k, v in banks.items()}, {"weapon": 1329, "hit": 159, "reaction": 28})
        for entries in banks.values():
            for _, script in entries:
                for actor in script.actors:
                    self.assertTrue(registry[actor.registry][0])
                    self.assertTrue(3 <= actor.behavior <= 382)
        weapons = banks["weapon"]
        self.assertEqual([a.registry for a in weapons[0][1].actors], [222])       # アイアンネット
        self.assertEqual([a.registry for a in weapons[228][1].actors], [121, 0, 56])  # ビームライフル
        self.assertNotEqual(weapons[29][0], weapons[24][0])  # indexed by weapon id, not the dialogue id
        cut = range(CUTIN_REGISTRY[0], CUTIN_REGISTRY[1] + 1)
        users = {w for w, (_, s) in enumerate(weapons) for a in s.actors if a.registry in cut}
        self.assertEqual(users, {18, 19, 24, 29, 742, 778, 820, 821, 874, 881, 1062, 1075, 1216, 1217})
        self.assertEqual(read_battle_weapons(self.rom)[29]["dialogue_weapon"], 24)
        self.assertEqual(len(read_battle_units(self.rom)), 354)

    def test_map_movies(self):
        movies = read_map_movies(self.rom)
        self.assertEqual([len(t) for *_, t in movies], [13, 7, 9, 9, 9, 5, 5, 5, 5, 5, 5, 18])
        self.assertEqual(movies[11][3][0], (1486, 1472, 1479))  # ゴッドマーズ 合体
        for *_, triplets in movies:
            for sid, aid, pid in triplets:
                parse_scene(self.resources.extract(sid)[0])


if __name__ == "__main__":
    unittest.main()
