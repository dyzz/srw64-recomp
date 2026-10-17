#!/usr/bin/env python3
"""ICU's data cut down to what the game uses, for every package.

ICU's full data (icudt78l.dat in the pinned source, config/recomp/macos-dependencies.json)
is 31.6 MB: converters, collation, locale names, time zones, currencies, units. The game
calls ICU only in src/native/text/portable_text.cpp: grapheme and line breaks (root;
zh-Hans, ja and en with lb=strict), bidi and scripts. Bidi, scripts and the character
properties are compiled into ICU's code; the breaks need the break rules and the bundles
that pick them by locale (brkitr/*.brk, brkitr/*.res) and the root bundle index. The
dictionaries (brkitr/*.dict) only break Thai, Lao, Khmer and Burmese words and CJK words
in phrase mode, none of which the game does. That leaves 1.1 MB.

Checked on 2026-10-17 with ICU 78.3's static libraries: the 927 text files under content/,
each broken as zh-Hans, ja and en, give the same grapheme and line breaks, scripts and bidi
runs with this subset as with the full data; without the break rules ubrk_open fails.

Each package swaps ICU's data library for one built from the subset, exporting the same
symbol (icudt78_dat):
  c-source  C source of the data (Android links it in place of libicudata.a)
  library   a shared library: icudt78.dll (Windows, MSVC's cl and link), or
            libicudata.so.78 / libicudata.78.dylib (Linux, macOS, clang)
  subset    the subset as a .dat file
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / 'config/recomp/macos-dependencies.json'
ARCHIVES = ROOT / 'build/macos-deps/sources'
KEEP = ('brkitr/*.brk', 'brkitr/*.res', 'res_index.res', 'root.res', 'pool.res')


def pinned_archive(archives: Path = ARCHIVES) -> tuple[Path, str]:
    entry = json.loads(LOCK.read_text())['sources']['icu']
    archive = archives / entry['url'].rsplit('/', 1)[1]
    if not archive.exists():
        archives.mkdir(parents=True, exist_ok=True)
        partial = archive.with_suffix(archive.suffix + '.part')
        with urllib.request.urlopen(entry['url']) as response, partial.open('wb') as out:
            while chunk := response.read(1 << 20):
                out.write(chunk)
        partial.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
        raise ValueError(f'ICU source checksum mismatch: {archive}')
    return archive, entry['url']


def full_data(archives: Path = ARCHIVES) -> tuple[bytes, str]:
    """The pinned ICU's icudt<major>l.dat and its major version."""
    archive, _ = pinned_archive(archives)
    with tarfile.open(archive) as payload:
        for member in payload:
            name = member.name.rsplit('/', 1)[-1]
            if member.isfile() and '/source/data/in/' in member.name and name.startswith('icudt') and name.endswith('l.dat'):
                return payload.extractfile(member).read(), name[len('icudt'):-len('l.dat')]
    raise ValueError(f'No icudt*l.dat in {archive}')


def items(data: bytes) -> tuple[bytes, list[tuple[str, bytes]]]:
    """A little-endian common data file ("CmnD"): its header and (name, bytes) items."""
    header_size, magic1, magic2 = struct.unpack_from('<HB B', data, 0)
    if (magic1, magic2) != (0xda, 0x27) or data[8] != 0 or data[12:16] != b'CmnD':
        raise ValueError('Not little-endian ICU common data')
    count, = struct.unpack_from('<I', data, header_size)
    table = [struct.unpack_from('<II', data, header_size + 4 + 8 * i) for i in range(count)]
    found = []
    for i, (name_offset, data_offset) in enumerate(table):
        start = header_size + name_offset
        name = data[start:data.index(b'\0', start)].decode('ascii')
        end = header_size + table[i + 1][1] if i + 1 < count else len(data)
        found.append((name, data[header_size + data_offset:end]))
    return data[:header_size], found


