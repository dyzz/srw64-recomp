"""The table window (docs/design/new-unit-ids.md §3): the generator moves the readers to the
same blocks the host serves (src/host/table_window.hpp), and every reader the audit finds
for a moved table is moved."""
import importlib.util
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
HEADER = (ROOT / "src/host/table_window.hpp").read_text()
GENERATED = ROOT / "build/recomp/cpu-bound/generated"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class TableWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generate = load("generate_cpu", ROOT / "tools/recomp/toolchain/generate_cpu.py")

    def header_constant(self, name):
        found = re.search(rf"\b{name}=(0x[0-9A-Fa-f]+)", HEADER)
        self.assertIsNotNone(found, name)
        return int(found[1], 16)

    def test_layout_matches_the_host(self):
        g = self.generate
        self.assertEqual(g.TABLE_WINDOW, self.header_constant("window"))
        self.assertEqual(g.TABLE_BLOCK, self.header_constant("block"))
        self.assertEqual(g.MAP_ICONS_RAM, self.header_constant("map_icons_ram"))
        self.assertEqual(g.MAP_ICONS_COPY, self.header_constant("map_icons_copy"))
        order = re.search(r"enum Table : unsigned \{([^}]*)\}", HEADER)[1].replace("\n", " ")
        names = [n.strip() for n in order.split(",") if n.strip()]
        layouts = re.findall(r"\{(0x[0-9A-Fa-f]+|0),(0x[0-9A-Fa-f]+|\d+),(\d+)\}", HEADER.split("layouts[table_count]={")[1].split("};")[0])
        # The loader's raised cap is the host's limit on resource numbers.
        cap = g.MAP_WIDTH_CONSTANTS[("resident_func_80089E9C", 0x80089EAC)]
        self.assertEqual(cap[0], "0X1924")
        # Raised only by the packages' own resources (mod_tables.hpp resource_limit_now).
        self.assertEqual(cap[1], "srw64_resource_limit()")
        self.assertEqual(g.NATIVE_HOOKS["resident_func_80089BBC"], "srw64_original_resource_descriptor")
        # The quote picker's weapon (8022241C) takes the new weapons' numbers too.
        last_weapon = int(re.search(r"last_new_weapon=(\d+)", HEADER)[1])
        self.assertEqual(g.MAP_WIDTH_CONSTANTS[("load_00121560_func_8022241C", 0x80222424)], ("0X531", f"0X{last_weapon + 1:X}"))
        for key, (block, rom) in g.TABLE_BLOCKS.items():
            self.assertEqual(names.index(key), block, key)
            self.assertEqual(int(layouts[block][0], 16), rom, key)

    def test_every_moved_table_reader_is_moved(self):
        # The audit's readers of the moved tables (lui + addiu building the table's base) are
        # the generator's, but for the debug menu's sample data (load_000856D0), never run.
        if not GENERATED.is_dir():
            self.skipTest("no generated code (run generate_cpu.py)")
        audit = load("audit_table_readers", ROOT / "tools/recomp/audit_table_readers.py")
        hits, _ = audit.scan(GENERATED)
        bases = {key: rom for key, (_, rom) in self.generate.TABLE_BLOCKS.items()}
        # The character tables (docs/design/unit-mod-kit.md §5.3) are the audit's --pilots set.
        unit_tables = list(audit.TABLES)
        audit.TABLES[:] = audit.PILOT_TABLES
        try:
            pilot_hits, _ = audit.scan(GENERATED)
        finally:
            audit.TABLES[:] = unit_tables
        hits = hits + pilot_hits
        found = {(h["function"], int(h["address"], 16)) for h in hits
                 if h["table"] in bases and h["how"] == "常量" and int(h["value"], 16) == bases[h["table"]]}
        found = {f for f in found if not f[0].startswith("load_000856D0")}
        # 0x1161C0 is also where an overlay's code ends (8007FEB0 loads 0x1156A0-0x1161C0).
        found -= {("resident_func_8007FEB0", 0x8007FECC)}
        self.assertEqual(found, set(self.generate.TABLE_READERS))
        icons = {(h["function"], int(h["address"], 16)) for h in hits if h["table"] == "map_icons_ram" and h["value"] == "80218218"}
        icons -= {("load_000AB160_func_801C60A4", 0x801C6158)}   # reads through 801C60C0's moved base
        self.assertEqual(icons, set(self.generate.MAP_ICON_READERS))
        # The character -> record maps: every reader goes to a copy (the stats', the quotes' or
        # the spirits'), but for reads through a base already moved in the same function.
        maps = {(h["function"], int(h["address"], 16)) for h in pilot_hits
                if h["table"] in ("pilot_records_map", "spirit_map") and h["value"] in ("800CA9C4", "800CA6F4")}
        maps -= {("srw64_original_demo_battle_fill", 0x8009C480), ("srw64_original_battle_quote_show", 0x801C6FAC),
                 ("srw64_original_battle_quote_show", 0x801C6FB0)}
        self.assertEqual(maps, set(self.generate.PILOT_MAP_READERS))
        # The form families (mod_forms.hpp): the list's one walk and every read of the pointers,
        # but for 801E174C, which reads through 801E170C's moved base.
        forms = {(h["function"], int(h["address"], 16)) for h in hits if h["table"] == "forms" and h["how"] == "常量"}
        self.assertEqual(forms, set(self.generate.FORMS_LIST_READERS))
        pointers = {(h["function"], int(h["address"], 16)) for h in hits if h["table"] == "forms_pointers"}
        pointers -= {("load_000AB160_func_801E1688", 0x801E174C)}
        self.assertEqual(pointers, set(self.generate.FORMS_POINTER_READERS))


if __name__ == "__main__":
    unittest.main()
