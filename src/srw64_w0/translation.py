from __future__ import annotations

from collections import Counter
import csv
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
from typing import Any, Iterable

from .baseline import TextLayout
from .text_ir import TextEntry, parse_entries


OVERLAY_SCHEMA = "srw64.translation-overlay.v1"
CONTROL_CODES = {"END": 0xFFFF, "BR": 0xFFFE, "STOP": 0xFFFD}
CODE_TO_CONTROL = {value: key for key, value in CONTROL_CODES.items()}
TOKEN_PATTERN = re.compile(r"<(END|BR|STOP|G:([0-9A-Fa-f]{1,4}))>")


class TranslationError(ValueError):
    """Raised when a translator-facing row would lose structural information."""


@dataclass(frozen=True)
class Token:
    kind: str
    value: str | int


@dataclass(frozen=True)
class TranslationRow:
    key: str
    table_id: int
    text_id: int
    target: str
    note: str


def load_glyph_map(path: Path) -> dict[int, str]:
    mapping: dict[int, str] = {}
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for line_number, row in enumerate(csv.DictReader(handle), start=2):
            glyph_text = row.get("glyph_id", "").strip()
            character = row.get("char", "")
            if not glyph_text or not character:
                continue
            glyph_id = int(glyph_text, 0)
            if glyph_id in mapping:
                raise TranslationError(f"glyph map line {line_number}: duplicate id {glyph_id}")
            mapping[glyph_id] = character
    if not mapping:
        raise TranslationError("glyph map is empty")
    return mapping


def reverse_glyph_map(mapping: dict[int, str]) -> dict[str, int]:
    reverse: dict[str, int] = {}
    for glyph_id in sorted(mapping):
        character = mapping[glyph_id]
        if len(character) == 1:
            reverse.setdefault(character, glyph_id)
    return reverse


def decode_source(entry: TextEntry, mapping: dict[int, str]) -> str:
    parts: list[str] = []
    for unit in entry.units:
        if unit in CODE_TO_CONTROL:
            parts.append(f"<{CODE_TO_CONTROL[unit]}>")
        elif unit == 0:
            parts.append(" ")
        elif unit in mapping:
            parts.append(mapping[unit])
        else:
            parts.append(f"<G:{unit:04X}>")
    return "".join(parts)


def parse_mixed_text(text: str) -> list[Token]:
    tokens: list[Token] = []
    position = 0
    while position < len(text):
        if text[position] == "<":
            match = TOKEN_PATTERN.match(text, position)
            if match is None:
                snippet = text[position : position + 24]
                raise TranslationError(f"invalid token near {snippet!r}")
            name = match.group(1)
            if name.startswith("G:"):
                tokens.append(Token("glyph", int(match.group(2), 16)))
            else:
                tokens.append(Token("control", name))
            position = match.end()
            continue
        tokens.append(Token("char", text[position]))
        position += 1
    return tokens


def _source_required_signature(entry: TextEntry, mapping: dict[int, str]) -> tuple[tuple[str, int | str], ...]:
    signature: list[tuple[str, int | str]] = []
    for unit in entry.units:
        if unit in CODE_TO_CONTROL:
            signature.append(("control", CODE_TO_CONTROL[unit]))
        elif unit != 0 and unit not in mapping:
            signature.append(("glyph", unit))
    return tuple(signature)


def _target_required_signature(tokens: Iterable[Token]) -> tuple[tuple[str, int | str], ...]:
    return tuple((token.kind, token.value) for token in tokens if token.kind != "char")


def validate_target(entry: TextEntry, target: str, mapping: dict[int, str]) -> list[Token]:
    if not target:
        raise TranslationError(f"{entry.key}: target is empty")
    tokens = parse_mixed_text(target)
    end_positions = [index for index, token in enumerate(tokens) if token == Token("control", "END")]
    if end_positions != [len(tokens) - 1]:
        raise TranslationError(f"{entry.key}: target must contain exactly one final <END>")
    source_signature = _source_required_signature(entry, mapping)
    target_signature = _target_required_signature(tokens)
    if source_signature != target_signature:
        raise TranslationError(
            f"{entry.key}: required token sequence changed; "
            f"source={source_signature!r}, target={target_signature!r}"
        )
    return tokens


