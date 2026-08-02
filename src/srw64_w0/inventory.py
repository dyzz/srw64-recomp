from __future__ import annotations

from collections import Counter
import csv
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import struct
import tempfile
from typing import Any, Iterable

from .baseline import load_baseline, sha256_file, verify_rom
from .text_ir import TextEntry, parse_entries, write_json
from .translation import (
    CODE_TO_CONTROL,
    TranslationRow,
    decode_source,
    load_glyph_map,
    load_overlay,
    validate_overlay,
)


SUMMARY_SCHEMA = "srw64.text-inventory-summary.v1"
PRESERVE_ACTION = "preserve_token"
CONTROL_UNITS = frozenset(CODE_TO_CONTROL)


class InventoryError(ValueError):
    """Raised when a text inventory input or output is inconsistent."""


@dataclass(frozen=True)
class EntryAnalysis:
    entry: TextEntry
    source: str
    sequence_sha256: str
    visible_glyph_cells: int
    mapped_glyph_cells: int
    decoded_characters: int
    decoded_nonspace_characters: int
    spaces: int
    line_breaks: int
    stops: int
    preserved_token_cells: int
    preserved_token_ids: tuple[int, ...]
    unresolved_glyph_cells: int
    unresolved_glyph_ids: tuple[int, ...]
    text_class: str
    flow_class: str
    translation_status: str


@dataclass
class SequenceGroup:
    analysis: EntryAnalysis
    keys: list[str]


def load_preserved_glyph_policy(path: Path) -> dict[int, str]:
    policy: dict[int, str] = {}
    try:
        handle = path.open("r", encoding="utf-8-sig", newline="")
    except OSError as exc:
        raise InventoryError(f"cannot read glyph policy {path}: {exc}") from exc
    with handle:
        for line_number, row in enumerate(csv.DictReader(handle), start=2):
            glyph_text = row.get("glyph_id", "").strip()
            action = row.get("action", "").strip()
            if not glyph_text or action != PRESERVE_ACTION:
                continue
            try:
                glyph_id = int(glyph_text, 0)
            except ValueError as exc:
                raise InventoryError(
                    f"glyph policy line {line_number}: invalid id {glyph_text!r}"
                ) from exc
            if glyph_id in policy:
                raise InventoryError(
                    f"glyph policy line {line_number}: duplicate id {glyph_id}"
                )
            policy[glyph_id] = row.get("kind", "").strip() or PRESERVE_ACTION
    return policy


def sequence_sha256(units: Iterable[int]) -> str:
    digest = hashlib.sha256()
    for unit in units:
        digest.update(struct.pack(">H", unit))
    return digest.hexdigest()


def analyze_entry(
    entry: TextEntry,
    mapping: dict[int, str],
    preserved_policy: dict[int, str],
) -> EntryAnalysis:
    visible_glyph_cells = 0
    mapped_glyph_cells = 0
    decoded_characters = 0
    decoded_nonspace_characters = 0
    spaces = 0
    preserved: Counter[int] = Counter()
    unresolved: Counter[int] = Counter()

    for unit in entry.units:
        if unit in CONTROL_UNITS:
            continue
        visible_glyph_cells += 1
        if unit == 0:
            spaces += 1
            decoded_characters += 1
        elif unit in mapping:
            text = mapping[unit]
            mapped_glyph_cells += 1
            decoded_characters += len(text)
            decoded_nonspace_characters += sum(
                not character.isspace() for character in text
            )
        elif unit in preserved_policy:
            preserved[unit] += 1
        else:
            unresolved[unit] += 1

    line_breaks = entry.units.count(0xFFFE)
    stops = entry.units.count(0xFFFD)
    if unresolved:
        text_class = "unresolved"
        translation_status = "blocked_unresolved"
    elif decoded_nonspace_characters:
        text_class = "text"
        translation_status = "candidate"
    elif preserved:
        text_class = "token_only"
        translation_status = "nontext"
    elif spaces:
        text_class = "whitespace_only"
        translation_status = "nontext"
    else:
        text_class = "structural_only"
        translation_status = "nontext"

    if stops > 1:
        flow_class = "multi_stop"
    elif stops == 1:
        flow_class = "stop"
    elif line_breaks:
        flow_class = "multiline"
    else:
        flow_class = "single_segment"

    return EntryAnalysis(
        entry=entry,
        source=decode_source(entry, mapping),
        sequence_sha256=sequence_sha256(entry.units),
        visible_glyph_cells=visible_glyph_cells,
        mapped_glyph_cells=mapped_glyph_cells,
        decoded_characters=decoded_characters,
        decoded_nonspace_characters=decoded_nonspace_characters,
        spaces=spaces,
        line_breaks=line_breaks,
        stops=stops,
        preserved_token_cells=sum(preserved.values()),
        preserved_token_ids=tuple(sorted(preserved)),
        unresolved_glyph_cells=sum(unresolved.values()),
        unresolved_glyph_ids=tuple(sorted(unresolved)),
        text_class=text_class,
        flow_class=flow_class,
        translation_status=translation_status,
    )


