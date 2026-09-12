#!/usr/bin/env python3
"""Build an isolated ROM resource variant for the 96-triangle marker.

All new pointers are segment-4 offsets inside resource 5600, so the original
game loader owns allocation, relocation and lifetime. No fixed RAM arena is
used by this variant, and no executable/overlay bytes are changed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'src'))
from srw64_rom.resources import ResourceTable, lz_encode, patch_resource_to_pool
from audit_rom_variant import audit
from prepare_model_5600_highpoly import make_mesh, compile_arena, ARENA_ADDRESS, MODEL_HASH, SOLID_COMMANDS


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_resource(original: bytes) -> tuple[bytes, dict]:
    if digest(original) != MODEL_HASH:
        raise RuntimeError('Unreviewed original marker')
    arena, entry, decoded = compile_arena(make_mesh())
    offset = (len(original)+15) & ~15
    relocated = bytearray(arena)
    entry_offset = entry-ARENA_ADDRESS
    relocations = []
    for p in range(entry_offset, len(arena), 8):
        a, b = struct.unpack_from('>II', arena, p)
        if a >> 24 not in (0x01, 0xDA):
            continue
        if not ARENA_ADDRESS <= b < ARENA_ADDRESS+entry_offset:
            raise RuntimeError('Unexpected matrix/vertex relocation')
        target = 0x04000000+offset+(b-ARENA_ADDRESS)
        struct.pack_into('>I', relocated, p+4, target)
        relocations.append({'command_offset': offset+p, 'address': target})
    resource = bytearray(original)
    resource += bytes(offset-len(resource))
    resource += relocated
    for i, p in enumerate(SOLID_COMMANDS):
        struct.pack_into('>II', resource, p, 0xDE000000 if i == 0 else 0xE0000000,
                         0x04000000+offset+entry_offset if i == 0 else 0)
    allowed = {p+i for p in SOLID_COMMANDS for i in range(8)}
    if any(a != b and i not in allowed for i, (a, b) in enumerate(zip(original, resource))):
        raise RuntimeError('Original ring, material or container metadata changed')
    return bytes(resource), {'original_bytes': len(original), 'replacement_bytes': len(resource),
                            'appended_offset': offset, 'child_list_offset': offset+entry_offset,
                            'relocations': relocations, 'static_geometry': decoded}


def runtime_hash(rom: Path, output: Path) -> str:
    # Use the very same vendored xxHash implementation as the native runtime.
    source = output/'hash_rom.c'
    source.write_text('#define XXH_INLINE_ALL\n#include "xxhash.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
        'int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;'
        'fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void*p=malloc(n);if(!p||fread(p,1,n,f)!=(size_t)n)return 2;'
        'printf("%016llx\\n",(unsigned long long)XXH3_64bits(p,n));free(p);fclose(f);return 0;}\n')
    binary = output/'hash_rom'
    subprocess.run(['clang', '-O2', '-I', str(ROOT/'build/recomp/upstream/N64ModernRuntime/thirdparty/xxHash'),
                    str(source), '-o', str(binary)], check=True)
    return subprocess.check_output([str(binary), str(rom)], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rom = (ROOT/'rom.z64').read_bytes()
    if digest(rom) != 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e':
        raise RuntimeError('Unreviewed base ROM')
    table = ResourceTable(rom)
    original, _ = table.extract(5600)
    resource, model_report = build_resource(original)
    pool = 0x1FF0000
    span = (4+len(lz_encode(resource))+15) & ~15
    if any(max(pool, table.entry(i).data_offset) < min(pool+span, table.entry(i).data_offset+table.entry(i).span_size)
           for i in range(table.count)):
        raise RuntimeError('Experimental pool overlaps an existing resource')
    candidate, patch = patch_resource_to_pool(rom, 5600, resource, pool, span)
    descriptor = table.entry(5600).descriptor_offset
    allowed = set(range(pool, pool+span)) | set(range(descriptor, descriptor+8))
    changed = [i for i, (a, b) in enumerate(zip(rom, candidate)) if a != b]
    if not set(changed) <= allowed:
        raise RuntimeError('ROM patch escaped resource data and its descriptor')
    compatibility = audit(rom, candidate, digest(candidate), json.loads((ROOT/'config/recomp/code-sections.json').read_text()))
    args.output.mkdir(parents=True, exist_ok=False)
    path = args.output/'srw64-model5600.z64'
    path.write_bytes(candidate)
    (args.output/'resource-5600.bin').write_bytes(resource)
    variant = {'path': str(path.resolve().relative_to(ROOT)), 'sha256': digest(candidate),
               'xxh3_64': runtime_hash(path, args.output), 'game_id': 'srw64-model5600-experiment'}
    report = {'schema': 'srw64.model-5600-rom.v1', 'scope': 'experimental ROM resource replacement; runtime validation separate',
              'base_rom_sha256': digest(rom), 'resource_sha256': digest(resource), 'variant': variant,
              'model': model_report, 'patch': patch, 'changed_rom_bytes': len(changed),
              'only_resource_5600_relocated': True, 'code_compatibility': compatibility}
    (args.output/'build.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'variant': variant, 'model': model_report, 'patch': patch, 'changed_rom_bytes': len(changed)}, indent=2))


if __name__ == '__main__':
    main()
