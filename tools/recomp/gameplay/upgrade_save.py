#!/usr/bin/env python3
"""Controlled save edit for upgrade-limit checks (docs/gameplay/upgrade-limits.md): set
funds, unit levels and weapon levels in an intermission save and fix the block
checksum.

The intermission block (the 801C2600 buffer written by 800924D8) sits at SRAM
offset 0x10; its first word is the checksum in srw64_native.original_saves.
Funds are the u32 at +0x54 (800918DC). Unit slot n is 16 bytes at +0x110 + 16n
(80092240): u16 (unit << 6 | weapon count), then the five levels as nibbles
(+2 HP<<4|EN, +3 mobility<<4|armor, +4 flags|limit) and at +0xE the index of the
unit's first weapon. Weapon n is 6 bytes at +0xE80 + 6n (80092498): u16 id,
u16 eligible form, level<<4|flags. Levels are four bits in the save, so 15 is
the largest value it can hold. This stands in for money and upgrades earned in
play and must be reported as a controlled edit.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "src"))
from srw64_native import original_saves  # noqa: E402

SRAM_BYTES = 0x8000
BLOCK, BLOCK_SIZE = original_saves.SLOTS["intermission-1"]
FUNDS = 0x54
UNITS, UNIT_SIZE, UNIT_SLOTS = 0x110, 0x10, 140
WEAPONS, WEAPON_SIZE, WEAPON_SLOTS = 0xE80, 6, 700


def checksum(sram: bytes | bytearray) -> int:
    return original_saves.checksum(bytes(sram[BLOCK:BLOCK + BLOCK_SIZE]), tactical=False)


def units(sram: bytes | bytearray) -> list[dict]:
    """Occupied unit slots: unit id, the five levels and their weapons."""
    rows = []
    for slot in range(UNIT_SLOTS):
        at = BLOCK + UNITS + slot * UNIT_SIZE
        head = int.from_bytes(sram[at:at + 2], "big")
        if not head:
            continue
        first = int.from_bytes(sram[at + 14:at + 16], "big")
        weapons = []
        for index in range(first, first + (head & 0x3F)):
            entry = BLOCK + WEAPONS + index * WEAPON_SIZE
            weapons.append({"index": index, "weapon": int.from_bytes(sram[entry:entry + 2], "big"),
                            "level": sram[entry + 4] >> 4})
        rows.append({"slot": slot, "unit": head >> 6,
                     "levels": [sram[at + 2] >> 4, sram[at + 2] & 15, sram[at + 3] >> 4, sram[at + 3] & 15, sram[at + 4] & 15],
                     "weapons": weapons})
    return rows


def edit(sram: bytes, funds: int | None = None, unit_levels: dict[int, list[int]] | None = None,
         weapon_levels: dict[int, int] | None = None) -> bytes:
    if len(sram) != SRAM_BYTES:
        raise ValueError(f"expected a {SRAM_BYTES}-byte SRAM, got {len(sram)}")
    if int.from_bytes(sram[BLOCK:BLOCK + 2], "big") != checksum(sram):
        raise ValueError("the intermission block checksum does not match; not an intermission save")
    edited = bytearray(sram)
    if funds is not None:
        if not 0 <= funds <= 0xFFFFFFFF:
            raise ValueError("funds are a u32")
        edited[BLOCK + FUNDS:BLOCK + FUNDS + 4] = funds.to_bytes(4, "big")
    occupied = {row["slot"] for row in units(sram)}
    for slot, levels in (unit_levels or {}).items():
        if slot not in occupied:
            raise ValueError(f"unit slot {slot} is empty")
        if len(levels) != 5 or any(not 0 <= level <= 15 for level in levels):
            raise ValueError("five levels, each 0..15")
        at = BLOCK + UNITS + slot * UNIT_SIZE
        hp, en, mobility, armor, limit = levels
        edited[at + 2] = hp << 4 | en
        edited[at + 3] = mobility << 4 | armor
        edited[at + 4] = (edited[at + 4] & 0xF0) | limit
    for index, level in (weapon_levels or {}).items():
        if not 0 <= index < WEAPON_SLOTS or not 0 <= level <= 15:
            raise ValueError("weapon index 0..699, level 0..15")
        at = BLOCK + WEAPONS + index * WEAPON_SIZE + 4
        edited[at] = level << 4 | (edited[at] & 0x0F)
    edited[BLOCK:BLOCK + 2] = checksum(edited).to_bytes(2, "big")
    return bytes(edited)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--funds", type=int)
    parser.add_argument("--unit", action="append", default=[], metavar="SLOT:HP,EN,MOB,ARM,LIM")
    parser.add_argument("--weapon", action="append", default=[], metavar="INDEX:LEVEL")
    parser.add_argument("--list", action="store_true", help="print the occupied unit slots and stop")
    args = parser.parse_args()
    source = args.source.read_bytes()
    if args.list or not args.output:
        print(f"funds {int.from_bytes(source[BLOCK + FUNDS:BLOCK + FUNDS + 4], 'big')}")
        for row in units(source):
            print(row)
        return 0
    unit_levels = {int(slot): [int(value) for value in levels.split(",")]
                   for slot, levels in (item.split(":") for item in args.unit)}
    weapon_levels = {int(index): int(level) for index, level in (item.split(":") for item in args.weapon)}
    edited = edit(source, args.funds, unit_levels, weapon_levels)
    with args.output.open("xb") as out:
        out.write(edited)
    print(f"source sha256 {hashlib.sha256(source).hexdigest()}; output sha256 {hashlib.sha256(edited).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