def pack(header: bytes, kept: list[tuple[str, bytes]]) -> bytes:
    """As icupkg writes it: names after the table, 0xAA up to the first item, items 16-aligned."""
    names, name_offsets = b'', []
    for name, _ in kept:
        name_offsets.append(4 + 8 * len(kept) + len(names))
        names += name.encode('ascii') + b'\0'
    first = 4 + 8 * len(kept) + len(names)
    gap = -(len(header) + first) % 16
    body, data_offsets = b'', []
    for _, blob in kept:
        data_offsets.append(first + gap + len(body))
        body += blob + b'\0' * (-(len(header) + first + gap + len(body) + len(blob)) % 16)
    table = struct.pack('<I', len(kept)) + b''.join(struct.pack('<II', n, d) for n, d in zip(name_offsets, data_offsets))
    return header + table + names + b'\xaa' * gap + body


def subset(data: bytes) -> bytes:
    header, found = items(data)
    if pack(header, found) != data:
        raise ValueError('ICU data does not repack to itself')
    prefix = found[0][0].split('/', 1)[0] + '/'
    kept = [(name, blob) for name, blob in found if any(fnmatch.fnmatchcase(name[len(prefix):], p) for p in KEEP)]
    if not any(name.endswith('/brkitr/line.brk') for name, _ in kept):
        raise ValueError('ICU data subset has no line break rules')
    return pack(header, kept)


def c_source(data: bytes, symbol: str) -> str:
    # genccode's layout: a double first keeps the bytes aligned (ICU skips it).
    lines = [f'/* ICU data subset, tools/release/icu_data.py. */',
             '#include <stdint.h>',
             '#ifdef __cplusplus', 'extern "C"', '#endif',
             '#if defined(_WIN32)', '__declspec(dllexport)', '#else', '__attribute__((visibility("default")))', '#endif',
             f'const struct {{ double bogus; uint8_t bytes[{len(data)}]; }} {symbol} = {{ 0.0, {{']
    lines += [','.join(map(str, data[i:i + 32])) + ',' for i in range(0, len(data), 32)]
    lines.append('} };')
    return '\n'.join(lines) + '\n'


def library(data: bytes, major: str, output: Path, install_name: str | None = None, minimum_macos: str = '14.0') -> Path:
    symbol = f'icudt{major}_dat'
    with tempfile.TemporaryDirectory(prefix='srw64-icudata-') as work:
        source = Path(work) / f'{symbol}.c'
        source.write_text(c_source(data, symbol))
        if sys.platform == 'win32':
            obj = Path(work) / f'{symbol}.obj'
            subprocess.run(['cl', '/nologo', '/c', '/O1', f'/Fo{obj}', str(source)], check=True)
            # A data-only DLL, like ICU's own: no entry point, no C runtime.
            subprocess.run(['link', '/nologo', '/DLL', '/NOENTRY', '/MACHINE:X64', f'/OUT:{output}',
                            f'/IMPLIB:{Path(work) / (symbol + ".lib")}', str(obj)], check=True)
            exports = subprocess.run(['dumpbin', '/nologo', '/exports', str(output)], check=True, capture_output=True, text=True).stdout
            if symbol not in exports:
                raise ValueError(f'{output} does not export {symbol}')
        elif sys.platform == 'darwin':
            subprocess.run(['clang', '-dynamiclib', '-O1', f'-mmacosx-version-min={minimum_macos}',
                            '-install_name', install_name or f'@rpath/{output.name}',
                            '-compatibility_version', f'{major}.0', '-current_version', f'{major}.0',
                            '-o', str(output), str(source)], check=True)
        else:
            subprocess.run([os.environ.get('CC', 'clang'), '-shared', '-O1', '-fPIC', f'-Wl,-soname,{output.name}',
                            '-o', str(output), str(source)], check=True)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('kind', choices=('c-source', 'library', 'subset'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--install-name', help='macOS: the install name of the library replaced')
    parser.add_argument('--archives', type=Path, default=ARCHIVES,
                        help='where the pinned ICU source archive is kept or downloaded to')
    args = parser.parse_args()
    data, major = full_data(args.archives)
    small = subset(data)
    if args.kind == 'subset':
        args.out.write_bytes(small)
    elif args.kind == 'c-source':
        args.out.write_text(c_source(small, f'icudt{major}_dat'))
    else:
        library(small, major, args.out, args.install_name)
    print(f'{args.out}: ICU {major} data, {len(small) / 1048576:.2f} of {len(data) / 1048576:.2f} MB')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
