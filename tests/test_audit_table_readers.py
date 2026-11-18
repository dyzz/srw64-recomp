"""tools/recomp/audit_table_readers.py: following lui values to the tables
(docs/design/new-unit-ids.md §2), on synthetic generated C and, when it is there, on the
build's generated code."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "build/recomp/cpu-bound/generated"


def load():
    spec = importlib.util.spec_from_file_location("audit_table_readers", ROOT / "tools/recomp/audit_table_readers.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["audit_table_readers"] = module
    spec.loader.exec_module(module)
    return module


SYNTHETIC = """
RECOMP_FUNC void resident_func_80001000(uint8_t* rdram, recomp_context* ctx) {
    // 0x80001000: lui         $v0, 0x7
    // 0x80001004: addiu       $v0, $v0, 0x1B80
    // 0x80001008: jal         0x8007F704
    // 0x8000100C: addu        $a0, $a0, $v0
    // 0x8000100C: addu        $a0, $a0, $v0
    // 0x80001010: sltiu       $v0, $a1, 0x16B
    // 0x80001014: lui         $v1, 0x7
    // 0x80001018: addiu       $v1, $v1, 0x2000
}
RECOMP_FUNC void load_000AB160_func_801C5000(uint8_t* rdram, recomp_context* ctx) {
    // 0x801C5000: lui         $s0, 0x8022
    // 0x801C5004: addu        $s0, $s0, $v0
    // 0x801C5008: lhu         $s0, -0x7DE8($s0)
}
RECOMP_FUNC void load_00121560_func_801C5000(uint8_t* rdram, recomp_context* ctx) {
    // 0x801C5000: lui         $s0, 0x8022
    // 0x801C5004: addu        $s0, $s0, $v0
    // 0x801C5008: lhu         $s0, -0x7DE8($s0)
}
RECOMP_FUNC void resident_func_80002000(uint8_t* rdram, recomp_context* ctx) {
    // 0x80002000: lui         $v0, 0x7
    // 0x80002004: lw          $v0, 0x0($a0)
    // 0x80002008: addiu       $v0, $v0, 0x1B80
}
"""


class AuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = load()

    def scan(self, text):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "funcs_0.c").write_text(text)
            return self.audit.scan(Path(directory))

    def test_follows_lui_into_tables(self):
        hits, limits = self.scan(SYNTHETIC)
        found = {(h["table"], h["function"], h["address"]) for h in hits}
        # The unit table's base, built by lui + addiu; 0x72000 is inside it as well.
        self.assertIn(("units", "resident_func_80001000", "80001004"), found)
        self.assertIn(("units", "resident_func_80001000", "80001018"), found)
        # An overlay's RAM table counts only in that overlay (others share the addresses).
        self.assertIn(("map_icons_ram", "load_000AB160_func_801C5000", "801C5008"), found)
        self.assertFalse(any(h["function"] == "load_00121560_func_801C5000" for h in hits))
        # A load into the register drops what lui put there.
        self.assertFalse(any(h["function"] == "resident_func_80002000" for h in hits))
        self.assertEqual([(l["function"], l["limit"]) for l in limits], [("resident_func_80001000", "363 机体数")])

    def test_delay_slot_listed_twice_counts_once(self):
        hits, _ = self.scan(SYNTHETIC)
        self.assertEqual(sum(1 for h in hits if h["address"] == "80001004"), 1)

    @unittest.skipUnless(GENERATED.is_dir(), "no generated code (run generate_cpu.py)")
    def test_build_readers(self):
        hits, limits = self.audit.scan(GENERATED)
        functions = lambda table: {h["function"] for h in hits if h["table"] == table}
        # The readers new-unit-ids.md §2 counts on: a new one means the plan has a gap.
        self.assertEqual(functions("weapons"), {"resident_func_800A642C"})
        self.assertEqual(functions("poses"), {"resident_func_8009C864"})
        self.assertEqual(functions("battle_units"), {"load_00121560_func_801C30D0", "load_00216730_func_801C2634"})
        self.assertEqual(functions("scenes"), {"load_00121560_func_801C3170", "load_00216730_func_801C26D4"})
        self.assertEqual(len(functions("units")), 5)
        self.assertEqual(len(functions("map_icons_ram")), 21)
        self.assertIn(("load_00121560_func_8022241C", "1329 武器数"), {(l["function"], l["limit"]) for l in limits})


if __name__ == "__main__":
    unittest.main()
