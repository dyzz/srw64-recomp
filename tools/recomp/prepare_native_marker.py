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

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'src'))
from srw64_rom.resources import ResourceTable


def make_mesh(sectors=64, bands=32):
    def profile(t):
        return math.sqrt(max(0,math.sin(math.pi*t)))*t**.7
    maximum=max(profile(i/10000) for i in range(1,10000))
    positions, normals, faces=[],[],[]
    for k in range(1,bands):
        t=k/bands
        r=5.5*profile(t)/maximum
        derivative=(profile(t+1e-5)-profile(t-1e-5))*5.5/maximum/2e-5/24
        for j in range(sectors):
            a=2*math.pi*j/sectors
            positions.append([r*math.cos(a),-12+24*t,r*math.sin(a)])
            n=[math.cos(a),-derivative,math.sin(a)]
            length=math.sqrt(sum(x*x for x in n));normals.append([x/length for x in n])
    bottom,top=len(positions),len(positions)+1
    positions += [[0,-12,0],[0,12,0]];normals += [[0,-1,0],[0,1,0]]
    for j in range(sectors):
        nxt=(j+1)%sectors
        faces.append([bottom,j,nxt]);faces.append([top,(bands-2)*sectors+nxt,(bands-2)*sectors+j])
        for k in range(bands-2):
            a,b,c,d=k*sectors+j,k*sectors+nxt,(k+1)*sectors+nxt,(k+1)*sectors+j
            faces += [[a,c,b],[a,d,c]]
    # Winding must agree with the continuous surface normals.
    for f in faces:
        p,q,r=[positions[i] for i in f];u=[q[i]-p[i] for i in range(3)];v=[r[i]-p[i] for i in range(3)]
        n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        expected=[sum(normals[x][i] for x in f) for i in range(3)]
        if sum(n[i]*expected[i] for i in range(3))<0:f[1],f[2]=f[2],f[1]
    edges=Counter(tuple(sorted((f[i],f[(i+1)%3]))) for f in faces for i in range(3))
    if set(edges.values())!={2} or len(positions)-len(edges)+len(faces)!=2:raise ValueError('Not a closed manifold')
    return {'positions':positions,'normals':normals,'faces':faces}


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
    (out/'waterdrop.obj').write_text('# Native host geometry, no N64 vertex quantization\n'+'\n'.join(
        ['v '+' '.join(map(str,p)) for p in mesh['positions']]+['vn '+' '.join(map(str,n)) for n in mesh['normals']]+
        ['f '+' '.join(f'{i+1}//{i+1}' for i in f) for f in mesh['faces']])+'\n')
    manifest={'schema':'srw64.native-marker.v1','resource_id':5600,'vertices':len(mesh['positions']),'triangles':len(mesh['faces']),
              'material':'golden smooth drop; per-fragment studio lighting, specular highlights and Fresnel rim',
              'original_resource_sha256':hashlib.sha256(original).hexdigest(),
              'files':{name:digest(out/name) for name in ('reference.bin','vertices.bin','indices.bin','mesh.json','waterdrop.obj')}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(validate(out)))


if __name__=='__main__':main()
