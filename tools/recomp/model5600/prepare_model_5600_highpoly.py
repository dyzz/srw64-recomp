#!/usr/bin/env python3
"""Build a 96-face marker and an isolated, hash-locked RT64 task experiment.

The arena below is for this captured task only. It is not a guest allocator,
ROM patch, or live-game model replacement hook.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

SOURCE_HASH = 'd62ff43849b7f9ea280b1d28163e9d56ef5c92ad3991d3ecc1a466cd49269f46'
TASK_HASH = '086cef1486425cc6e59d9f8c0cf72f50b693bc8f54da748ab760307e9e1c9547'
MODEL_HASH = '3891b8b1462c80159de4da706b8523aa4e3c1f2c71bbefc1beb4acd0fe67d069'
MODEL_ADDRESS = 0x2BDE68
ARENA_ADDRESS = 0x700000
PRECISION = 256
SOLID_COMMANDS = (0x1A90, 0x1A98, 0x1AA0, 0x1AA8, 0x1B18, 0x1B20, 0x1B28, 0x1B30)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def face_normal(points):
    a, b, c = points
    return cross([b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)])


def make_mesh() -> dict:
    # Rounded square section: four samples on each radius-1 corner, preserving
    # the original X/Z extents. Three rings add a narrow beveled waist.
    outline = []
    for cx, cz, start in ((4, 4, 0), (-4, 4, 90), (-4, -4, 180), (4, -4, 270)):
        for step in range(4):
            angle = math.radians(start + step*30)
            outline.append((cx+math.cos(angle), cz+math.sin(angle)))
    points = [(x*radius, y, z*radius) for y, radius in ((4.5, .92), (6, 1), (7.5, .84)) for x, z in outline]
    points += [(0, -12, 0), (0, 12, 0)]
    # Author/export from the exact same quantized positions consumed by RT64.
    packed = [tuple(round(v*PRECISION) for v in point) for point in points]
    points = [tuple(v/PRECISION for v in point) for point in packed]
    faces = []
    for i in range(16):
        j = (i+1) % 16
        faces += [(48, i, j), (49, 32+i, 32+j)]
        for ring in range(2):
            a, b, c, d = ring*16+i, ring*16+j, (ring+1)*16+j, (ring+1)*16+i
            faces += [(a, b, c), (a, c, d)]
    oriented = []
    colors = []
    light = (-.55, .8, .7)
    light_length = math.sqrt(sum(v*v for v in light))
    for face in faces:
        n = face_normal([points[i] for i in face])
        center = [sum(points[i][axis] for i in face)/3 for axis in range(3)]
        if sum(n[axis]*(center[axis]-(6 if axis == 1 else 0)) for axis in range(3)) < 0:
            face = (face[0], face[2], face[1])
            n = tuple(-v for v in n)
        length = math.sqrt(sum(v*v for v in n))
        if not length:
            raise RuntimeError('Degenerate high-poly face')
        shade = .59 + .41*max(0, sum(n[i]*light[i] for i in range(3))/length/light_length)
        colors.append([round(c*shade) for c in (255, 248, 12)] + [255])
        oriented.append(face)
    edges = Counter(tuple(sorted((face[i], face[(i+1) % 3]))) for face in oriented for i in range(3))
    if len(points) != 50 or len(oriented) != 96 or set(edges.values()) != {2} or len(points)-len(edges)+len(oriented) != 2:
        raise RuntimeError('Expected a closed, manifold 96-triangle solid')
    bounds = [[min(p[i] for p in points), max(p[i] for p in points)] for i in range(3)]
    if bounds != [[-5, 5], [-12, 12], [-5, 5]]:
        raise RuntimeError('High-poly marker changed the original extents')
    return {'positions': points, 'packed_positions': packed, 'faces': oriented, 'colors': colors,
            'triangles': len(oriented), 'vertices': len(points), 'bounds': bounds}


def compile_arena(mesh: dict) -> tuple[bytes, int, dict]:
    # N64 Mtx: separate signed integer and unsigned fractional 4x4 halves.
    fixed = [0]*16
    for i in (0, 5, 10):
        fixed[i] = 65536//PRECISION
    fixed[15] = 65536
    arena = bytearray(struct.pack('>16h16H', *(v >> 16 for v in fixed), *(v & 65535 for v in fixed)))
    for face, color in zip(mesh['faces'], mesh['colors']):
        for index in face:
            arena += struct.pack('>hhhHhh4B', *mesh['packed_positions'][index], 0, 0, 0, *color)
    entry = ARENA_ADDRESS + len(arena)
    commands = []

    def emit(a, b=0):
        commands.append((a, b))

    # F3DEX2 XORs the encoded matrix push bit: 0 => MUL + PUSH.
    emit(0xDA380000, ARENA_ADDRESS)
    emit(0xE7000000)
    emit(0xD7000000, 0xFFFFFFFF)
    # Both combiner cycles: (0-0)*0 + SHADE, including vertex alpha.
    emit(0xFCFFFFFF, 0xFFFE793C)
    # The captured marker uses unlit geometry. Duplicate face vertices carry
    # baked yellow facet colors, not normals; do not enable G_LIGHTING here.
    count = len(mesh['faces'])*3
    for first in range(0, count, 30):
        n = min(30, count-first)
        emit(0x01000000 | (n << 12) | (n << 1), ARENA_ADDRESS+64+first*16)
        for v in range(0, n, 6):
            a = (v*2 << 16) | ((v+1)*2 << 8) | ((v+2)*2)
            b = ((v+3)*2 << 16) | ((v+4)*2 << 8) | ((v+5)*2)
            emit(0x06000000 | a, b)
    emit(0xE7000000)
    emit(0xFC11FE23, 0xFFFFF3F9)
    emit(0xD7000002, 0xFFFFFFFF)
    emit(0xD8380002, 64)
    emit(0xDF000000)
    for a, b in commands:
        arena += struct.pack('>II', a, b)
    # Independently decode the serialized vertex loads/indices, including
    # bounds and <=32-entry cache limits, and compare exact drawn triangles.
    cache = {}
    decoded = []
    matrix_depth = 0
    for offset in range(entry-ARENA_ADDRESS, len(arena), 8):
        a, b = struct.unpack_from('>II', arena, offset)
        if a >> 24 == 0xDA:
            if a != 0xDA380000 or b != ARENA_ADDRESS:
                raise RuntimeError('Unexpected replacement matrix command')
            matrix_depth += 1
        elif a >> 24 == 0xD8:
            matrix_depth -= b >> 6
            if matrix_depth < 0:
                raise RuntimeError('Replacement popped the caller matrix')
        elif a >> 24 == 1:
            n = (a >> 12) & 255
            first = ((a >> 1) & 127)-n
            if not 0 <= first < first+n <= 32:
                raise RuntimeError('Invalid vertex cache load')
            for i in range(n):
                pos = b-ARENA_ADDRESS+i*16
                if not 64 <= pos < entry-ARENA_ADDRESS:
                    raise RuntimeError('Vertex read outside serialized geometry')
                cache[first+i] = struct.unpack_from('>hhh', arena, pos)
        elif a >> 24 == 6:
            for word in (a, b):
                decoded.append([cache[((word >> shift) & 255)//2] for shift in (16, 8, 0)])
    expected = [[tuple(mesh['packed_positions'][i]) for i in face] for face in mesh['faces']]
    if decoded != expected or matrix_depth:
        raise RuntimeError('Emitted display list differs from authored mesh or matrix stack is unbalanced')
    return bytes(arena), entry, {'commands': len(commands), 'vertex_loads': sum(a >> 24 == 1 for a, _ in commands),
                                'drawn_solid_triangles': len(decoded), 'matrix_stack_delta': matrix_depth}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    original = (source/'latest-gfx-rdram.bin').read_bytes()
    task = (source/'latest-gfx-task.bin').read_bytes()
    if len(original) != 0x800000 or digest(original) != SOURCE_HASH or digest(task) != TASK_HASH:
        raise RuntimeError('Only the reviewed female-story-2 task snapshot is supported')
    model = original[MODEL_ADDRESS:MODEL_ADDRESS+7048]
    if digest(model) != MODEL_HASH:
        raise RuntimeError('Original 5600 is not intact')
    # Segment 0 is explicitly zero in this root; the arena uses physical addresses.
    if original[0x49C00:0x49C08] != bytes.fromhex('db06000000000000'):
        raise RuntimeError('Unexpected segment mapping')
    mesh = make_mesh()
    arena, entry, decoded = compile_arena(mesh)
    if any(original[ARENA_ADDRESS:ARENA_ADDRESS+len(arena)]):
        raise RuntimeError('Reviewed replay-only arena is not empty')
    patched = bytearray(original)
    patched[ARENA_ADDRESS:ARENA_ADDRESS+len(arena)] = arena
    patches = []
    for i, offset in enumerate(SOLID_COMMANDS):
        absolute = MODEL_ADDRESS+offset
        if original[absolute] != 0x05:
            raise RuntimeError('Expected original solid triangle command')
        target = struct.pack('>II', 0xDE000000, entry) if i == 0 else bytes.fromhex('e000000000000000')
        patched[absolute:absolute+8] = target
        patches.append({'offset': absolute, 'before': original[absolute:absolute+8].hex(), 'after': target.hex()})
    allowed = set(range(ARENA_ADDRESS, ARENA_ADDRESS+len(arena))) | {MODEL_ADDRESS+o+i for o in SOLID_COMMANDS for i in range(8)}
    changes = {i for i, (a, b) in enumerate(zip(original, patched)) if a != b}
    if not changes <= allowed or len(patched) != len(original):
        raise RuntimeError('Patch escaped its allowlist')
    args.output.mkdir(parents=True, exist_ok=False)
    records = []
    for label, data in (('baseline', original), ('highpoly', patched)):
        folder = args.output/(label+'-source')
        folder.mkdir()
        (folder/'latest-gfx-rdram.bin').write_bytes(data)
        (folder/'latest-gfx-task.bin').write_bytes(task)
        records.append({'name': label, 'source': str(folder.resolve()), 'rdram_sha256': digest(data), 'task_sha256': digest(task)})
    # Source positions/faces are also suitable for the local Three.js viewer.
    (args.output/'mesh.json').write_text(json.dumps(mesh, separators=(',', ':'))+'\n')
    obj = ['# SRW64 5600 high-poly prototype; original local units']
    obj += ['v '+' '.join(map(str, p)) for p in mesh['positions']]
    obj += ['f '+' '.join(str(i+1) for i in face) for face in mesh['faces']]
    (args.output/'5600-highpoly-solid.obj').write_text('\n'.join(obj)+'\n')
    (args.output/'arena.bin').write_bytes(arena)
    report = {'schema': 'srw64.model-5600-highpoly-fixtures.v1', 'resource_id': 5600,
              'scope': 'captured-task-only; no ROM patch or live-game allocator', 'source': str(source),
              'original_rdram_sha256': SOURCE_HASH, 'model_sha256': MODEL_HASH,
              'original_solid_triangles': 8, 'replacement_solid_triangles': mesh['triangles'],
              'replacement_total_triangles': mesh['triangles']+8, 'replacement_solid_vertices': mesh['vertices'],
              'original_ring_commands_unchanged': True, 'arena_address': ARENA_ADDRESS,
              'arena_bytes': len(arena), 'arena_sha256': digest(arena), 'entry': entry,
              'position_precision': PRECISION, 'bounds': mesh['bounds'], 'static_decode': decoded,
              'mesh_sha256': digest((args.output/'mesh.json').read_bytes()),
              'changed_bytes': len(changes), 'patches': patches, 'fixtures': records}
    (args.output/'fixtures.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'output': str(args.output), 'solid_triangles': mesh['triangles'], 'arena_bytes': len(arena),
                      'changed_bytes': len(changes), 'static_decode': decoded}))


if __name__ == '__main__':
    main()
