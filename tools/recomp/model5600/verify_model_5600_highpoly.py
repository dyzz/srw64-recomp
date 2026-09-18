#!/usr/bin/env python3
"""Verify the high-poly 5600 isolated replay and record bounded pixel evidence."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.model5600.prepare_model_5600_highpoly import SOURCE_HASH, TASK_HASH, ARENA_ADDRESS, MODEL_ADDRESS, SOLID_COMMANDS


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    fixture = json.loads((root/'fixtures.json').read_text())
    if digest(root/'mesh.json') != fixture['mesh_sha256'] or digest(root/'arena.bin') != fixture['arena_sha256']:
        raise RuntimeError('Authored geometry drifted after fixture generation')
    original = (root/'baseline-source/latest-gfx-rdram.bin').read_bytes()
    changed = (root/'highpoly-source/latest-gfx-rdram.bin').read_bytes()
    if hashlib.sha256(original).hexdigest() != SOURCE_HASH or len(changed) != len(original):
        raise RuntimeError('Baseline or snapshot size mismatch')
    arena_end = ARENA_ADDRESS+fixture['arena_bytes']
    expected_highpoly = next(f['rdram_sha256'] for f in fixture['fixtures'] if f['name'] == 'highpoly')
    if hashlib.sha256(changed).hexdigest() != expected_highpoly or changed[ARENA_ADDRESS:arena_end] != (root/'arena.bin').read_bytes():
        raise RuntimeError('Replay snapshot does not contain the generated replacement')
    for patch in fixture['patches']:
        offset = patch['offset']
        if original[offset:offset+8].hex() != patch['before'] or changed[offset:offset+8].hex() != patch['after']:
            raise RuntimeError('Marker command patch mismatch')
    allowed = set(range(ARENA_ADDRESS, arena_end)) | {MODEL_ADDRESS+offset+i for offset in SOLID_COMMANDS for i in range(8)}
    if any(a != b and i not in allowed for i, (a, b) in enumerate(zip(original, changed))):
        raise RuntimeError('Memory changed outside the marker patch and replay-only arena')
    # Includes the original plane vertices, texture bytes, culling bounds,
    # texture loaders, and all eight original ring triangle draws.
    if changed[MODEL_ADDRESS:MODEL_ADDRESS+0x1A90] != original[MODEL_ADDRESS:MODEL_ADDRESS+0x1A90]:
        raise RuntimeError('Original ring changed')
    reports = []
    images = []
    for label in ('baseline', 'highpoly'):
        report = json.loads((root/label/'report.json').read_text())
        if report['status'] != 'GPU-frame-captured' or report['exit_code'] != 0 or report['resolution_scale'] != 3:
            raise RuntimeError('Replay did not complete at the expected resolution')
        for filename, expected in report['input_sha256'].items():
            if digest(root/(label+'-source')/filename) != expected:
                raise RuntimeError('Replay input hash mismatch')
        if digest(root/(label+'-source')/'latest-gfx-task.bin') != TASK_HASH:
            raise RuntimeError('Task descriptor changed')
        frame = root/label/'present-60.png'
        meta = json.loads(frame.with_suffix('.json').read_text())
        if meta['GPU_completion'] != 'completed' or (meta['width'], meta['height']) != (960, 720):
            raise RuntimeError('Expected a completed GPU frame at 960x720')
        record = next(f for f in report['frames'] if f['path'] == frame.name)
        if record['sha256'] != digest(frame):
            raise RuntimeError('GPU frame hash drift')
        reports.append(report)
        images.append(Image.open(frame).convert('RGB'))
    if reports[0]['binary_sha256'] != reports[1]['binary_sha256']:
        raise RuntimeError('Baseline and high-poly used different renderer binaries')
    delta = ImageChops.difference(*images)
    changed_pixels = sum(pixel != (0, 0, 0) for pixel in delta.get_flattened_data())
    bbox = delta.getbbox()
    # Expected solid screen region, inside the larger ring. Coordinates are
    # specific to the locked 960x720 captured task, not a game-wide contract.
    region = (455, 312, 507, 372)
    outside = delta.copy()
    outside.paste((0, 0, 0), region)
    if not changed_pixels or outside.getbbox() is not None:
        raise RuntimeError(f'Unexpected frame difference: {bbox}, pixels={changed_pixels}')
    acceptance = {'schema': 'srw64.model-5600-highpoly-acceptance.v1', 'status': 'verified',
                  'scope': 'single captured graphics task; no live-game installation or full-playthrough claim',
                  'solid_triangles_before': 8, 'solid_triangles_after': fixture['replacement_solid_triangles'],
                  'mesh_sha256': fixture['mesh_sha256'], 'arena_sha256': fixture['arena_sha256'],
                  'binary_sha256': reports[0]['binary_sha256'], 'source_sha256': SOURCE_HASH,
                  'ring_bytes_unchanged': True, 'changed_pixels': changed_pixels,
                  'difference_bbox_exclusive': bbox, 'solid_check_region': region,
                  'pixels_changed_outside_solid_region': 0,
                  'frames': {label: {'path': f'{label}/present-60.png', 'sha256': digest(root/label/'present-60.png')}
                             for label in ('baseline', 'highpoly')}}
    (root/'acceptance.json').write_text(json.dumps(acceptance, indent=2)+'\n')
    print(json.dumps(acceptance))


if __name__ == '__main__':
    main()
