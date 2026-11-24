#!/usr/bin/env python3
"""The Switch's HD folder in one file (src/host/hd_pack.cpp, docs/design/switch-port.md).

Opening a file on the console's FAT32 SD card walks its folder entry by entry: with 17,000
files in art/battle-sprites a 68-byte picture took half a second, a battle stopped for it.
This moves every file under an HD folder into <hd>/art.pack, which is opened once and read
at an offset; the game mounts it as the device "hd:" and reads the folder's files from it
as before. Nothing else is left in the folder but a copy of hd.json, which says which HD
pack this is to whoever looks at the folder or its zip (build_release.py, a player).

  tools/release/switch/pack_hd.py build/recomp/switch-romfs/hd

art.pack, little-endian: "MWPK", version 2, entry count, the names' byte count; then that
many entries sorted by hash (the path relative to the HD folder, FNV-1a 64: hash u64,
offset u64, size u32, the name's offset in the names u32); then the names, each ended by a
zero; then the files, each at a multiple of ALIGN.

Version 1 held only the .mwtex files and no names. A folder that already has an art.pack
keeps its entries: a version 1 pack's come over without names (NO_NAME: found by hash,
not listed in their folder), so a folder packed before can be packed again without the
pictures being prepared again.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct

MAGIC = b"MWPK"
VERSION = 2
ALIGN = 512
ENTRY = struct.Struct("<QQII")
HEADER = struct.Struct("<4sIII")
HEADER_V1 = struct.Struct("<4sII")
NAME = "art.pack"
NO_NAME = 0xFFFFFFFF
KEPT = "hd.json"   # in the pack, and beside it too


def fnv1a(text: str) -> int:
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def fnv1a_bytes(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def packed(pack: Path) -> list[tuple[int, str | None, int, int]]:
    """An existing pack's entries: hash, name (None in a version 1 pack), offset, size."""
    with pack.open("rb") as source:
        magic, version, count = HEADER_V1.unpack(source.read(HEADER_V1.size))
        if magic != MAGIC or version not in (1, 2):
            raise SystemExit(f"{pack} is not an art.pack this script knows")
        names = b""
        if version == 2:
            (names_size,) = struct.unpack("<I", source.read(4))
        table = source.read(ENTRY.size * count)
        if version == 2:
            names = source.read(names_size)
    entries = []
    for index in range(count):
        value, offset, size, name_offset = ENTRY.unpack_from(table, index * ENTRY.size)
        name = None
        if version == 2 and name_offset != NO_NAME:
            name = names[name_offset:names.index(b"\0", name_offset)].decode("utf-8")
        entries.append((value, name, offset, size))
    return entries


def pack(folder: Path) -> tuple[int, int]:
    """Writes folder/art.pack and removes the files that went in: the entry count and the pack's bytes."""
    old_pack = folder / NAME
    carried = packed(old_pack) if old_pack.exists() else []
    files = sorted(path for path in folder.rglob("*") if path.is_file() and path.name not in (NAME, NAME + ".part"))
    if not files and not carried:
        raise SystemExit(f"nothing to pack under {folder}")
    # hash -> (name, the loose file or None, the old pack's offset, size); a loose file wins.
    entries: dict[int, tuple[str | None, Path | None, int, int]] = {value: (name, None, offset, size) for value, name, offset, size in carried}
    seen: dict[int, str] = {}
    for path in files:
        name = path.relative_to(folder).as_posix()
        value = fnv1a(name)
        if value in seen:
            raise SystemExit(f"{name} and {seen[value]} have the same hash")
        seen[value] = name
        entries[value] = (name, path, 0, path.stat().st_size)
    for value, (name, path, _, _) in entries.items():
        if path is None and name is not None and value in seen:
            raise SystemExit(f"{name} is in the old pack and in the folder under another name")
    ordered = sorted(entries.items())
    names = bytearray()
    name_offsets = []
    for _, (name, _, _, _) in ordered:
        if name is None:
            name_offsets.append(NO_NAME)
        else:
            name_offsets.append(len(names))
            names += name.encode("utf-8") + b"\0"
    start = HEADER.size + ENTRY.size * len(ordered) + len(names)
    offset = (start + ALIGN - 1) // ALIGN * ALIGN
    target = folder / (NAME + ".part")
    table = bytearray(HEADER.pack(MAGIC, VERSION, len(ordered), len(names)))
    old = old_pack.open("rb") if carried else None
    with target.open("wb") as out:
        out.write(b"\0" * offset)
        for (value, (name, path, old_offset, size)), name_offset in zip(ordered, name_offsets):
            if path is not None:
                data = path.read_bytes()
            else:
                old.seek(old_offset)
                data = old.read(size)
            if len(data) >= 1 << 32:
                raise SystemExit(f"{name} is too large for the pack")
            table += ENTRY.pack(value, offset, len(data), name_offset)
            out.write(data)
            offset += len(data)
            padding = -offset % ALIGN
            out.write(b"\0" * padding)
            offset += padding
        out.seek(0)
        out.write(table)
        out.write(names)
    if old is not None:
        old.close()
    target.replace(old_pack)
    for path in files:
        if path.relative_to(folder).as_posix() != KEPT:
            path.unlink()
    if not (folder / KEPT).exists():   # packed before: out of the pack again
        for (value, _), row in zip(ordered, range(len(ordered))):
            if value == fnv1a(KEPT):
                _, kept_offset, kept_size, _ = ENTRY.unpack_from(table, HEADER.size + row * ENTRY.size)
                with old_pack.open("rb") as source:
                    source.seek(kept_offset)
                    (folder / KEPT).write_bytes(source.read(kept_size))
    # The folders the files were in, now empty, deepest first.
    for directory in sorted((p for p in folder.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    return len(ordered), offset


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path, help="an HD folder prepare-hd wrote")
    args = parser.parse_args()
    count, size = pack(args.folder)
    print(f"{args.folder / NAME}: {count} files, {size / 1048576:.1f} MiB")


if __name__ == "__main__":
    main()