def _parse_key(key: str) -> tuple[int, int]:
    match = re.fullmatch(r"t(\d{2})_(\d{5})", key)
    if match is None:
        raise TranslationError(f"invalid translation key {key!r}")
    return int(match.group(1)), int(match.group(2))


def load_overlay(path: Path) -> list[TranslationRow]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise TranslationError(f"cannot read overlay {path}: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema") != OVERLAY_SCHEMA:
        raise TranslationError("unsupported or missing translation overlay schema")
    raw_entries = document.get("entries")
    if not isinstance(raw_entries, list):
        raise TranslationError("translation overlay entries must be a list")
    rows: list[TranslationRow] = []
    seen: set[str] = set()
    for index, raw in enumerate(raw_entries, start=1):
        if not isinstance(raw, dict):
            raise TranslationError(f"overlay entry {index}: expected an object")
        key = raw.get("key")
        target = raw.get("target")
        if not isinstance(key, str) or not isinstance(target, str):
            raise TranslationError(f"overlay entry {index}: key and target must be strings")
        if key in seen:
            raise TranslationError(f"overlay entry {index}: duplicate key {key}")
        seen.add(key)
        table_id, text_id = _parse_key(key)
        rows.append(
            TranslationRow(
                key=key,
                table_id=table_id,
                text_id=text_id,
                target=target,
                note=str(raw.get("note", "")),
            )
        )
    if not rows:
        raise TranslationError("translation overlay has no entries")
    return rows


def is_han(character: str) -> bool:
    codepoint = ord(character)
    return (
        0x3400 <= codepoint <= 0x4DBF
        or 0x4E00 <= codepoint <= 0x9FFF
        or 0xF900 <= codepoint <= 0xFAFF
        or 0x20000 <= codepoint <= 0x323AF
    )


def allocate_target_characters(
    validated_tokens: dict[str, list[Token]],
    reverse_mapping: dict[str, int],
    first_glyph: int,
    last_glyph: int,
) -> dict[str, int]:
    frequencies: Counter[str] = Counter()
    for tokens in validated_tokens.values():
        for token in tokens:
            if token.kind != "char" or token.value == " ":
                continue
            character = str(token.value)
            if is_han(character) or character not in reverse_mapping:
                frequencies[character] += 1
    ordered = sorted(frequencies, key=lambda character: (-frequencies[character], ord(character)))
    capacity = last_glyph - first_glyph + 1
    if len(ordered) > capacity:
        raise TranslationError(
            f"target needs {len(ordered)} new/redrawn glyphs but only {capacity} slots exist"
        )
    return {character: first_glyph + index for index, character in enumerate(ordered)}


def encode_target(
    tokens: Iterable[Token],
    reverse_mapping: dict[str, int],
    allocations: dict[str, int],
) -> tuple[int, ...]:
    units: list[int] = []
    for token in tokens:
        if token.kind == "control":
            units.append(CONTROL_CODES[str(token.value)])
        elif token.kind == "glyph":
            units.append(int(token.value))
        else:
            character = str(token.value)
            if character == " ":
                units.append(0)
            elif character in allocations:
                units.append(allocations[character])
            elif character in reverse_mapping:
                units.append(reverse_mapping[character])
            else:
                raise TranslationError(f"no glyph allocation for {character!r}")
    return tuple(units)


