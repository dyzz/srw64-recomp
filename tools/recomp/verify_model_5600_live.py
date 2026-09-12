#!/usr/bin/env python3
"""Verify the bounded 5600 ROM experiment using live CPU/GPU run evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageChops

from build_model_5600_rom import ROOT, ResourceTable, build_resource
from prepare_model_5600_highpoly import make_mesh
from watch_model_5600_runtime import inspect


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise RuntimeError(reason)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    build = json.loads((root/'rom/build.json').read_text())
    base_rom = ROOT/'rom.z64'
    rom = ROOT/build['variant']['path']
    require(sha(base_rom) == build['base_rom_sha256'], 'Base ROM drift')
    require(sha(rom) == build['variant']['sha256'], 'Experiment ROM drift')
    original, _ = ResourceTable(base_rom.read_bytes()).extract(5600)
    resource, _ = ResourceTable(rom.read_bytes()).extract(5600)
    expected, _ = build_resource(original)
    require(resource == expected == (root/'rom/resource-5600.bin').read_bytes(), 'ROM resource differs from authored mesh')
    mesh_path = ROOT/'build/analysis/model-5600-highpoly/mesh.json'
    require(json.loads(mesh_path.read_text()) == json.loads(json.dumps(make_mesh())), 'Viewer mesh differs from the ROM authoring mesh')
    reports = {name: json.loads((root/name/'report.json').read_text()) for name in ('live-1', 'original-1')}
    live, baseline = reports['live-1'], reports['original-1']
    for name, report in reports.items():
        require(report['exit_code'] == 0, f'{name}: failed process')
        require(report['initial_save'] is None and not report['interactive'], f'{name}: unexpected starting state')
        require(report['resolution_scale'] == 3 and report['font_pack'] is None, f'{name}: render configuration drift')
        require(report['counters']['vis'] >= 7073, f'{name}: missing comparison scene')
        require(report['input']['compiled_sha256'] == sha(Path(report['input']['compiled_path'])), f'{name}: input drift')
        require(report['native_log_sha256'] == sha(Path(report['native_log_path'])), f'{name}: log drift')
        require(report['rom_sha256'] == sha(rom if name == 'live-1' else base_rom), f'{name}: wrong ROM')
    for field in ('source_sha256', 'graphics_source_patches', 'runtime_lifecycle',
                  'generation_report_sha256', 'runtime_abi_header_sha256', 'variant_lock_sha256'):
        require(live[field] == baseline[field], f'Comparison source mismatch: {field}')
    require(live['input']['compiled_sha256'] == baseline['input']['compiled_sha256'], 'Different input scripts')
    require(live['counters']['vis'] == live['requested_vis'] == 16800, 'High-poly run did not reach its limit')
    require(live['status'] == 'native-graphics-frames-captured', 'High-poly run incomplete')
    require(live['code_compatibility']['first_mib_identical'] and
            len(live['code_compatibility']['full_loader_sections_identical']) == 18, 'Guest code changed')
    require(live['final_save']['path'] != baseline['final_save']['path'], 'Save paths not isolated')

    frames = []
    specs = [('original-1',3540,'实际游戏 · 原版 8 面'), ('live-1',3540,'实际游戏 · 高模 96 面'),
             ('live-1',4920,'实际游戏 · 地图移动与另一旋转角度'), ('live-1',8400,'实际游戏 · 已进入第一话战术地图')]
    for folder, present, title in specs:
        frame = next(f for f in reports[folder]['frames'] if f['metadata']['present'] == present)
        require(frame['sha256'] == sha(root/folder/frame['path']), 'GPU image drift')
        require(frame['metadata']['GPU_completion'] == 'completed', 'GPU frame not completed')
        frames.append({**frame, 'path': f'{folder}/{frame["path"]}', 'title': title})
    require(frames[0]['metadata']['native_vi_at_draw'] == frames[1]['metadata']['native_vi_at_draw'] == 7073,
            'Comparison VI mismatch')
    a, b = [Image.open(root/f['path']).convert('RGB') for f in frames[:2]]
    require(a.size == b.size == (960,720), 'Unexpected comparison dimensions')
    channels = ImageChops.difference(a,b).split()
    mask = ImageChops.lighter(ImageChops.lighter(channels[0],channels[1]),channels[2]).point(lambda x: 255 if x else 0)
    outside = mask.copy()
    checked_region = (455,312,507,372)
    outside.paste(0,checked_region)
    require(mask.histogram()[255] > 0 and outside.histogram()[255] == 0, 'Change escaped solid-marker region')

    observations = [json.loads(s) for s in (root/'live-1/model-5600-observations.jsonl').read_text().splitlines()]
    matrices, translations = set(), set()
    snapshots = 0
    for row in observations:
        if not row.get('snapshot'):
            continue
        folder = root/'live-1'/row['snapshot']
        ram, task = (folder/'latest-gfx-rdram.bin').read_bytes(), (folder/'latest-gfx-task.bin').read_bytes()
        require(sha(folder/'latest-gfx-rdram.bin') == row['rdram_sha256'] and
                sha(folder/'latest-gfx-task.bin') == row['task_sha256'], 'Saved task drift')
        result = inspect(ram,task,resource)
        require(result['root_calls'] == row['root_calls'] and result['root_calls'], 'Missing resource invocation')
        for call in result['root_calls']:
            data = bytes.fromhex(call['modelview']['bytes_hex'])
            values = tuple(struct.unpack_from('>h',data,i*2)[0] + struct.unpack_from('>H',data,32+i*2)[0]/65536 for i in range(16))
            matrices.add(tuple(values[i] for i in (0,1,2,4,5,6,8,9,10)))
            translations.add(values[12:15])
        snapshots += 1
    require(snapshots >= 2 and len(matrices) >= 2, 'No changing live rotation in saved tasks')
    final = root/'live-1'
    end = inspect((final/'latest-gfx-rdram.bin').read_bytes(), (final/'latest-gfx-task.bin').read_bytes(),resource)
    require(not end['resident'] and not end['root_calls'], 'Expected post-story scene transition missing')
    acceptance = {
        'schema':'srw64.model-5600-live-acceptance.v1', 'status':'verified-bounded-native-run',
        'scope':'Live recompiled game CPU, original resource loader, RT64/Metal GPU readback; opening female story through first tactical map. Not N64 hardware or full-route acceptance.',
        'rom_sha256':sha(rom), 'resource_sha256':hashlib.sha256(resource).hexdigest(), 'mesh_sha256':sha(mesh_path),
        'build_report_sha256':sha(root/'rom/build.json'),
        'runs':{name:{'report_sha256':sha(root/name/'report.json'), 'binary_sha256':r['binary_sha256'],
                      'status':r['status'], 'vis':r['counters']['vis'], 'exit_code':r['exit_code']} for name,r in reports.items()},
        'comparison_provenance':'Same recorded host sources, generated guest code, renderer sources and input. Separate rebuilds have different binary hashes. Original run ended at VI 11564 with SDL window-quit; used only for earlier completed frames.',
        'same_host_binary':live['binary_sha256'] == baseline['binary_sha256'],
        'frames':frames,
        'frame_comparison':{'native_vi':7073, 'changed_pixels':mask.histogram()[255], 'bbox_exclusive':mask.getbbox(),
                            'checked_solid_region':checked_region, 'changed_pixels_outside_region':outside.histogram()[255]},
        'saved_tasks_verified':snapshots, 'distinct_saved_rotation_matrices':len(matrices),
        'distinct_saved_translations':len(translations),
        'latest_task_after_scene_change':{'rdram_sha256':sha(final/'latest-gfx-rdram.bin'),
                                         'task_sha256':sha(final/'latest-gfx-task.bin'), **end},
        'observations_sha256':sha(final/'model-5600-observations.jsonl'),
        'observation_scope':'Read-only asynchronous task snapshots; observed_vi is not an exact GPU-frame synchronization claim.'}
    (root/'acceptance.json').write_text(json.dumps(acceptance,indent=2,ensure_ascii=False)+'\n')
    print(json.dumps({k:acceptance[k] for k in ('status','frame_comparison','saved_tasks_verified','distinct_saved_rotation_matrices','distinct_saved_translations')},ensure_ascii=False))


if __name__ == '__main__':
    main()
