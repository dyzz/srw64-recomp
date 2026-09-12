from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
from typing import Any


class BaselineError(ValueError):
    """Raised when a ROM or baseline description fails validation."""


def _parse_int(value: int | str, field: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise BaselineError(f"{field}: invalid integer {value!r}") from exc
    raise BaselineError(f"{field}: expected integer or integer string")


@dataclass(frozen=True)
class TextLayout:
    pointer_table_offset: int
    table_count: int
    entry_header_size: int
    allowed_control_words: frozenset[int]


@dataclass(frozen=True)
class Baseline:
    path: Path
    schema: str
    name: str
    rom: dict[str, Any]
    text_layout: TextLayout
    reference: dict[str, Any]


def load_baseline(path: Path) -> Baseline:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BaselineError(f"cannot read baseline {path}: {exc}") from exc

    if document.get("schema") != "srw64.rom-baseline.v1":
        raise BaselineError("unsupported or missing baseline schema")
    rom = document.get("rom")
    layout = document.get("text_layout")
    if not isinstance(rom, dict) or not isinstance(layout, dict):
        raise BaselineError("baseline requires rom and text_layout objects")

    controls = layout.get("allowed_control_words", [])
    if not isinstance(controls, list):
        raise BaselineError("allowed_control_words must be a list")
    parsed_layout = TextLayout(
        pointer_table_offset=_parse_int(layout.get("pointer_table_offset"), "pointer_table_offset"),
        table_count=_parse_int(layout.get("table_count"), "table_count"),
        entry_header_size=_parse_int(layout.get("entry_header_size"), "entry_header_size"),
        allowed_control_words=frozenset(
            _parse_int(value, "allowed_control_words") for value in controls
        ),
    )
    if parsed_layout.table_count <= 0:
        raise BaselineError("table_count must be positive")
    if parsed_layout.entry_header_size <= 0:
        raise BaselineError("entry_header_size must be positive")

    return Baseline(
        path=path,
        schema=document["schema"],
        name=str(document.get("name", "")),
        rom=rom,
        text_layout=parsed_layout,
        reference=dict(document.get("reference", {})),
    )


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_rom(rom: bytes) -> dict[str, Any]:
    if len(rom) < 0x40:
        raise BaselineError("ROM is shorter than the 0x40-byte N64 header")
    return {
        "size": len(rom),
        "sha256": sha256_bytes(rom),
        "byte_order_magic": rom[0:4].hex(),
        "game_code": rom[0x3B:0x3F].decode("ascii", errors="replace"),
        "revision": rom[0x3F],
        "header_crc1": rom[0x10:0x14].hex(),
        "header_crc2": rom[0x14:0x18].hex(),
    }


def verify_rom(rom: bytes, baseline: Baseline) -> dict[str, Any]:
    actual = inspect_rom(rom)
    expected = baseline.rom
    fields = (
        "size",
        "sha256",
        "byte_order_magic",
        "game_code",
        "revision",
        "header_crc1",
        "header_crc2",
    )
    mismatches: list[str] = []
    for field in fields:
        if field not in expected:
            mismatches.append(f"baseline is missing rom.{field}")
            continue
        expected_value = expected[field]
        if field in {"size", "revision"}:
            expected_value = _parse_int(expected_value, f"rom.{field}")
        elif isinstance(expected_value, str):
            expected_value = expected_value.lower() if field != "game_code" else expected_value
        if actual[field] != expected_value:
            mismatches.append(
                f"rom.{field}: expected {expected_value!r}, got {actual[field]!r}"
            )
    if mismatches:
        raise BaselineError("ROM identity gate failed:\n- " + "\n- ".join(mismatches))
    return actual


def read_u32_be(data: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise BaselineError(f"{label}: u32 at 0x{offset:x} exceeds ROM size")
    return struct.unpack_from(">I", data, offset)[0]
