#!/usr/bin/env python3
"""Move only host geometry behind an opaque portrait for a task-replay test."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.model5600.prepare_native_marker import digest, validate


def solve(matrix, values):
    rows = [list(row)+[value] for row, value in zip(matrix, values)]
    for k in range(4):
        pivot = max(range(k,4), key=lambda i: abs(rows[i][k]))
        rows[k], rows[pivot] = rows[pivot], rows[k]
        divisor = rows[k][k]
        if abs(divisor) < 1e-10:
            raise ValueError('Singular projection')
        rows[k] = [x/divisor for x in rows[k]]
        for i in range(4):
            if i != k:
                multiplier = rows[i][k]
                rows[i] = [a-multiplier*b for a,b in zip(rows[i],rows[k])]
    return [row[4] for row in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--draw-log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--target-x', type=float, default=278)
    parser.add_argument('--target-y', type=float, default=172)
    args = parser.parse_args()
    validate(args.assets)
    # The first implementation's 'world' diagnostic contains world * viewProj.
    flat = json.loads(args.draw_log.read_text().splitlines()[0])['world']
    matrix = [flat[i:i+4] for i in range(0,16,4)]
    clip = matrix[3].copy()
    clip[0] = (args.target_x-160)/160*clip[3]
    clip[1] = -(args.target_y-120)/120*clip[3]
    position = solve(list(zip(*matrix)),clip)
    delta = [x/position[3] for x in position[:3]]
    shutil.copytree(args.assets,args.output)
    mesh = json.loads((args.assets/'mesh.json').read_text())
    mesh['positions'] = [[x+d for x,d in zip(p,delta)] for p in mesh['positions']]
    (args.output/'mesh.json').write_text(json.dumps(mesh,separators=(',',':'))+'\n')
    (args.output/'vertices.bin').write_bytes(b''.join(struct.pack('<6f',*p,*n) for p,n in zip(mesh['positions'],mesh['normals'])))
    (args.output/'waterdrop.obj').write_text('# Controlled occlusion test geometry\n'+'\n'.join(
        ['v '+' '.join(map(str,p)) for p in mesh['positions']] +
        ['f '+' '.join(str(i+1) for i in f) for f in mesh['faces']])+'\n')
    manifest = json.loads((args.assets/'manifest.json').read_text())
    manifest['test_only'] = {'target_screen':[args.target_x,args.target_y], 'translation_local':delta,
        'source_draw_log_sha256':digest(args.draw_log), 'purpose':'portrait occlusion in captured-task replay'}
    manifest['files'] = {name:digest(args.output/name) for name in manifest['files']}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(validate(args.output)))


if __name__ == '__main__':
    main()