def group_sequences(
    analyses: Iterable[EntryAnalysis],
) -> dict[tuple[int, ...], SequenceGroup]:
    groups: dict[tuple[int, ...], SequenceGroup] = {}
    for analysis in analyses:
        units = analysis.entry.units
        group = groups.get(units)
        if group is None:
            groups[units] = SequenceGroup(analysis=analysis, keys=[analysis.entry.key])
        else:
            if group.analysis.sequence_sha256 != analysis.sequence_sha256:
                raise InventoryError(
                    "sequence hash changed inside an exact duplicate group"
                )
            group.keys.append(analysis.entry.key)
    return groups


def _hex_ids(ids: Iterable[int]) -> str:
    return " ".join(f"{glyph_id:04X}" for glyph_id in ids)


def _length_bucket(length: int) -> str:
    if length <= 8:
        return "000-008"
    if length <= 16:
        return "009-016"
    if length <= 32:
        return "017-032"
    if length <= 64:
        return "033-064"
    if length <= 128:
        return "065-128"
    return "129+"


def _duplicate_group_bucket(size: int) -> str:
    if size == 1:
        return "1"
    if size == 2:
        return "2"
    if size <= 5:
        return "3-5"
    if size <= 10:
        return "6-10"
    return "11+"


def _catalog_status(
    group: SequenceGroup,
    overlay: dict[str, TranslationRow],
) -> tuple[str, list[str]]:
    translated_keys = [key for key in group.keys if key in overlay]
    if group.analysis.translation_status != "candidate":
        status = group.analysis.translation_status
    elif not translated_keys:
        status = "pending"
    elif len(translated_keys) == len(group.keys):
        status = "translated"
    else:
        status = "partially_translated"
    return status, translated_keys


