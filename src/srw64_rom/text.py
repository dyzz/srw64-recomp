from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any, Iterator

from .baseline import BaselineError, TextLayout, read_u32_be


IR_SCHEMA = "srw64.lossless-text-entry.v1"


class TextIRError(ValueError):
    """Raised when text tables or the lossless IR are structurally invalid."""


@dataclass(frozen=True)
class TextEntry:
    table_id: int
    text_id: int
    table_base: int
    descriptor_offset: int
    relative_offset: int
    data_offset: int
    byte_size: int
    header: bytes
    units: tuple[int, ...]

    @property
    def key(self) -> str:
        return f"t{self.table_id:02d}_{self.text_id:05d}"

    @property
    def data(self) -> bytes:
        return self.header + b"".join(struct.pack(">H", unit) for unit in self.units)

    def to_record(self) -> dict[str, Any]:
        return {
            "schema": IR_SCHEMA,
            "key": self.key,
            "table_id": self.table_id,
            "text_id": self.text_id,
            "table_base": self.table_base,
            "descriptor_offset": self.descriptor_offset,
            "relative_offset": self.relative_offset,
            "data_offset": self.data_offset,
            "byte_size": self.byte_size,
            "header_hex": self.header.hex(),
            "units_u16": " ".join(f"{unit:04X}" for unit in self.units),
        }


def _fail(message: str) -> TextIRError:
    return TextIRError(message)


def parse_entries(rom: bytes, layout: TextLayout) -> Iterator[TextEntry]:
    pointer_end = layout.pointer_table_offset + layout.table_count * 4
    if layout.pointer_table_offset < 0 or pointer_end > len(rom):
        raise _fail(
            f"text pointer table 0x{layout.pointer_table_offset:x}-0x{pointer_end:x} exceeds ROM"
        )
    for table_id in range(layout.table_count):
        pointer_offset = layout.pointer_table_offset + table_id * 4
        try:
            table_base = read_u32_be(rom, pointer_offset, f"table {table_id} pointer")
            entry_count = read_u32_be(rom, table_base, f"table {table_id} entry count")
        except BaselineError as exc:
            raise _fail(str(exc)) from exc
        descriptor_start = table_base + 4
        descriptor_end = descriptor_start + entry_count * 8
        if descriptor_end > len(rom):
            raise _fail(
                f"table {table_id}: descriptor array ends at 0x{descriptor_end:x}, beyond ROM"
            )

        for text_id in range(entry_count):
            descriptor_offset = descriptor_start + text_id * 8
            relative_offset, byte_size = struct.unpack_from(">II", rom, descriptor_offset)
            data_offset = table_base + relative_offset
            data_end = data_offset + byte_size
            label = f"table {table_id} text {text_id}"
            if byte_size < layout.entry_header_size:
                raise _fail(f"{label}: byte size {byte_size} is smaller than its header")
            body_size = byte_size - layout.entry_header_size
            if body_size % 2:
                raise _fail(f"{label}: glyph body has odd byte size {body_size}")
            if data_offset < 0 or data_end > len(rom):
                raise _fail(
                    f"{label}: data 0x{data_offset:x}-0x{data_end:x} exceeds ROM"
                )
            data = rom[data_offset:data_end]
            header = data[: layout.entry_header_size]
            body = data[layout.entry_header_size :]
            units = tuple(struct.unpack(f">{len(body) // 2}H", body))
            if not units or units[-1] != 0xFFFF:
                raise _fail(f"{label}: glyph stream does not end in FFFF")
            invalid_controls = sorted(
                {
                    unit
                    for unit in units
                    if unit >= 0x8000 and unit not in layout.allowed_control_words
                }
            )
            if invalid_controls:
                rendered = ", ".join(f"{unit:04X}" for unit in invalid_controls)
                raise _fail(f"{label}: unapproved control word(s): {rendered}")
            yield TextEntry(
                table_id=table_id,
                text_id=text_id,
                table_base=table_base,
                descriptor_offset=descriptor_offset,
                relative_offset=relative_offset,
                data_offset=data_offset,
                byte_size=byte_size,
                header=header,
                units=units,
            )
