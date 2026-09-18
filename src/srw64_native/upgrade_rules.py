"""Unit upgrade rules file (docs/gameplay/upgrade-limits.md): template export and validation.

The host reads the same file through SRW64_UPGRADE_RULES and applies the same
checks (src/host/upgrade_rules.hpp); this module lets the
launcher reject a bad file before starting the game and gives MOD authors a
template holding the original values.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

SCHEMA = "srw64.upgrade-rules.v1"
LEVELS = 15
STATS = ("hp", "en", "mobility", "armor", "limit")
WEAPON_TYPES = ("1", "2", "3", "4")
MIN_CAP, MAX_CAP = 5, 15
MAX_INCREMENT, MAX_INCREMENT_SUM = 9999, 30000
# 0 and 99999 are what the upgrade screen shows as "-----".
MIN_PRICE, MAX_PRICE = 1, 99998
UNIT_COUNT, WEAPON_COUNT = 363, 1329

# ROM layout, identical to upgrade_rules.hpp.
RESIDENT_DELTA = 0x80075610
STAT_INCREMENTS, STAT_INCREMENTS_STRIDE = 0x800CA4F0, 0x20
WEAPON_INCREMENTS = 0x800CA590
OVERLAY_ROM, OVERLAY_RAM = 0x8F4B0, 0x801C4500
STAT_PRICES, STAT_PRICES_STRIDE = 0x801DC36C, 0x40
WEAPON_PRICES = (0x801DC7BC, 0x801DC77C, 0x801DC73C, 0x801DC6FC)
UNIT_RECORDS, UNIT_RECORD_SIZE, RECORD_CAP = 0x71B80, 0x24, 0x20
WEAPON_RECORDS, WEAPON_RECORD_SIZE, RECORD_TYPE = 0x74E90, 0x10, 0x0E


def _overlay(vram: int) -> int:
    return vram - OVERLAY_RAM + OVERLAY_ROM


def _word(rom: bytes, offset: int, size: int) -> int:
    return int.from_bytes(rom[offset:offset + size], "big")


def original(rom: bytes) -> dict:
    """Every curve of the original game, in the file format."""
    stats = {}
    for index, name in enumerate(STATS):
        base = STAT_INCREMENTS - RESIDENT_DELTA + index * STAT_INCREMENTS_STRIDE
        prices = _overlay(STAT_PRICES) + index * STAT_PRICES_STRIDE
        stats[name] = {"increments": [_word(rom, base + (n + 1) * 2, 2) for n in range(LEVELS)],
                       "prices": [_word(rom, prices + n * 4, 4) for n in range(LEVELS)]}
    weapons = {}
    for index, name in enumerate(WEAPON_TYPES):
        base = WEAPON_INCREMENTS - RESIDENT_DELTA + (index + 1) * LEVELS * 2
        weapons[name] = {"increments": [_word(rom, base + n * 2, 2) for n in range(LEVELS)],
                         "prices": [_word(rom, _overlay(WEAPON_PRICES[index]) + n * 4, 4) for n in range(LEVELS)]}
    return {"stats": stats, "weapon_types": weapons}


def unit_caps(rom: bytes) -> list[int]:
    return [rom[UNIT_RECORDS + unit * UNIT_RECORD_SIZE + RECORD_CAP] for unit in range(UNIT_COUNT)]


def weapon_types(rom: bytes) -> list[int]:
    return [rom[WEAPON_RECORDS + weapon * WEAPON_RECORD_SIZE + RECORD_TYPE] for weapon in range(WEAPON_COUNT)]


def template(rom: bytes, unit_names: list[str] | None = None, weapon_names: list[str] | None = None,
             units: bool = False, weapons: bool = False) -> dict:
    """A complete file equal to the original. With units/weapons it also lists every
    unit cap or weapon type, so an author edits values instead of looking up ids."""
    document = {"schema": SCHEMA, "description": "原版数值；修改需要的项，其余可以删掉。", **original(rom)}
    if units:
        document["unit_caps"] = [{"id": unit, **({"name": unit_names[unit]} if unit_names else {}), "cap": cap}
                                 for unit, cap in enumerate(unit_caps(rom))]
    if weapons:
        document["weapon_type_overrides"] = [
            {"id": weapon, **({"name": weapon_names[weapon]} if weapon_names else {}), "type": kind}
            for weapon, kind in enumerate(weapon_types(rom))]
    return document


def _only(value, allowed: set[str], where: str) -> None:
    if not isinstance(value, dict):
        raise ValueError(f"{where} 必须是对象")
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise ValueError(f"{where} 有未知字段 {', '.join(unknown)}")


def _integer(value, low: int, high: int, where: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{where} 必须是整数")
    if not low <= value <= high:
        raise ValueError(f"{where} 超出范围 {low}–{high}")
    return value


def _curve(value, where: str) -> None:
    _only(value, {"increments", "prices"}, where)
    if "increments" in value:
        values = value["increments"]
        if not isinstance(values, list) or len(values) != LEVELS:
            raise ValueError(f"{where}.increments 必须是 15 个整数")
        if sum(_integer(item, 0, MAX_INCREMENT, f"{where}.increments") for item in values) > MAX_INCREMENT_SUM:
            raise ValueError(f"{where}.increments 之和超过 {MAX_INCREMENT_SUM}")
    if "prices" in value:
        values = value["prices"]
        if not isinstance(values, list) or len(values) != LEVELS:
            raise ValueError(f"{where}.prices 必须是 15 个整数")
        for item in values:
            _integer(item, MIN_PRICE, MAX_PRICE, f"{where}.prices")


def _overrides(value, field: str, count: int, low: int, high: int, where: str) -> None:
    if not isinstance(value, list):
        raise ValueError(f"{where} 必须是数组")
    seen = set()
    for row in value:
        _only(row, {"id", field, "name"}, f"{where} 的条目")
        if "id" not in row or field not in row:
            raise ValueError(f"{where} 的条目缺少 id 或 {field}")
        identifier = _integer(row["id"], 0, count - 1, f"{where}.id")
        if "name" in row and not isinstance(row["name"], str):
            raise ValueError(f"{where}.name 必须是字符串")
        _integer(row[field], low, high, f"{where}.{field}")
        if identifier in seen:
            raise ValueError(f"{where} 重复的 id {identifier}")
        seen.add(identifier)


def validate(document) -> None:
    """Raise ValueError with the same message the host would stop with."""
    _only(document, {"schema", "description", "stats", "weapon_types", "unit_caps", "weapon_type_overrides"}, "顶层")
    if document.get("schema") != SCHEMA:
        raise ValueError(f"schema 必须是 {SCHEMA}")
    if "description" in document and not isinstance(document["description"], str):
        raise ValueError("description 必须是字符串")
    if "stats" in document:
        _only(document["stats"], set(STATS), "stats")
        for name, curve in document["stats"].items():
            _curve(curve, f"stats.{name}")
    if "weapon_types" in document:
        _only(document["weapon_types"], set(WEAPON_TYPES), "weapon_types")
        for name, curve in document["weapon_types"].items():
            _curve(curve, f"weapon_types.{name}")
    if "unit_caps" in document:
        _overrides(document["unit_caps"], "cap", UNIT_COUNT, MIN_CAP, MAX_CAP, "unit_caps")
    if "weapon_type_overrides" in document:
        _overrides(document["weapon_type_overrides"], "type", WEAPON_COUNT, 0, len(WEAPON_TYPES), "weapon_type_overrides")


def load(path: Path) -> dict:
    try:
        document = json.loads(path.read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"无法读取 {path}：{error}") from error
    validate(document)
    return document


def report(path: Path) -> dict:
    """What a run records about the file it was given."""
    document = load(path)
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "stats": sorted(document.get("stats", {})), "weapon_types": sorted(document.get("weapon_types", {})),
            "unit_caps": len(document.get("unit_caps", [])),
            "weapon_type_overrides": len(document.get("weapon_type_overrides", []))}