def validate_overlay(
    rom: bytes,
    layout: TextLayout,
    rows: list[TranslationRow],
    mapping: dict[int, str],
) -> tuple[dict[str, TextEntry], dict[str, list[Token]]]:
    entries = {entry.key: entry for entry in parse_entries(rom, layout)}
    selected: dict[str, TextEntry] = {}
    validated: dict[str, list[Token]] = {}
    previous: tuple[int, int] | None = None
    for row in rows:
        if row.key not in entries:
            raise TranslationError(f"{row.key}: key does not exist in the ROM")
        order = (row.table_id, row.text_id)
        if previous is not None and order <= previous:
            raise TranslationError("translation overlay keys must be strictly increasing")
        previous = order
        entry = entries[row.key]
        selected[row.key] = entry
        validated[row.key] = validate_target(entry, row.target, mapping)
    return selected, validated


def patch_translations(
    rom: bytes,
    layout: TextLayout,
    entries: dict[str, TextEntry],
    encoded_targets: dict[str, tuple[int, ...]],
    text_pool_start: int,
    text_pool_end: int,
) -> tuple[bytes, list[dict[str, Any]], list[tuple[int, int]]]:
    if text_pool_start % 2 or text_pool_end % 2 or text_pool_start >= text_pool_end:
        raise TranslationError("text pool must be a nonempty, two-byte-aligned range")
    if text_pool_start < 0 or text_pool_end > len(rom):
        raise TranslationError("text pool exceeds ROM")

    all_entries = list(parse_entries(rom, layout))
    range_counts: Counter[tuple[int, int]] = Counter(
        (entry.data_offset, entry.byte_size) for entry in all_entries
    )
    patched = bytearray(rom)
    cursor = text_pool_start
    reports: list[dict[str, Any]] = []
    allowed_ranges: list[tuple[int, int]] = []

    for key, entry in entries.items():
        units = encoded_targets[key]
        encoded_body = b"".join(struct.pack(">H", unit) for unit in units)
        original_capacity = len(entry.units)
        is_shared = range_counts[(entry.data_offset, entry.byte_size)] > 1
        if len(units) <= original_capacity and not is_shared:
            body_offset = entry.data_offset + len(entry.header)
            body_end = entry.data_offset + entry.byte_size
            padding = struct.pack(">H", 0xFFFF) * (original_capacity - len(units))
            patched[body_offset:body_end] = encoded_body + padding
            mode = "in_place"
            destination = entry.data_offset
            allowed_ranges.append((body_offset, body_end))
        else:
            payload = entry.header + encoded_body
            destination = cursor
            destination_end = destination + len(payload)
            if destination_end > text_pool_end:
                raise TranslationError(
                    f"{key}: text pool exhausted at 0x{destination_end:x} (limit 0x{text_pool_end:x})"
                )
            if any(byte not in (0x00, 0xFF) for byte in patched[destination:destination_end]):
                raise TranslationError(f"{key}: text pool overlaps non-padding bytes")
            relative_offset = destination - entry.table_base
            if relative_offset < 0:
                raise TranslationError(f"{key}: text pool is before its table base")
            struct.pack_into(">II", patched, entry.descriptor_offset, relative_offset, len(payload))
            patched[destination:destination_end] = payload
            allowed_ranges.append((entry.descriptor_offset, entry.descriptor_offset + 8))
            allowed_ranges.append((destination, destination_end))
            cursor = (destination_end + 1) & ~1
            mode = "pool_shared" if is_shared else "pool_expanded"

        reports.append(
            {
                "key": key,
                "mode": mode,
                "original_units": original_capacity,
                "target_units": len(units),
                "destination": destination,
            }
        )

    reparsed = {entry.key: entry for entry in parse_entries(bytes(patched), layout)}
    for key, target in encoded_targets.items():
        actual = reparsed[key].units
        if actual[: len(target)] != target:
            raise TranslationError(f"{key}: patched glyph stream does not match target")
        if any(unit != 0xFFFF for unit in actual[len(target) :]):
            raise TranslationError(f"{key}: in-place tail is not safe terminator padding")
    reports.append(
        {
            "text_pool_start": text_pool_start,
            "text_pool_end": text_pool_end,
            "text_pool_used": cursor - text_pool_start,
            "text_pool_remaining": text_pool_end - cursor,
        }
    )
    return bytes(patched), reports, allowed_ranges
