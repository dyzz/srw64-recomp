#!/usr/bin/env python3
"""Prepare isolated 5600 hide/stretch task fixtures; never patches a live game."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'src'))
from srw64_rom.resources import ResourceTable


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rom = (ROOT / 'rom.z64').read_bytes()
    if digest(rom) != 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e':
        raise RuntimeError('ROM differs from reviewed baseline')
    model, _ = ResourceTable(rom).extract(5600)
    if digest(model) != '3891b8b1462c80159de4da706b8523aa4e3c1f2c71bbefc1beb4acd0fe67d069':
        raise RuntimeError('Model differs from reviewed geometry')
    source = args.source.resolve()
    original = (source / 'latest-gfx-rdram.bin').read_bytes()
    task = (source / 'latest-gfx-task.bin').read_bytes()
    address = original.find(model)
    if len(original) != 0x800000 or address < 0 or original.find(model, address+1) >= 0:
        raise RuntimeError('Expected one complete, unmodified 5600 in an 8 MiB snapshot')
    if len(task) != 64 or struct.unpack_from('<I',task)[0] != 1:
        raise RuntimeError('Expected native 64-byte graphics task')
    hidden = bytearray(original); stretched = bytearray(original); removed=[]; changed=[]
    first = struct.unpack_from('>I', model, 20)[0]
    for offset in range(first, len(model), 8):
        a,b = struct.unpack_from('>II',model,offset)
        if a>>24 == 0xDF: break
        if a>>24 in (0x05,0x06):
            hidden[address+offset:address+offset+8] = bytes.fromhex('e000000000000000')
            removed.append({'relative_offset':offset,'original':model[offset:offset+8].hex()})
    if len(removed) != 12:
        raise RuntimeError('Unexpected triangle command count')
    # Six referenced vertices form the solid double pyramid. Four earlier
    # vertices belong to the textured plane and must remain unchanged.
    expected = {0x1888:(-5,6,-5),0x1898:(-5,6,5),0x18A8:(0,12,0),
                0x18B8:(5,6,5),0x18C8:(5,6,-5),0x18D8:(0,-12,0)}
    for offset, xyz in expected.items():
        if struct.unpack_from('>hhh',model,offset) != xyz:
            raise RuntimeError('Unexpected solid vertex coordinates')
        target = (xyz[0]*2,xyz[1]*3,xyz[2]*2)
        struct.pack_into('>hhh',stretched,address+offset,*target)
        changed.append({'relative_offset':offset,'before':xyz,'after':target})
    allowed_hidden={address+r['relative_offset']+i for r in removed for i in range(8)}
    allowed_stretch={address+r['relative_offset']+i for r in changed for i in range(6)}
    args.output.mkdir(parents=True,exist_ok=False)
    records=[]
    for label,data,allowed in [('baseline',original,set()),('hidden',hidden,allowed_hidden),('stretched',stretched,allowed_stretch)]:
        differences=[i for i,(a,b) in enumerate(zip(original,data)) if a!=b]
        if not set(differences)<=allowed:
            raise RuntimeError('Patch changed bytes outside its allowlist')
        folder=args.output/(label+'-source');folder.mkdir()
        (folder/'latest-gfx-rdram.bin').write_bytes(data)
        (folder/'latest-gfx-task.bin').write_bytes(task)
        records.append({'name':label,'source':str(folder.resolve()),'rdram_sha256':digest(data),
                        'task_sha256':digest(task),'changed_bytes':len(differences)})
    report={'schema':'srw64.model-5600-fixtures.v1','scope':'isolated captured-task copies; no game logic or replacement loader',
            'source':str(source),'rom_sha256':digest(rom),'original_rdram_sha256':digest(original),
            'resource_id':5600,'model_address':address,'model_sha256':digest(model),
            'hidden_commands':removed,'stretched_solid_vertices':changed,'fixtures':records}
    (args.output/'fixtures.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'output':str(args.output),'resource_address':hex(address),'fixtures':records},indent=2))


if __name__ == '__main__': main()
