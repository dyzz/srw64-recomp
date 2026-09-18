from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[1] / "tools/recomp/toolchain/analyze_layout.py"
spec = importlib.util.spec_from_file_location("recomp_layout", path)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LayoutTests(unittest.TestCase):
    def test_signed_low_half_and_delay_slot_size(self) -> None:
        # A synthetic complete loader family: one wrapper then no-op wrappers.
        begin = module.LOADER_START - module.MAIN_DELTA
        end = module.LOADER_END - module.MAIN_DELTA
        rom = bytearray(end + 8)
        words = [0x3C040001, 0x24848000,  # a0 = 0x8000, not 0x18000
                 0x3C05801C, 0x34A52600,  # a1 = 0x801C2600
                 0x3C060001, 0x24C68020,  # a2 = 0x8020
                 0x0C01FDC1, 0x00C43023,  # ROM read; delay computes a2 -= a0
                 0x03E00008, 0]
        struct.pack_into(">" + "I" * len(words), rom, begin, *words)
        transfers = module.recover_loaders(bytes(rom))
        self.assertEqual(len(transfers), 1)
        self.assertEqual(transfers[0]["rom_start"], 0x8000)
        self.assertEqual(transfers[0]["size"], 0x20)
        self.assertEqual(transfers[0]["vram"], 0x801C2600)

    def test_unknown_instruction_does_not_become_a_constant(self) -> None:
        with self.assertRaises(module.LayoutError):
            module.step_constants(0x10800001, [0] + [None] * 31)

    def test_unknown_input_propagates(self) -> None:
        registers = [0] + [None] * 31
        module.step_constants(0x24840004, registers)
        self.assertIsNone(registers[4])