def summarize_inventory(
    analyses: list[EntryAnalysis],
    groups: dict[tuple[int, ...], SequenceGroup],
    overlay: dict[str, TranslationRow],
) -> dict[str, Any]:
    totals: Counter[str] = Counter()
    controls: Counter[str] = Counter()
    text_classes: Counter[str] = Counter()
    flow_classes: Counter[str] = Counter()
    length_buckets: Counter[str] = Counter()
    per_table: dict[int, Counter[str]] = {}
    physical_refs: Counter[tuple[int, int]] = Counter()
    header_patterns: Counter[str] = Counter()
    catalog_statuses: Counter[str] = Counter()
    duplicate_group_sizes: Counter[str] = Counter()
    used_nonspace_glyph_ids: set[int] = set()
    preserved_glyph_ids: set[int] = set()
    unresolved_glyph_ids: set[int] = set()

    for analysis in analyses:
        entry = analysis.entry
        totals["entries"] += 1
        totals["entry_payload_bytes"] += entry.byte_size
        totals["glyph_units"] += len(entry.units)
        totals["visible_glyph_cells"] += analysis.visible_glyph_cells
        totals["mapped_glyph_cells"] += analysis.mapped_glyph_cells
        totals["decoded_characters"] += analysis.decoded_characters
        totals["decoded_nonspace_characters"] += analysis.decoded_nonspace_characters
        totals["spaces"] += analysis.spaces
        totals["preserved_token_cells"] += analysis.preserved_token_cells
        totals["unresolved_glyph_cells"] += analysis.unresolved_glyph_cells
        controls["END"] += entry.units.count(0xFFFF)
        controls["BR"] += analysis.line_breaks
        controls["STOP"] += analysis.stops
        text_classes[analysis.text_class] += 1
        flow_classes[analysis.flow_class] += 1
        length_buckets[_length_bucket(analysis.visible_glyph_cells)] += 1
        physical_refs[(entry.data_offset, entry.byte_size)] += 1
        header_patterns[entry.header.hex().upper()] += 1
        used_nonspace_glyph_ids.update(
            unit
            for unit in entry.units
            if unit not in CONTROL_UNITS and unit != 0
        )
        preserved_glyph_ids.update(analysis.preserved_token_ids)
        unresolved_glyph_ids.update(analysis.unresolved_glyph_ids)

        table = per_table.setdefault(entry.table_id, Counter())
        table["entries"] += 1
        table["entry_payload_bytes"] += entry.byte_size
        table["glyph_units"] += len(entry.units)
        table["visible_glyph_cells"] += analysis.visible_glyph_cells
        table["decoded_characters"] += analysis.decoded_characters
        table["decoded_nonspace_characters"] += analysis.decoded_nonspace_characters
        table["candidate_entries"] += analysis.translation_status == "candidate"
        table["translated_entries"] += entry.key in overlay

    unique_totals: Counter[str] = Counter()
    unique_text_sequences = 0
    for group in groups.values():
        analysis = group.analysis
        status, _ = _catalog_status(group, overlay)
        catalog_statuses[status] += 1
        duplicate_group_sizes[_duplicate_group_bucket(len(group.keys))] += 1
        unique_totals["visible_glyph_cells"] += analysis.visible_glyph_cells
        unique_totals["decoded_characters"] += analysis.decoded_characters
        unique_totals["decoded_nonspace_characters"] += (
            analysis.decoded_nonspace_characters
        )
        unique_totals["spaces"] += analysis.spaces
        unique_totals["preserved_token_cells"] += analysis.preserved_token_cells
        if analysis.translation_status == "candidate":
            unique_text_sequences += 1

    occurrence_denominator = (
        totals["mapped_glyph_cells"]
        + totals["preserved_token_cells"]
        + totals["unresolved_glyph_cells"]
    )
    ordinary_denominator = (
        totals["mapped_glyph_cells"] + totals["unresolved_glyph_cells"]
    )
    candidate_entries = text_classes["text"]
    visible_lengths = sorted(
        analysis.visible_glyph_cells for analysis in analyses
    )

    def percentile(fraction: float) -> int:
        index = int((len(visible_lengths) - 1) * fraction)
        return visible_lengths[index]

    return {
        "totals": {
            **dict(totals),
            "candidate_entries": candidate_entries,
            "non_candidate_entries": len(analyses) - candidate_entries,
            "unique_sequences": len(groups),
            "unique_text_sequences": unique_text_sequences,
            "duplicate_entry_instances": len(analyses) - len(groups),
            "unique_physical_entry_refs": len(physical_refs),
            "aliased_descriptor_count": len(analyses) - len(physical_refs),
        },
        "unique_sequence_volume": dict(unique_totals),
        "controls": dict(controls),
        "text_classes": dict(text_classes),
        "flow_classes": dict(flow_classes),
        "visible_length_buckets": dict(sorted(length_buckets.items())),
        "visible_length_distribution": {
            "minimum": visible_lengths[0],
            "median": percentile(0.5),
            "p90": percentile(0.9),
            "p95": percentile(0.95),
            "p99": percentile(0.99),
            "maximum": visible_lengths[-1],
        },
        "duplicate_groups": {
            "size_buckets": dict(duplicate_group_sizes),
            "maximum_group_size": max(len(group.keys) for group in groups.values()),
            "largest_groups": [
                {
                    "representative_key": group.analysis.entry.key,
                    "entry_count": len(group.keys),
                    "sequence_sha256": group.analysis.sequence_sha256,
                    "source": group.analysis.source,
                }
                for group in sorted(
                    groups.values(),
                    key=lambda item: (
                        -len(item.keys),
                        item.analysis.entry.table_id,
                        item.analysis.entry.text_id,
                    ),
                )[:20]
            ],
        },
        "catalog_statuses": dict(catalog_statuses),
        "headers": {
            "unique_patterns": len(header_patterns),
            "top_patterns": [
                {"header_hex": header, "entries": count}
                for header, count in header_patterns.most_common(20)
            ],
        },
        "tables": {
            str(table_id): dict(per_table[table_id])
            for table_id in sorted(per_table)
        },
        "glyph_coverage": {
            "used_unique_nonspace_glyph_ids": len(used_nonspace_glyph_ids),
            "mapped_unique_glyph_ids": len(
                used_nonspace_glyph_ids
                - preserved_glyph_ids
                - unresolved_glyph_ids
            ),
            "mapped_occurrence_percent_including_preserved_tokens": round(
                100 * totals["mapped_glyph_cells"] / occurrence_denominator, 6
            )
            if occurrence_denominator
            else 100.0,
            "ordinary_text_occurrence_percent": round(
                100 * totals["mapped_glyph_cells"] / ordinary_denominator, 6
            )
            if ordinary_denominator
            else 100.0,
            "preserved_token_ids": sorted(preserved_glyph_ids),
            "unresolved_glyph_ids": sorted(unresolved_glyph_ids),
        },
        "overlay": {
            "translated_entries": len(overlay),
            "candidate_entry_percent": round(
                100 * len(overlay) / candidate_entries, 6
            )
            if candidate_entries
            else 0.0,
        },
    }


