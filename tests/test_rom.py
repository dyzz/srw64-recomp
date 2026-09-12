from __future__ import annotations

from pathlib import Path
import struct
import unittest

from srw64_rom.baseline import Baseline, BaselineError, TextLayout, inspect_rom, verify_rom
from srw64_rom.text import TextIRError, parse_entries


def synthetic_rom() -> tuple[bytes, TextLayout]:
    rom = bytearray(0x400)
    pointer_offset = 0x100
    table_base = 0x200
    struct.pack_into(">I", rom, pointer_offset, table_base)
    struct.pack_into(">I", rom, table_base, 2)

    first_offset = 0x20
    second_offset = 0x30
    first = bytes.fromhex("0102030405060708") + struct.pack(">HHH", 0x002A, 0xFFFD, 0xFFFF)
    second = bytes.fromhex("1112131415161718") + struct.pack(">HHH", 0x0030, 0xFFFE, 0xFFFF)
    struct.pack_into(">II", rom, table_base + 4, first_offset, len(first))
    struct.pack_into(">II", rom, table_base + 12, second_offset, len(second))
    rom[table_base + first_offset : table_base + first_offset + len(first)] = first
    rom[table_base + second_offset : table_base + second_offset + len(second)] = second
    layout = TextLayout(
        pointer_table_offset=pointer_offset,
        table_count=1,
        entry_header_size=8,
        allowed_control_words=frozenset({0xFFFF, 0xFFFE, 0xFFFD}),
    )
    return bytes(rom), layout


class OriginalRomTests(unittest.TestCase):
    def test_rom_identity_gate_detects_any_byte_change(self) -> None:
        rom, layout = synthetic_rom()
        baseline = Baseline(
            path=Path("synthetic.json"),
            schema="srw64.rom-baseline.v1",
            name="synthetic",
            rom=inspect_rom(rom),
            text_layout=layout,
            reference={},
        )
        self.assertEqual(verify_rom(rom, baseline)["sha256"], inspect_rom(rom)["sha256"])
        changed = bytearray(rom)
        changed[-1] ^= 1
        with self.assertRaisesRegex(BaselineError, "sha256"):
            verify_rom(bytes(changed), baseline)

    def test_control_words_remain_distinct(self) -> None:
        rom, layout = synthetic_rom()
        entries = list(parse_entries(rom, layout))
        self.assertEqual(entries[0].units, (0x002A, 0xFFFD, 0xFFFF))
        self.assertEqual(entries[1].units, (0x0030, 0xFFFE, 0xFFFF))
        self.assertIn("FFFD", entries[0].to_record()["units_u16"])
        self.assertIn("FFFE", entries[1].to_record()["units_u16"])



    def test_unknown_negative_control_is_rejected(self) -> None:
        rom, layout = synthetic_rom()
        mutated = bytearray(rom)
        struct.pack_into(">H", mutated, 0x200 + 0x20 + 8 + 2, 0xFFFC)
        with self.assertRaisesRegex(TextIRError, "unapproved control"):
            list(parse_entries(bytes(mutated), layout))


if __name__ == "__main__":
    unittest.main()
