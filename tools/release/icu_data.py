#!/usr/bin/env python3
"""ICU's common data cut down to what the game reads (config/recomp/icu-data.txt).

The game's only ICU caller is src/native/text/portable_text.cpp: grapheme and
line breaks (ubrk), BCP-47 tags (uloc), scripts (uscript) and bidi (ubidi).
Character properties, bidi and script data are compiled into libicuuc; from
the data library only the break rules are read. The whole library is 31.6 MB;
the kept items are under half a megabyte, with the same breaks on every line
of the game's text in all three languages (docs/native/macos-release.md).

    trim     ICU common data (a .dat, or a library carrying it such as
             icudt78.dll) -> a .dat of the kept items. Before ICU's configure
             it replaces source/data/in/icudt<v>l.dat, and ICU builds its data
             library from it (macOS, Linux, Android).
    c-source a .dat -> C defining the data symbol (icudt<v>_dat), for a data
             DLL in place of vcpkg's (Windows).

The output is laid out as icupkg writes it (same header, names sorted, items
16-byte aligned); tests/test_icu_data.py checks the layout.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEEP = ROOT / "config/recomp/icu-data.txt"
MAGIC = b"\xda\x27"


def keep_list(path: Path = KEEP) -> list[str]:
    rows = [line.split("#", 1)[0].strip() for line in path.read_text().splitlines()]
    return [row for row in rows if row]


def find_common(blob: bytes) -> int:
    """Offset of the common data (format CmnD) in a .dat or a library."""
    at = 0
    while (at := blob.find(MAGIC, at)) >= 0:
        start = at - 2
        if start >= 0 and blob[start + 12:start + 16] == b"CmnD" and blob[start + 4:start + 6] == b"\x14\x00":
            return start
        at += 1
    raise ValueError("no ICU common data (CmnD) found")


def read_common(blob: bytes) -> tuple[bytes, str, dict[str, bytes]]:
    """(header, package name, items by name without the package) of little-endian
    ASCII common data. An item ends where the next one starts, so the last item
    is only exact in a .dat (it is never one we keep)."""
    start = find_common(blob)
    header_size = struct.unpack_from("<H", blob, start)[0]
    if blob[start + 8] != 0 or blob[start + 9] != 0:
        raise ValueError("expected little-endian ASCII ICU data")
    toc = start + header_size
    count = struct.unpack_from("<I", blob, toc)[0]
    entries = [struct.unpack_from("<II", blob, toc + 4 + 8 * i) for i in range(count)]
    items, package = {}, None
    for i, (name_at, data_at) in enumerate(entries):
        end = toc + entries[i + 1][1] if i + 1 < count else len(blob)
        full = blob[toc + name_at:blob.index(b"\0", toc + name_at)].decode("ascii")
        prefix, _, name = full.partition("/")
        if package not in (None, prefix):
            raise ValueError(f"mixed packages {package} and {prefix}")
        package = prefix
        items[name] = blob[toc + data_at:end]
    return blob[start:toc], package, items


def write_common(header: bytes, package: str, items: dict[str, bytes]) -> bytes:
    names = sorted(items)
    full = [f"{package}/{name}".encode("ascii") + b"\0" for name in names]
    table = 4 + 8 * len(names)
    name_at, offsets = table, []
    for raw in full:
        offsets.append(name_at)
        name_at += len(raw)
    data_at = (name_at + 15) & ~15
    out = bytearray(header) + struct.pack("<I", len(names))
    body, starts = bytearray(), []
    for name in names:
        starts.append(data_at + len(body))
        body += items[name]
        body += b"\0" * (-len(body) % 16)
    for name_offset, start in zip(offsets, starts):
        out += struct.pack("<II", name_offset, start)
    out += b"".join(full)
    out += b"\xaa" * (data_at - name_at)   # icupkg's filler
    # The last item's padding is not part of the package.
    return bytes(out + body[:len(body) - (-len(items[names[-1]]) % 16)])


def trim(blob: bytes, keep: list[str]) -> tuple[bytes, str]:
    header, package, items = read_common(blob)
    missing = [name for name in keep if name not in items]
    if missing:
        raise ValueError(f"ICU data has no {', '.join(missing)}")
    return write_common(header, package, {name: items[name] for name in keep}), package


def c_source(dat: bytes, symbol: str) -> str:
    """The data library's one symbol, 16-byte aligned like genccode's output."""
    rows = [", ".join(str(b) for b in dat[i:i + 32]) for i in range(0, len(dat), 32)]
    return ("/* ICU common data cut down by tools/release/icu_data.py. */\n"
            "#ifdef _WIN32\n#define SRW64_DATA __declspec(dllexport) __declspec(align(16))\n#else\n"
            "#define SRW64_DATA __attribute__((visibility(\"default\"), aligned(16)))\n#endif\n"
            f"SRW64_DATA const unsigned char {symbol}[{len(dat)}] = {{\n"
            + ",\n".join(rows) + "\n};\n")


def digest(path: Path = KEEP) -> str:
    """Identifies the cut, for the dependency caches that hold a built data library."""
    return hashlib.sha256("\n".join(keep_list(path)).encode()).hexdigest()[:16]


def prepare_source(icu: Path) -> Path:
    """Puts the cut-down data where ICU's build packages it from (source/data/in) and
    lets that build accept it; the originals stay beside them (.full, .orig), so this
    can run again on the same tree."""
    dats = sorted((icu / "source/data/in").glob("icudt*l.dat"))
    if len(dats) != 1:
        raise ValueError(f"expected one icudt*l.dat in {icu}/source/data/in, found {len(dats)}")
    dat = dats[0]
    full = dat.with_name(dat.name + ".full")
    if not full.exists():
        dat.rename(full)
    trimmed, package = trim(full.read_bytes(), keep_list())
    if package != dat.stem:
        raise ValueError(f"{full} holds package {package}")
    dat.write_bytes(trimmed)
    # ICU's data build unpacks that file with icupkg, which refuses items whose references
    # are gone (the root and ja break bundles name the dictionaries and the word, sentence
    # and phrase rules); the game never opens those, so its icupkg calls ignore them.
    makefile = icu / "source/data/Makefile.in"
    original = makefile.with_name(makefile.name + ".orig")
    if not original.exists():
        makefile.rename(original)
    text = original.read_text()
    if text.count("$(TOOLBINDIR)/icupkg ") < 2:
        raise ValueError(f"unexpected icupkg calls in {original}")
    makefile.write_text(text.replace("$(TOOLBINDIR)/icupkg ", "$(TOOLBINDIR)/icupkg --ignore-deps "))
    print(f"ICU data: {full.stat().st_size} -> {len(trimmed)} bytes ({len(keep_list())} items)", flush=True)
    return dat


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    t = sub.add_parser("trim")
    t.add_argument("source", type=Path, help="icudt<v>l.dat, or a library that carries it")
    t.add_argument("output", type=Path)
    t.add_argument("--keep", type=Path, default=KEEP)
    c = sub.add_parser("c-source")
    c.add_argument("source", type=Path, help="a trimmed .dat")
    c.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command == "trim":
        dat, package = trim(args.source.read_bytes(), keep_list(args.keep))
        args.output.write_bytes(dat)
        print(f"{package}: {args.source.stat().st_size} -> {len(dat)} bytes")
    else:
        dat = args.source.read_bytes()
        _, package, _ = read_common(dat)
        version = package.removeprefix("icudt").rstrip("lbe")
        args.output.write_text(c_source(dat, f"icudt{version}_dat"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
