"""The map's HP window in a widened picture (docs/design/deck-16x10.md §2, 2026-11-26).

Render slot 0x2E's position (80101DAC x, 80101DB0 y) is shared: 801E3AE4 places the HP window
there on the centred 320, and the gauge's node callbacks (801E38FC and two siblings) draw at
that slot's x on the centred 320 too. The damage figure starts from the same x but 80209900
draws it in the widened map view. So the store must stay the original's and only the reads
that hand the x to the figure (801FCC6C) may add the half of the extra width. Widening the
store moved the gauge out of its window on every renderer (2026-10-12, and 11-04 to 11-26).
tools/recomp/debug/check_map_hp_window.py checks the same on screen."""
import importlib.util
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "build/recomp/cpu-bound/generated"
FIGURE_READERS = {
    ("load_000AB160_func_801D7490", 0x801D75EC), ("load_000AB160_func_801D7778", 0x801D7ABC),
    ("load_000AB160_func_801D8BB0", 0x801D8C98), ("load_000AB160_func_801D95B8", 0x801D98E4),
    ("load_000AB160_func_801DA478", 0x801DA714), ("load_000AB160_func_801DBBAC", 0x801DBCE4),
}
WRITERS = {
    ("load_000AB160_func_801CABAC", 0x801CACDC), ("srw64_original_map_menu_open", 0x801D1448),
    ("load_000AB160_func_801E3AE4", 0x801E3D28),
}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class MapHpWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generate = load("generate_cpu", ROOT / "tools/recomp/toolchain/generate_cpu.py")

    def test_window_place_keeps_the_original(self):
        # Only the anchor's scroll read (801E3B1C) changes in 801E3AE4: the window and the
        # gauge stay on the centred 320.
        entries = sorted(a for f, a in self.generate.MAP_WIDTH_CONSTANTS if f == "load_000AB160_func_801E3AE4")
        self.assertEqual(entries, [0x801E3B1C])

    def test_figure_readers_take_the_half(self):
        for key in FIGURE_READERS:
            literal, halves = self.generate.MAP_WIDTH_CONSTANTS[key]
            self.assertEqual(literal, "MEM_W(ctx->r1, 0X1DAC)", key)
            self.assertEqual(halves, "srw64_map_x_float(MEM_W(ctx->r1, 0X1DAC), 1)", key)

    def test_map_miss_takes_the_half(self):
        source = (ROOT / "src/host/map_miss.hpp").read_text()
        self.assertIn("read_float(ram, figure_x) + float(wide_map::extra()) / 2", source)

    @unittest.skipUnless(GENERATED.is_dir(), "the generated CPU sources are not built")
    def test_every_absolute_access_is_accounted_for(self):
        # Every read of 80101DAC by absolute address in the generated code is one of the six
        # figure readers and is widened, and its writers are the three windows that open beside
        # a unit, each placing it on the centred 320 (801CABAC the terrain panel, 801D13C0 the
        # unit's menu through 801C9D08, 801E3AE4 the HP window). A new reader or writer fails
        # here first: decide which space it is in before adding it.
        access = re.compile(r"// 0x([0-9A-F]{8}): (lwc1|swc1|lw|sw)\s+\$\w+, 0x1DAC\(\$at\)")
        reads, stores = set(), set()
        for path in sorted(GENERATED.glob("funcs_*.c")):
            function = ""
            lines = path.read_text().split("\n")
            for index, line in enumerate(lines):
                if line.startswith("RECOMP_FUNC void "):
                    function = line[len("RECOMP_FUNC void "):line.index("(")]
                    continue
                found = access.search(line)
                if not found or "lui         $at, 0x8010" not in "\n".join(lines[max(0, index - 6):index]):
                    continue
                statement = lines[index + 1]
                if found[2].startswith("l"):
                    reads.add((function, int(found[1], 16)))
                    self.assertIn("srw64_map_x_float(", statement, (function, found[1]))
                else:
                    stores.add((function, int(found[1], 16)))
        self.assertEqual(reads, FIGURE_READERS)
        self.assertEqual(stores, WRITERS)


if __name__ == "__main__":
    unittest.main()
