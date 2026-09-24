#!/usr/bin/env python3
"""Author floating-point host geometry for the native 5600 replacement."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'src'))
from srw64_rom.resources import ResourceTable


# The original 5600 is an eight-face double pyramid: a square waist at Y = 6 with
# corners (+-5, 6, +-5), a short top tip at Y = 12 and a long bottom tip at Y = -12.
WAIST = [(5, 6, 5), (-5, 6, 5), (-5, 6, -5), (5, 6, -5)]
INSIDE = (0, 5, 0)


def _sub(a, b): return tuple(a[i]-b[i] for i in range(3))
def _add(a, b): return tuple(a[i]+b[i] for i in range(3))
def _mul(a, s): return tuple(v*s for v in a)
def _dot(a, b): return sum(a[i]*b[i] for i in range(3))
def _cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _unit(a): return _mul(a, 1/math.sqrt(_dot(a, a)))
def _nlerp(a, b, t): return _unit(_add(_mul(a, 1-t), _mul(b, t)))


def _solve3(m, v):
    det = lambda a: (a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1]) - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
                     + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]))
    d = det(m)
    return tuple(det([[v[r] if c == k else m[r][c] for c in range(3)] for r in range(3)])/d for k in range(3))


def _rounded(radius, segments, top, bottom):
    """Minkowski sum of the double pyramid shrunk by `radius` with a sphere of that
    radius: the eight faces stay flat on the original planes, the twelve edges become
    cylinders and the six corners sphere patches, with continuous normals."""
    points = WAIST+[(0, top, 0), (0, bottom, 0)]
    faces = [(4, i, (i+1) % 4) for i in range(4)]+[(5, (i+1) % 4, i) for i in range(4)]
    planes = []
    for f in faces:
        a, b, c = (points[i] for i in f)
        n = _unit(_cross(_sub(b, a), _sub(c, a)))
        if _dot(n, _sub(a, INSIDE)) < 0: n = _mul(n, -1)
        planes.append((n, _dot(n, a)))
    shrunk = []
    for v in range(len(points)):
        adjacent = [planes[k] for k, f in enumerate(faces) if v in f]
        m = [[sum(n[r]*n[c] for n, _ in adjacent) for c in range(3)] for r in range(3)]
        shrunk.append(_solve3(m, [sum(n[r]*(d-radius) for n, d in adjacent) for r in range(3)]))
    positions, normals, triangles, index = [], [], [], {}
    def vertex(p, n):
        key = tuple(round(x, 6) for x in p)+tuple(round(x, 5) for x in n)
        if key not in index:
            index[key] = len(positions); positions.append(list(p)); normals.append(list(n))
        return index[key]
    def triangle(a, b, c):
        g = _cross(_sub(positions[b], positions[a]), _sub(positions[c], positions[a]))
        if _dot(g, _add(_add(normals[a], normals[b]), normals[c])) < 0: b, c = c, b
        triangles.append([a, b, c])
    for (n, _), f in zip(planes, faces):
        triangle(*(vertex(_add(shrunk[i], _mul(n, radius)), n) for i in f))
    edges = {}
    for k, f in enumerate(faces):
        for i in range(3):
            edges.setdefault(tuple(sorted((f[i], f[(i+1) % 3]))), []).append(k)
    for (u, v), (fa, fb) in edges.items():
        na, nb = planes[fa][0], planes[fb][0]
        for s in range(segments):
            d0, d1 = _nlerp(na, nb, s/segments), _nlerp(na, nb, (s+1)/segments)
            a, b = vertex(_add(shrunk[u], _mul(d0, radius)), d0), vertex(_add(shrunk[v], _mul(d0, radius)), d0)
            c, d = vertex(_add(shrunk[v], _mul(d1, radius)), d1), vertex(_add(shrunk[u], _mul(d1, radius)), d1)
            triangle(a, b, c); triangle(a, c, d)
    for v in range(len(points)):
        ns = [planes[k][0] for k, f in enumerate(faces) if v in f]
        axis = _unit(tuple(sum(n[i] for n in ns) for i in range(3)))
        ref = _unit(_sub(ns[0], _mul(axis, _dot(ns[0], axis))))
        side = _cross(axis, ref)
        ns.sort(key=lambda n: math.atan2(_dot(n, side), _dot(n, ref)))
        for i in range(len(ns)):
            a, b = ns[i], ns[(i+1) % len(ns)]
            grid = {}
            for p in range(segments+1):
                for q in range(segments+1-p):
                    w = segments-p-q
                    # Boundary directions use the same nlerp as the edge strips, so seams close.
                    if q == 0: d = _nlerp(axis, a, p/segments) if p else axis
                    elif p == 0: d = _nlerp(axis, b, q/segments)
                    elif w == 0: d = _nlerp(a, b, q/segments)
                    else: d = _unit(_add(_add(_mul(axis, w/segments), _mul(a, p/segments)), _mul(b, q/segments)))
                    grid[p, q] = vertex(_add(shrunk[v], _mul(d, radius)), d)
            for p in range(segments):
                for q in range(segments-p):
                    triangle(grid[p, q], grid[p+1, q], grid[p, q+1])
                    if p+q < segments-1: triangle(grid[p+1, q], grid[p+1, q+1], grid[p, q+1])
    return {'positions': positions, 'normals': normals, 'faces': triangles}


def make_mesh(radius=.5, segments=6):
    """The original faceted marker with rounded edges that catch the highlights; the
    tips are pushed out by what the rounding takes off, so Y still spans -12..12."""
    top, bottom = 12.0, -12.0
    for _ in range(12):
        mesh = _rounded(radius, segments, top, bottom)
        ys = [p[1] for p in mesh['positions']]
        top += 12-max(ys); bottom += -12-min(ys)
    mesh = _rounded(radius, segments, top, bottom)
    for p in mesh['positions']:
        if abs(abs(p[1])-12) < 1e-6: p[1] = math.copysign(12, p[1])
    key = lambda i: tuple(round(v, 5) for v in mesh['positions'][i])
    edges = Counter(tuple(sorted((key(f[i]), key(f[(i+1) % 3])))) for f in mesh['faces'] for i in range(3))
    if set(edges.values()) != {2}: raise ValueError('Not a closed surface')
    return mesh


def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def validate(directory):
    directory=directory.resolve();m=json.loads((directory/'manifest.json').read_text())
    if m['schema']!='srw64.native-marker.v1' or m.get('resource_id')!=5600:raise ValueError('Unsupported native mesh pack')
    required={'reference.bin','vertices.bin','indices.bin','mesh.json','waterdrop.obj'}
    if set(m['files'])!=required:raise ValueError('Incomplete native mesh pack')
    for name,expected in m['files'].items():
        p=directory/name
        if p.parent!=directory or digest(p)!=expected:raise ValueError('Native marker asset drift')
    if m.get('original_resource_sha256')!='3891b8b1462c80159de4da706b8523aa4e3c1f2c71bbefc1beb4acd0fe67d069' or m['files']['reference.bin']!='9f97063fbdc656bb4fe0355ebffe710b9b3a1ec3eb89a35769526188daf432ad':
        raise ValueError('Unrecognized original marker')
    mesh=json.loads((directory/'mesh.json').read_text())
    if len(mesh['positions'])!=m['vertices'] or len(mesh['normals'])!=m['vertices'] or len(mesh['faces'])!=m['triangles']:
        raise ValueError('Native marker geometry counts differ')
    if not mesh['positions'] or not mesh['faces']:raise ValueError('Empty native mesh')
    for p,n in zip(mesh['positions'],mesh['normals']):
        if len(p)!=3 or len(n)!=3 or not all(math.isfinite(v) for v in p+n) or abs(sum(v*v for v in n)-1)>1e-4:
            raise ValueError('Invalid native position or normal')
    for face in mesh['faces']:
        if len(face)!=3 or any(type(i)!=int or not 0<=i<m['vertices'] for i in face):raise ValueError('Invalid native mesh index')
    packed=b''.join(struct.pack('<6f',*p,*n) for p,n in zip(mesh['positions'],mesh['normals']))
    if packed!=(directory/'vertices.bin').read_bytes():raise ValueError('Preview and GPU vertices differ')
    packed=b''.join(struct.pack('<3I',*f) for f in mesh['faces'])
    if packed!=(directory/'indices.bin').read_bytes():raise ValueError('Preview and GPU indices differ')
    return {'path':str(directory),'manifest_sha256':digest(directory/'manifest.json'),**m}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    rom=(ROOT/'rom.z64').read_bytes()
    if hashlib.sha256(rom).hexdigest()!='ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e':raise ValueError('Wrong ROM')
    original,_=ResourceTable(rom).extract(5600)
    if hashlib.sha256(original).hexdigest()!='3891b8b1462c80159de4da706b8523aa4e3c1f2c71bbefc1beb4acd0fe67d069':raise ValueError('Wrong 5600')
    mesh=make_mesh();out=args.output;out.mkdir(parents=True,exist_ok=False)
    (out/'reference.bin').write_bytes(b''.join(original[i:i+4][::-1] for i in range(0,len(original),4)))
    (out/'vertices.bin').write_bytes(b''.join(struct.pack('<6f',*p,*n) for p,n in zip(mesh['positions'],mesh['normals'])))
    (out/'indices.bin').write_bytes(b''.join(struct.pack('<3I',*f) for f in mesh['faces']))
    (out/'mesh.json').write_text(json.dumps(mesh,separators=(',',':'))+'\n')
    # The file keeps its first-version name; validate() and the occlusion pack expect it.
    (out/'waterdrop.obj').write_text('# Native host geometry, no N64 vertex quantization\n'+'\n'.join(
        ['v '+' '.join(map(str,p)) for p in mesh['positions']]+['vn '+' '.join(map(str,n)) for n in mesh['normals']]+
        ['f '+' '.join(f'{i+1}//{i+1}' for i in f) for f in mesh['faces']])+'\n')
    manifest={'schema':'srw64.native-marker.v1','resource_id':5600,'vertices':len(mesh['positions']),'triangles':len(mesh['faces']),
              'material':'golden faceted marker (original eight faces, 0.5-unit rounded edges); per-fragment studio lighting, specular highlights and Fresnel rim',
              'original_resource_sha256':hashlib.sha256(original).hexdigest(),
              'files':{name:digest(out/name) for name in ('reference.bin','vertices.bin','indices.bin','mesh.json','waterdrop.obj')}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(validate(out)))


if __name__=='__main__':main()
