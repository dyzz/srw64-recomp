from __future__ import annotations

from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
from typing import Any

from PIL import Image, ImageDraw, ImageFont

from .baseline import BaselineError, load_baseline, sha256_bytes, sha256_file, verify_rom
from .resources import (
    RESOURCE_BASE,
    ResourceError,
    ResourceTable,
    decode_i4_texture,
    encode_i4_texture,
    patch_resource_to_pool,
)
from .text_ir import TextIRError, parse_entries, write_json
from .translation import (
    TranslationError,
    allocate_target_characters,
    decode_source,
    encode_target,
    load_glyph_map,
    load_overlay,
    patch_translations,
    reverse_glyph_map,
    validate_overlay,
)


W1_CONFIG_SCHEMA = "srw64.w1-config.v1"
W1_REPORT_SCHEMA = "srw64.w1-build-report.v1"


def _as_int(value: int | str, name: str) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise TranslationError(f"{name}: invalid integer {value!r}") from exc
    raise TranslationError(f"{name}: expected an integer")


def _load_config(path: Path) -> dict[str, Any]:
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise TranslationError(f"cannot read W1 config {path}: {exc}") from exc
    if not isinstance(config, dict) or config.get("schema") != W1_CONFIG_SCHEMA:
        raise TranslationError("unsupported or missing W1 config schema")
    return config


def _resolve_from_config(config_path: Path, value: str) -> Path:
    return (config_path.parent / value).resolve()


def _verify_file_hash(path: Path, expected: str, label: str) -> str:
    actual = sha256_file(path)
    if actual != expected.lower():
        raise TranslationError(f"{label} hash mismatch: expected {expected}, got {actual}")
    return actual


def _dominant_ink(image: Image.Image) -> int:
    counts = Counter(value for value in image.tobytes() if value != 0)
    return counts.most_common(1)[0][0] if counts else 1


