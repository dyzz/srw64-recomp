#!/usr/bin/env python3
"""Controlled save edit for BUG05: set a Wing pilot's kill backup in an
intermission save and fix the block checksum.

The intermission save block (the 801C2600 buffer written by 800924D8) sits at
SRAM offset 0x10; its first word is the checksum in srw64_native.original_saves.
Buffer offset 0x1EE8 (801C44E8) holds the five u16 kill backups that
800920B4 copies into 801614E0 on load; 800A5054 later copies them into new
pilot records. This stands in for real kills and must be reported as a
controlled edit, never as a normally played checkpoint.
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
BACKUP = 0x1EE8
PILOTS = {"heero": 0, "duo": 1, "trowa": 2, "quatre": 3, "wufei": 4}


def checksum(sram: bytes | bytearray) -> int:
    return original_saves.checksum(bytes(sram[BLOCK:BLOCK + BLOCK_SIZE]), tactical=False)


def read_backup(sram: bytes | bytearray) -> dict[str, int]:
    return {name: int.from_bytes(sram[BLOCK + BACKUP + 2 * i:BLOCK + BACKUP + 2 * i + 2], "big")
            for name, i in PILOTS.items()}


def set_kills(sram: bytes, pilot: str, kills: int) -> bytes:
    if len(sram) != SRAM_BYTES:
        raise ValueError(f"expected a {SRAM_BYTES}-byte SRAM, got {len(sram)}")
    if int.from_bytes(sram[BLOCK:BLOCK + 2], "big") != checksum(sram):
        raise ValueError("the intermission block checksum does not match; not an intermission save")
    if not 0 <= kills <= 999:
        raise ValueError("the game caps kills at 999")
    edited = bytearray(sram)
    at = BLOCK + BACKUP + 2 * PILOTS[pilot]
    edited[at:at + 2] = kills.to_bytes(2, "big")
    edited[BLOCK:BLOCK + 2] = checksum(edited).to_bytes(2, "big")
    return bytes(edited)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--pilot", choices=PILOTS, default="wufei")
    parser.add_argument("--kills", type=int, required=True)
    args = parser.parse_args()
    source = args.source.read_bytes()
    edited = set_kills(source, args.pilot, args.kills)
    with args.output.open("xb") as out:
        out.write(edited)
    print(f"{args.pilot}: {read_backup(source)[args.pilot]} -> {args.kills}; "
          f"source sha256 {hashlib.sha256(source).hexdigest()}; output sha256 {hashlib.sha256(edited).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
