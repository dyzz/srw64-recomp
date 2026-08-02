from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
from itertools import zip_longest
import json
import os
from pathlib import Path
import struct
import tempfile
from typing import Any, Iterable, Iterator

from .baseline import BaselineError, TextLayout, read_u32_be, sha256_file


IR_SCHEMA = "srw64.lossless-text-entry.v1"
MANIFEST_SCHEMA = "srw64.w0-manifest.v1"


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


def _atomic_text_writer(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    return tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="\n",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    )


def extract_ir(rom: bytes, layout: TextLayout, output_path: Path) -> dict[str, Any]:
    table_counts: Counter[int] = Counter()
    control_counts: Counter[int] = Counter()
    total_units = 0
    maximum_text_end = 0
    temp_name: str | None = None
    try:
        with _atomic_text_writer(output_path) as handle:
            temp_name = handle.name
            for entry in parse_entries(rom, layout):
                handle.write(json.dumps(entry.to_record(), ensure_ascii=False, separators=(",", ":")))
                handle.write("\n")
                table_counts[entry.table_id] += 1
                total_units += len(entry.units)
                maximum_text_end = max(maximum_text_end, entry.data_offset + entry.byte_size)
                control_counts.update(
                    unit for unit in entry.units if unit in layout.allowed_control_words
                )
        os.replace(temp_name, output_path)
        temp_name = None
    finally:
        if temp_name is not None:
            Path(temp_name).unlink(missing_ok=True)

    return {
        "entry_count": sum(table_counts.values()),
        "unit_count": total_units,
        "maximum_text_end": maximum_text_end,
        "table_entry_counts": {
            str(table_id): table_counts[table_id] for table_id in range(layout.table_count)
        },
        "control_word_counts": {
            f"{unit:04X}": control_counts[unit]
            for unit in sorted(layout.allowed_control_words, reverse=True)
        },
        "sha256": sha256_file(output_path),
        "byte_size": output_path.stat().st_size,
    }


def _parse_record(document: Any, line_number: int) -> TextEntry:
    if not isinstance(document, dict):
        raise _fail(f"IR line {line_number}: expected a JSON object")
    if document.get("schema") != IR_SCHEMA:
        raise _fail(f"IR line {line_number}: unsupported schema")
    required_ints = (
        "table_id",
        "text_id",
        "table_base",
        "descriptor_offset",
        "relative_offset",
        "data_offset",
        "byte_size",
    )
    values: dict[str, int] = {}
    for field in required_ints:
        value = document.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise _fail(f"IR line {line_number}: {field} must be a nonnegative integer")
        values[field] = value
    try:
        header = bytes.fromhex(document.get("header_hex", ""))
    except (TypeError, ValueError) as exc:
        raise _fail(f"IR line {line_number}: invalid header_hex") from exc
    units_text = document.get("units_u16")
    if not isinstance(units_text, str):
        raise _fail(f"IR line {line_number}: units_u16 must be a string")
    try:
        units = tuple(int(piece, 16) for piece in units_text.split())
    except ValueError as exc:
        raise _fail(f"IR line {line_number}: invalid units_u16") from exc
    if any(unit < 0 or unit > 0xFFFF for unit in units):
        raise _fail(f"IR line {line_number}: units_u16 contains a value outside u16")

    entry = TextEntry(header=header, units=units, **values)
    if document.get("key") != entry.key:
        raise _fail(
            f"IR line {line_number}: key {document.get('key')!r} does not match {entry.key!r}"
        )
    if len(entry.data) != entry.byte_size:
        raise _fail(
            f"IR line {line_number}: reconstructed size {len(entry.data)} != {entry.byte_size}"
        )
    return entry


def iter_ir(path: Path) -> Iterator[TextEntry]:
    try:
        handle = path.open("r", encoding="utf-8")
    except OSError as exc:
        raise _fail(f"cannot read IR {path}: {exc}") from exc
    with handle:
        for line_number, line in enumerate(handle, start=1):
            if not line.strip():
                raise _fail(f"IR line {line_number}: blank lines are not permitted")
            try:
                document = json.loads(line)
            except json.JSONDecodeError as exc:
                raise _fail(f"IR line {line_number}: invalid JSON: {exc.msg}") from exc
            yield _parse_record(document, line_number)


def validate_ir(rom: bytes, layout: TextLayout, ir_path: Path) -> dict[str, Any]:
    checked = 0
    for index, pair in enumerate(
        zip_longest(parse_entries(rom, layout), iter_ir(ir_path)), start=1
    ):
        expected, actual = pair
        if expected is None:
            raise _fail(f"IR has an extra record at line {index}")
        if actual is None:
            raise _fail(f"IR ends before ROM entry {expected.key}")
        if expected != actual:
            expected_record = expected.to_record()
            actual_record = actual.to_record()
            differing = [
                key
                for key in expected_record
                if expected_record.get(key) != actual_record.get(key)
            ]
            raise _fail(
                f"IR line {index} ({expected.key}) differs from ROM fields: {', '.join(differing)}"
            )
        checked += 1
    return {
        "entry_count": checked,
        "ir_sha256": sha256_file(ir_path),
        "lossless_match": True,
    }


def build_noop_rom(rom: bytes, layout: TextLayout, ir_path: Path, output_path: Path) -> dict[str, Any]:
    validate_ir(rom, layout, ir_path)
    patched = bytearray(rom)
    written = 0
    for entry in iter_ir(ir_path):
        struct.pack_into(
            ">II",
            patched,
            entry.descriptor_offset,
            entry.relative_offset,
            entry.byte_size,
        )
        patched[entry.data_offset : entry.data_offset + entry.byte_size] = entry.data
        written += 1

    source_hash = hashlib.sha256(rom).hexdigest()
    output_hash = hashlib.sha256(patched).hexdigest()
    if patched != rom:
        raise _fail("no-op reconstruction changed ROM bytes")
    if output_path.resolve() == Path.cwd().resolve():
        raise _fail("output path must be a file, not the current directory")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb",
        dir=output_path.parent,
        prefix=f".{output_path.name}.",
        suffix=".tmp",
        delete=False,
    ) as handle:
        temp_name = handle.name
        handle.write(patched)
    try:
        os.replace(temp_name, output_path)
    finally:
        Path(temp_name).unlink(missing_ok=True)
    return {
        "entry_count": written,
        "source_sha256": source_hash,
        "output_sha256": output_hash,
        "byte_identical": True,
        "output_size": len(patched),
    }


def write_json(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    with _atomic_text_writer(path) as handle:
        temp_name = handle.name
        handle.write(payload)
    try:
        os.replace(temp_name, path)
    finally:
        Path(temp_name).unlink(missing_ok=True)


def read_manifest(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise _fail(f"cannot read manifest {path}: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema") != MANIFEST_SCHEMA:
        raise _fail("unsupported or missing W0 manifest schema")
    return document


def verify_manifest(
    manifest: dict[str, Any], rom_sha256: str, ir_path: Path
) -> None:
    expected_rom = manifest.get("rom", {}).get("sha256")
    expected_ir = manifest.get("text_ir", {}).get("sha256")
    actual_ir = sha256_file(ir_path)
    if expected_rom != rom_sha256:
        raise _fail(
            f"manifest ROM hash {expected_rom!r} does not match input {rom_sha256!r}"
        )
    if expected_ir != actual_ir:
        raise _fail(
            f"manifest IR hash {expected_ir!r} does not match input {actual_ir!r}"
        )