def _glyph_box(glyph_id: int) -> tuple[int, int, int, int, int]:
    if glyph_id < 0x013B:
        row = glyph_id // 0x3F
        return 0, (glyph_id - row * 0x3F) * 8, row * 14, 8, 14
    if glyph_id < 0x0597:
        index = glyph_id - 0x013B
        return 0, (index % 0x24) * 14, (index // 0x24) * 14 + 0x46, 14, 14
    index = glyph_id - 0x0597
    return 1, (index % 0x24) * 14, (index // 0x24) * 14, 14, 14


def _draw_expanded_font(
    rom: bytes,
    resource_id: int,
    expected_width: int,
    expected_height: int,
    expanded_height: int,
    allocations: dict[str, int],
    font_path: Path,
    font_size: int,
    y_offset: int,
) -> tuple[bytes, Image.Image, dict[str, int]]:
    table = ResourceTable(rom)
    decoded, original_consumed = table.extract(resource_id)
    image, flags = decode_i4_texture(decoded)
    if image.size != (expected_width, expected_height):
        raise ResourceError(
            f"font resource {resource_id} is {image.size}, expected {(expected_width, expected_height)}"
        )
    expanded = Image.new("P", (expected_width, expanded_height), 0)
    expanded.putpalette(image.getpalette())
    expanded.paste(image, (0, 0))
    ink = _dominant_ink(image)
    if not 1 <= ink <= 15:
        raise ResourceError(f"font ink palette index {ink} is outside 1..15")
    draw = ImageDraw.Draw(expanded)
    font = ImageFont.truetype(str(font_path), font_size)
    for character, glyph_id in sorted(allocations.items(), key=lambda item: item[1]):
        glyph_resource, x, y, width, height = _glyph_box(glyph_id)
        if glyph_resource != resource_id:
            raise ResourceError(f"glyph {glyph_id:04X} is not in font resource {resource_id}")
        if x + width > expanded.width or y + height > expanded.height:
            raise ResourceError(f"glyph {glyph_id:04X} exceeds expanded font atlas")
        draw.rectangle((x, y, x + width - 1, y + height - 1), fill=0)
        bounding = draw.textbbox((0, 0), character, font=font)
        text_width = bounding[2] - bounding[0]
        text_height = bounding[3] - bounding[1]
        text_x = x + (width - text_width) // 2 - bounding[0]
        text_y = y + (height - text_height) // 2 - bounding[1] - 1 + y_offset
        draw.text((text_x, text_y), character, font=font, fill=ink)
    return encode_i4_texture(expanded, flags), expanded, {
        "original_decoded_size": len(decoded),
        "original_encoded_consumed": original_consumed,
        "ink_index": ink,
    }


def _atomic_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as handle:
        temporary = Path(handle.name)
        handle.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[list[int]] = []
    for start, end in sorted(ranges):
        if start >= end:
            continue
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return [(start, end) for start, end in merged]


def _diff_ranges(before: bytes, after: bytes) -> tuple[int, list[tuple[int, int]]]:
    if len(before) != len(after):
        raise TranslationError("patched ROM size changed")
    count = 0
    ranges: list[tuple[int, int]] = []
    start: int | None = None
    for offset, (old, new) in enumerate(zip(before, after)):
        if old != new:
            count += 1
            if start is None:
                start = offset
        elif start is not None:
            ranges.append((start, offset))
            start = None
    if start is not None:
        ranges.append((start, len(before)))
    return count, ranges


def _assert_diff_whitelist(
    before: bytes, after: bytes, allowed_ranges: list[tuple[int, int]]
) -> tuple[int, list[tuple[int, int]]]:
    allowed = _merge_ranges(allowed_ranges)
    changed_count, changed_ranges = _diff_ranges(before, after)
    allowed_index = 0
    for start, end in changed_ranges:
        while allowed_index < len(allowed) and allowed[allowed_index][1] <= start:
            allowed_index += 1
        cursor = start
        index = allowed_index
        while cursor < end and index < len(allowed):
            allow_start, allow_end = allowed[index]
            if allow_start > cursor:
                break
            cursor = max(cursor, allow_end)
            index += 1
        if cursor < end:
            raise TranslationError(
                f"ROM changed outside whitelist at 0x{cursor:08x} within 0x{start:08x}-0x{end:08x}"
            )
    return changed_count, changed_ranges


def _visible_palette() -> list[int]:
    palette = [0, 0, 0]
    for _ in range(255):
        palette.extend((255, 255, 255))
    return palette


def _render_units(rom: bytes, units: tuple[int, ...], scale: int = 3) -> Image.Image:
    table = ResourceTable(rom)
    font0, _ = decode_i4_texture(table.extract(0)[0])
    font1, _ = decode_i4_texture(table.extract(1)[0])
    lines: list[list[int]] = [[]]
    for unit in units:
        if unit == 0xFFFF:
            break
        if unit in (0xFFFE, 0xFFFD):
            lines.append([])
        elif unit < 0x8000:
            lines[-1].append(unit)
    width = max(
        1,
        max((sum(8 if glyph < 0x013B else 14 for glyph in line) for line in lines), default=1),
    )
    output = Image.new("P", (width, max(1, len(lines)) * 14), 0)
    output.putpalette(_visible_palette())
    for line_number, line in enumerate(lines):
        x = 0
        for glyph in line:
            resource_id, source_x, source_y, glyph_width, glyph_height = _glyph_box(glyph)
            if glyph != 0:
                source = font0 if resource_id == 0 else font1
                output.paste(
                    source.crop(
                        (source_x, source_y, source_x + glyph_width, source_y + glyph_height)
                    ),
                    (x, line_number * 14),
                )
            x += glyph_width
    if scale != 1:
        output = output.resize(
            (output.width * scale, output.height * scale), Image.Resampling.NEAREST
        )
    return output


def _write_catalog(
    path: Path,
    rows,
    entries,
    mapping,
    encoded_targets,
    patch_reports,
) -> None:
    modes = {report["key"]: report for report in patch_reports if "key" in report}
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "key",
                "source",
                "target",
                "source_units",
                "target_units",
                "original_capacity",
                "target_count",
                "patch_mode",
                "note",
            ],
            quoting=csv.QUOTE_ALL,
        )
        writer.writeheader()
        for row in rows:
            entry = entries[row.key]
            target_units = encoded_targets[row.key]
            writer.writerow(
                {
                    "key": row.key,
                    "source": decode_source(entry, mapping),
                    "target": row.target,
                    "source_units": " ".join(f"{unit:04X}" for unit in entry.units),
                    "target_units": " ".join(f"{unit:04X}" for unit in target_units),
                    "original_capacity": len(entry.units),
                    "target_count": len(target_units),
                    "patch_mode": modes[row.key]["mode"],
                    "note": row.note,
                }
            )