def _atomic_csv(
    path: Path,
    fieldnames: list[str],
    rows: Iterable[dict[str, Any]],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary = Path(handle.name)
            writer = csv.DictWriter(
                handle,
                fieldnames=fieldnames,
                quoting=csv.QUOTE_ALL,
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(rows)
        os.replace(temporary, path)
        path.chmod(0o644)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _inventory_rows(
    analyses: list[EntryAnalysis],
    groups: dict[tuple[int, ...], SequenceGroup],
    overlay: dict[str, TranslationRow],
) -> Iterable[dict[str, Any]]:
    for analysis in analyses:
        entry = analysis.entry
        group = groups[entry.units]
        translated = overlay.get(entry.key)
        yield {
            "key": entry.key,
            "table_id": entry.table_id,
            "text_id": entry.text_id,
            "data_offset": f"0x{entry.data_offset:08X}",
            "byte_size": entry.byte_size,
            "header_hex": entry.header.hex().upper(),
            "glyph_units": len(entry.units),
            "visible_glyph_cells": analysis.visible_glyph_cells,
            "decoded_characters": analysis.decoded_characters,
            "decoded_nonspace_characters": analysis.decoded_nonspace_characters,
            "spaces": analysis.spaces,
            "line_breaks": analysis.line_breaks,
            "stops": analysis.stops,
            "preserved_token_cells": analysis.preserved_token_cells,
            "preserved_token_ids": _hex_ids(analysis.preserved_token_ids),
            "unresolved_glyph_cells": analysis.unresolved_glyph_cells,
            "unresolved_glyph_ids": _hex_ids(analysis.unresolved_glyph_ids),
            "text_class": analysis.text_class,
            "flow_class": analysis.flow_class,
            "translation_status": (
                "translated" if translated else analysis.translation_status
            ),
            "sequence_sha256": analysis.sequence_sha256,
            "representative_key": group.analysis.entry.key,
            "duplicate_count": len(group.keys),
            "source": analysis.source,
            "target": translated.target if translated else "",
            "note": translated.note if translated else "",
        }


def _catalog_rows(
    groups: dict[tuple[int, ...], SequenceGroup],
    overlay: dict[str, TranslationRow],
) -> Iterable[dict[str, Any]]:
    ordered = sorted(
        groups.values(),
        key=lambda group: (group.analysis.entry.table_id, group.analysis.entry.text_id),
    )
    for group in ordered:
        analysis = group.analysis
        catalog_status, translated_keys = _catalog_status(group, overlay)
        targets = sorted({overlay[key].target for key in translated_keys})
        yield {
            "sequence_sha256": analysis.sequence_sha256,
            "representative_key": analysis.entry.key,
            "entry_count": len(group.keys),
            "keys": " ".join(group.keys),
            "translated_entry_count": len(translated_keys),
            "translated_keys": " ".join(translated_keys),
            "catalog_status": catalog_status,
            "text_class": analysis.text_class,
            "flow_class": analysis.flow_class,
            "visible_glyph_cells": analysis.visible_glyph_cells,
            "decoded_characters": analysis.decoded_characters,
            "decoded_nonspace_characters": analysis.decoded_nonspace_characters,
            "line_breaks": analysis.line_breaks,
            "stops": analysis.stops,
            "preserved_token_ids": _hex_ids(analysis.preserved_token_ids),
            "unresolved_glyph_ids": _hex_ids(analysis.unresolved_glyph_ids),
            "source": analysis.source,
            "known_targets": " || ".join(targets),
        }


def build_text_inventory(
    rom_path: Path,
    baseline_path: Path,
    glyph_map_path: Path,
    policy_path: Path,
    work_dir: Path,
    overlay_path: Path | None = None,
) -> dict[str, Any]:
    baseline = load_baseline(baseline_path)
    rom = rom_path.read_bytes()
    identity = verify_rom(rom, baseline)
    mapping = load_glyph_map(glyph_map_path)
    preserved_policy = load_preserved_glyph_policy(policy_path)
    entries = list(parse_entries(rom, baseline.text_layout))
    if not entries:
        raise InventoryError("ROM contains no text entries")
    analyses = [
        analyze_entry(entry, mapping, preserved_policy) for entry in entries
    ]
    unresolved_ids = sorted(
        {
            glyph_id
            for analysis in analyses
            for glyph_id in analysis.unresolved_glyph_ids
        }
    )
    if unresolved_ids:
        rendered = ", ".join(f"0x{glyph_id:04X}" for glyph_id in unresolved_ids)
        raise InventoryError(f"glyph coverage is incomplete: {rendered}")
    groups = group_sequences(analyses)

    overlay_rows = load_overlay(overlay_path) if overlay_path else []
    overlay = {row.key: row for row in overlay_rows}
    if overlay_rows:
        validate_overlay(rom, baseline.text_layout, overlay_rows, mapping)
    entry_keys = {entry.key for entry in entries}
    unexpected_overlay = sorted(set(overlay) - entry_keys)
    if unexpected_overlay:
        raise InventoryError(
            f"overlay contains unknown key {unexpected_overlay[0]!r}"
        )

    work_dir.mkdir(parents=True, exist_ok=True)
    inventory_path = work_dir / "text-inventory.csv"
    catalog_path = work_dir / "translation-catalog.csv"
    summary_path = work_dir / "summary.json"
    _atomic_csv(
        inventory_path,
        [
            "key",
            "table_id",
            "text_id",
            "data_offset",
            "byte_size",
            "header_hex",
            "glyph_units",
            "visible_glyph_cells",
            "decoded_characters",
            "decoded_nonspace_characters",
            "spaces",
            "line_breaks",
            "stops",
            "preserved_token_cells",
            "preserved_token_ids",
            "unresolved_glyph_cells",
            "unresolved_glyph_ids",
            "text_class",
            "flow_class",
            "translation_status",
            "sequence_sha256",
            "representative_key",
            "duplicate_count",
            "source",
            "target",
            "note",
        ],
        _inventory_rows(analyses, groups, overlay),
    )
    _atomic_csv(
        catalog_path,
        [
            "sequence_sha256",
            "representative_key",
            "entry_count",
            "keys",
            "translated_entry_count",
            "translated_keys",
            "catalog_status",
            "text_class",
            "flow_class",
            "visible_glyph_cells",
            "decoded_characters",
            "decoded_nonspace_characters",
            "line_breaks",
            "stops",
            "preserved_token_ids",
            "unresolved_glyph_ids",
            "source",
            "known_targets",
        ],
        _catalog_rows(groups, overlay),
    )

    summary = {
        "schema": SUMMARY_SCHEMA,
        "status": "passed",
        "inputs": {
            "rom": {"path": str(rom_path.resolve()), **identity},
            "baseline": {
                "path": str(baseline_path.resolve()),
                "sha256": sha256_file(baseline_path),
            },
            "glyph_map": {
                "path": str(glyph_map_path.resolve()),
                "sha256": sha256_file(glyph_map_path),
                "rows": len(mapping),
            },
            "unknown_glyph_policy": {
                "path": str(policy_path.resolve()),
                "sha256": sha256_file(policy_path),
                "preserved_ids": len(preserved_policy),
            },
            "overlay": (
                {
                    "path": str(overlay_path.resolve()),
                    "sha256": sha256_file(overlay_path),
                    "rows": len(overlay),
                }
                if overlay_path
                else None
            ),
        },
        **summarize_inventory(analyses, groups, overlay),
        "artifacts": {
            "text_inventory": {
                "path": str(inventory_path.resolve()),
                "rows": len(analyses),
                "sha256": sha256_file(inventory_path),
            },
            "translation_catalog": {
                "path": str(catalog_path.resolve()),
                "rows": len(groups),
                "sha256": sha256_file(catalog_path),
            },
        },
    }
    write_json(summary_path, summary)
    summary_path.chmod(0o644)
    return {**summary, "summary_path": str(summary_path.resolve())}
