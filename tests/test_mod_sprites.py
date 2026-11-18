"""A package sprite as the host builds it (src/host/mod_sprites.hpp, left by `make
recomp-mod-data-test`) read back with the ROM tools' own decoders (battle_graphics.parse_scene,
decode_atlas, render_scene): the same picture comes out, mirrored set included."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.battle_graphics import decode_atlas, mode0_vertex_quad, parse_scene, render_scene  # noqa: E402

RUN = ROOT / "build/recomp/mod-data-test/run"


@unittest.skipUnless((RUN / "sprite.scene").is_file(), "run make recomp-mod-data-test first")
class ModSpriteTest(unittest.TestCase):
    def test_reads_back(self):
        scene = parse_scene((RUN / "sprite.scene").read_bytes())
        data = (RUN / "sprite.scene").read_bytes()
        atlas, extra = decode_atlas((RUN / "sprite.atlas").read_bytes(), (RUN / "sprite.palette").read_bytes())
        self.assertEqual((scene.vertex_mode, scene.steps, scene.loop_step, len(scene.frames)), (0, ((0, 1),), 0, 1))
        self.assertEqual(scene.bounds(), (-20, -40, 44, 24))   # 40x40 at origin (20, 40), in 32-pixel parts
        self.assertEqual(extra, 0)
        for part in scene.frames[0]:
            self.assertEqual(mode0_vertex_quad(data, part), (part.x, part.y, 32, 32, False))
        frames, clipped = render_scene(scene, atlas)
        image = frames[0]
        self.assertEqual(clipped, 0)
        # The test's picture: red left half, green top half, blue 40 everywhere, 40x40 opaque.
        self.assertEqual(image.getpixel((0, 0))[:3], (255, 255, 41))
        self.assertEqual(image.getpixel((39, 39))[:3], (0, 0, 41))
        self.assertEqual(image.getpixel((25, 5))[:3], (0, 255, 41))
        self.assertEqual(image.getpixel((41, 0))[3], 0)


if __name__ == "__main__":
    unittest.main()
