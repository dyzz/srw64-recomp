"""web/src/lib/rom-art.ts, the unit maker's reader of the player's ROM, run under Node against
the ROM: its resources, scene rows, palettes and rendered frames agree with the ROM tools'
(src/srw64_native/battle_graphics.py), and a recolour changes only the entries named."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
MODULE = ROOT / "web/src/lib/rom-art.ts"
NODE = shutil.which("node")
ROM = ROOT / "rom.z64"


def run(script: str):
    result = subprocess.run([NODE, "--input-type=module", "-e", script], capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout)


@unittest.skipUnless(NODE and ROM.exists(), "node or the ROM is missing")
class RomArtTests(unittest.TestCase):
    ROWS = [56, 7, 27, 35, 121, 602, 926, 1016]   # beam, burst, bazooka flash, missile, a pose, slash, a shot, a cut-in

    @classmethod
    def setUpClass(cls):
        prelude = f"""import * as m from {json.dumps(MODULE.as_uri())};
import {{ readFileSync }} from 'node:fs';
const rom = m.toZ64(new Uint8Array(readFileSync({json.dumps(str(ROM))})));
const art = new m.RomArt(rom);
const out = {{}};
for (const row of {json.dumps(cls.ROWS)}) {{
  const [s, a, p] = art.triple(row), scene = art.scene(s), atlas = art.atlas(a), palette = art.palette(p);
  const f = m.firstFrame(scene), frame = m.renderFrame(scene, atlas, palette, f);
  let sum = 0; for (let i = 0; i < frame.rgba.length; i++) sum = (sum * 31 + frame.rgba[i]) >>> 0;
  out[row] = {{ triple: [s, a, p], bounds: scene.bounds, frame: f, size: [frame.width, frame.height], sum,
    used: [...m.usedEntries(scene, atlas).keys()].sort((x, y) => x - y), palette }};
}}
const pal = art.palette(art.triple(56)[2]);
out.recolour = m.recoloured(pal, {{ '1': '#20e060', '400': '#000000', '2': 'green' }});
out.hex = [m.toHex(0xFFFF), m.toHex(0x0001), m.fromHex('#20e060', 0), m.fromHex('#20e060', 1)];
const swapped = new Uint8Array(rom.subarray(0, 8)); for (let i = 0; i < 8; i += 2) [swapped[i], swapped[i + 1]] = [swapped[i + 1], swapped[i]];
out.v64 = Array.from(m.toZ64(swapped));
console.log(JSON.stringify(out));"""
        cls.result = run(prelude)
        cls.rom = ROM.read_bytes()

    def test_rows_and_frames_match_the_rom_tools(self):
        from srw64_native.battle_graphics import parse_scene, read_triplets, render_frame
        from srw64_native.battle_graphics import decode_atlas
        from srw64_rom.resources import ResourceTable   # the ROM tools' LZ resources
        table = ResourceTable(self.rom)
        read_resource = lambda _rom, index: table.extract(index)[0]
        triples = read_triplets(self.rom, "battle_scenes")
        for row in self.ROWS:
            got = self.result[str(row)]
            s, a, p = triples[row]
            self.assertEqual(got["triple"], [s, a, p], row)
            scene = parse_scene(read_resource(self.rom, s))
            self.assertEqual(got["bounds"], list(scene.bounds()), row)
            palette = read_resource(self.rom, p)
            count = min(256, struct.unpack_from(">H", palette, 2)[0] // 2)
            self.assertEqual(got["palette"], list(struct.unpack_from(f">{count}H", palette, 8)), row)
            atlas, _ = decode_atlas(read_resource(self.rom, a), palette)
            image, _ = render_frame(scene.frames[got["frame"]], atlas.convert("RGBA"), scene.bounds())
            self.assertEqual(got["size"], list(image.size), row)
            # Same pixels (transparent ones as 0, 0, 0, 0 like the page's).
            sum_, raw = 0, image.convert("RGBA").tobytes()
            for i in range(0, len(raw), 4):
                for v in (raw[i:i + 4] if raw[i + 3] else b"\0\0\0\0"):
                    sum_ = (sum_ * 31 + v) & 0xFFFFFFFF
            self.assertEqual(got["sum"], sum_, row)
            self.assertTrue(got["used"], row)

    def test_recolour_changes_only_what_it_names(self):
        palette = self.result["56"]["palette"]
        got = self.result["recolour"]
        self.assertEqual(got[2:], palette[2:])
        self.assertEqual(got[0], palette[0])
        self.assertEqual(got[1], (4 << 11 | 28 << 6 | 12 << 1) | (palette[1] & 1))   # what mod_units.hpp recolor writes
        self.assertEqual(self.result["hex"], ["#ffffff", "#000000", 4 << 11 | 28 << 6 | 12 << 1, (4 << 11 | 28 << 6 | 12 << 1) | 1])

    def test_byte_orders(self):
        self.assertEqual(bytes(self.result["v64"]), self.rom[:8])
        self.assertEqual(hashlib.sha256(self.rom).hexdigest(), run(
            f"import * as m from {json.dumps(MODULE.as_uri())}; console.log(JSON.stringify(m.ROM_SHA256));"))


if __name__ == "__main__":
    unittest.main()