def build_w1(
    rom_path: Path,
    config_path: Path,
    font_path: Path,
    work_dir: Path,
) -> dict[str, Any]:
    config_path = config_path.resolve()
    config = _load_config(config_path)
    baseline_path = _resolve_from_config(config_path, str(config["baseline"]))
    glyph_map_config = config["glyph_map"]
    glyph_map_path = _resolve_from_config(config_path, str(glyph_map_config["path"]))
    overlay_path = _resolve_from_config(config_path, str(config["overlay"]))
    baseline = load_baseline(baseline_path)
    rom = rom_path.read_bytes()
    identity = verify_rom(rom, baseline)
    glyph_map_hash = _verify_file_hash(
        glyph_map_path, str(glyph_map_config["sha256"]), "glyph map"
    )
    font_hash = _verify_file_hash(font_path, str(config["font"]["sha256"]), "font")

    mapping = load_glyph_map(glyph_map_path)
    reverse = reverse_glyph_map(mapping)
    rows = load_overlay(overlay_path)
    minimum = _as_int(config["slice_entry_minimum"], "slice_entry_minimum")
    maximum = _as_int(config["slice_entry_maximum"], "slice_entry_maximum")
    if not minimum <= len(rows) <= maximum:
        raise TranslationError(
            f"W1 overlay has {len(rows)} rows; required range is {minimum}..{maximum}"
        )
    entries, validated = validate_overlay(rom, baseline.text_layout, rows, mapping)

    font_resource = config["font_resource"]
    new_start = _as_int(font_resource["new_glyph_start"], "new_glyph_start")
    new_end = _as_int(font_resource["new_glyph_end"], "new_glyph_end")
    allocations = allocate_target_characters(validated, reverse, new_start, new_end)
    encoded_targets = {
        key: encode_target(tokens, reverse, allocations) for key, tokens in validated.items()
    }

    decoded_font, atlas, original_font = _draw_expanded_font(
        rom=rom,
        resource_id=_as_int(font_resource["resource_id"], "resource_id"),
        expected_width=_as_int(font_resource["expected_width"], "expected_width"),
        expected_height=_as_int(font_resource["expected_height"], "expected_height"),
        expanded_height=_as_int(font_resource["expanded_height"], "expanded_height"),
        allocations=allocations,
        font_path=font_path,
        font_size=_as_int(config["font"]["size"], "font.size"),
        y_offset=_as_int(config["font"]["y_offset"], "font.y_offset"),
    )
    resource_pool = config["resource_pool"]
    resource_start = _as_int(resource_pool["start"], "resource_pool.start")
    resource_end = _as_int(resource_pool["end"], "resource_pool.end")
    patched, resource_report = patch_resource_to_pool(
        rom,
        _as_int(font_resource["resource_id"], "resource_id"),
        decoded_font,
        resource_start,
        resource_end - resource_start,
    )
    text_pool = config["text_pool"]
    patched, text_reports, text_allowed = patch_translations(
        patched,
        baseline.text_layout,
        entries,
        encoded_targets,
        _as_int(text_pool["start"], "text_pool.start"),
        _as_int(text_pool["end"], "text_pool.end"),
    )

    resource_descriptor = RESOURCE_BASE + 4 + resource_report["resource_id"] * 8
    allowed = [
        (resource_descriptor, resource_descriptor + 8),
        (resource_start, resource_end),
        *text_allowed,
    ]
    changed_bytes, changed_ranges = _assert_diff_whitelist(rom, patched, allowed)
    if patched[: 1024 * 1024] != rom[: 1024 * 1024]:
        raise TranslationError("W1 changed the N64 checksum-covered first MiB")

    work_dir.mkdir(parents=True, exist_ok=True)
    output_path = work_dir / "srw64-w1.zh-test.z64"
    if output_path.resolve() == rom_path.resolve():
        raise TranslationError("W1 output must not overwrite its input ROM")
    _atomic_bytes(output_path, patched)
    atlas_path = work_dir / "font-resource1-expanded.png"
    atlas.save(atlas_path)
    catalog_path = work_dir / "catalog.csv"
    _write_catalog(catalog_path, rows, entries, mapping, encoded_targets, text_reports)

    diff_path = work_dir / "diff-ranges.csv"
    with diff_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["start", "end", "size"])
        writer.writeheader()
        for start, end in changed_ranges:
            writer.writerow(
                {"start": f"0x{start:08x}", "end": f"0x{end:08x}", "size": end - start}
            )

    allocation_path = work_dir / "glyph-allocations.csv"
    with allocation_path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["glyph_id", "character"], quoting=csv.QUOTE_ALL)
        writer.writeheader()
        for character, glyph_id in sorted(allocations.items(), key=lambda item: item[1]):
            writer.writerow({"glyph_id": f"0x{glyph_id:04x}", "character": character})

    patched_entries = {entry.key: entry for entry in parse_entries(patched, baseline.text_layout)}
    preview_dir = work_dir / "previews"
    preview_dir.mkdir(parents=True, exist_ok=True)
    for row in rows:
        _render_units(patched, patched_entries[row.key].units).save(preview_dir / f"{row.key}.png")

    pool_summary = text_reports[-1]
    report = {
        "schema": W1_REPORT_SCHEMA,
        "status": "static-passed",
        "rom": identity,
        "inputs": {
            "config_sha256": sha256_file(config_path),
            "overlay_sha256": sha256_file(overlay_path),
            "glyph_map_sha256": glyph_map_hash,
            "font_name": font_path.name,
            "font_sha256": font_hash,
        },
        "slice": {
            "entry_count": len(rows),
            "new_or_redrawn_glyph_count": len(allocations),
            "glyph_start": min(allocations.values()) if allocations else None,
            "glyph_end": max(allocations.values()) if allocations else None,
            "in_place_entries": sum(
                1 for item in text_reports if item.get("mode") == "in_place"
            ),
            "pooled_entries": sum(
                1 for item in text_reports if str(item.get("mode", "")).startswith("pool_")
            ),
        },
        "font_resource": {**original_font, **resource_report},
        "text_pool": pool_summary,
        "diff": {
            "changed_byte_count": changed_bytes,
            "changed_range_count": len(changed_ranges),
            "first_changed_offset": changed_ranges[0][0] if changed_ranges else None,
            "last_changed_offset_exclusive": changed_ranges[-1][1] if changed_ranges else None,
            "ranges_sha256": sha256_file(diff_path),
            "first_mib_unchanged": True,
        },
        "output": {
            "name": output_path.name,
            "size": len(patched),
            "sha256": sha256_bytes(patched),
        },
        "artifacts": {
            "catalog": catalog_path.name,
            "allocations": allocation_path.name,
            "font_atlas": atlas_path.name,
            "diff_ranges": diff_path.name,
            "preview_directory": preview_dir.name,
        },
    }
    write_json(work_dir / "build-report.json", report)
    return report
