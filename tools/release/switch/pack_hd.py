#!/usr/bin/env python3
"""The Switch's prepared pictures in one file (src/host/hd_texture.cpp, docs/design/switch-port.md).

Opening a file on the console's FAT32 SD card walks its folder entry by entry: with 17,000
files in art/battle-sprites a 68-byte picture took half a second, a battle stopped for it.
This moves every .mwtex under an HD folder into <hd>/art.pack, which is opened once and read
at an offset. Everything else stays where it is.

  tools/release/switch/pack_hd.py build/recomp/switch-romfs/hd

art.pack, little-endian: "MWPK", version, entry count, then that many entries sorted by hash
(the path relative to the HD folder, FNV-1a 64: hash u64, offset u64, size u32, reserved u32),
then the files, each at a multiple of ALIGN.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct

MAGIC = b"MWPK"
VERSION = 1
ALIGN = 512
ENTRY = struct.Struct("<QQII")
HEADER = struct.Struct("<4sII")
NAME = "art.pack"


def fnv1a(text: str) -> int:
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def pack(folder: Path) -> tuple[int, int]:
    """Writes folder/art.pack and removes the files that went in: their count and the pack's bytes."""
    pictures = sorted(path for path in folder.rglob("*.mwtex") if path.is_file())
    if not pictures:
        raise SystemExit(f"no .mwtex under {folder}")
    entries = sorted((fnv1a(path.relative_to(folder).as_posix()), path) for path in pictures)
    hashes = [value for value, _ in entries]
    if len(set(hashes)) != len(hashes):
        raise SystemExit("two pictures have the same hash")
    start = HEADER.size + ENTRY.size * len(entries)
    offset = (start + ALIGN - 1) // ALIGN * ALIGN
    target = folder / (NAME + ".part")
    table = bytearray(HEADER.pack(MAGIC, VERSION, len(entries)))
    with target.open("wb") as out:
        out.write(b"\0" * offset)
        for value, path in entries:
            data = path.read_bytes()
            table += ENTRY.pack(value, offset, len(data), 0)
            out.write(data)
            offset += len(data)
            padding = -offset % ALIGN
            out.write(b"\0" * padding)
            offset += padding
        out.seek(0)
        out.write(table)
    target.replace(folder / NAME)
    for _, path in entries:
        path.unlink()
    return len(entries), offset


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path, help="an HD folder prepare-hd wrote")
    args = parser.parse_args()
    count, size = pack(args.folder)
    print(f"{args.folder / NAME}: {count} pictures, {size / 1048576:.1f} MiB")


if __name__ == "__main__":
    main()
