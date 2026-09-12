"""Attach the opt-in native mesh and verified GPU evidence to the local viewer."""
import hashlib
import json
import shutil
import sys


def add_native_marker(root, out, resources):
    folder = root / 'build/recomp/native-marker'
    if not (folder/'assets/manifest.json').exists():
        return None
    sys.path.insert(0, str(root/'tools/recomp'))
    from prepare_native_marker import validate
    pack = validate(folder/'assets')
    mesh = json.loads((folder/'assets/mesh.json').read_text())
    original = json.loads((out/'models/5600.json').read_text())
    parts = original['batches'][:1] + [{
        'part': 0, 'positions': [v for f in mesh['faces'] for i in f for v in mesh['positions'][i]],
        'normals': [v for f in mesh['faces'] for i in f for v in mesh['normals'][i]],
        'uvs': [0] * len(mesh['faces']) * 6, 'color': [255, 170, 25, 255],
        'texture': None, 'wrap_s': 2, 'wrap_t': 2, 'shading': 'native-gold'}]
    geometry = {'schema': 'srw64.model-viewer-geometry.v1', 'id': 5600,
                'variant': 'native', 'batches': parts, 'part_count': 1}
    (out/'models/5600-native.json').write_text(json.dumps(geometry, separators=(',', ':'))+'\n')
    resource = next(r for r in resources if r['id'] == 5600)
    resource['original_variant_label'] = '原版 · 8 面'
    overrides = {'title': '剧情地图标记 · 原生水滴', 'status': 'prototype',
        'triangles': pack['triangles']+8, 'vertices': pack['vertices']+4, 'texture_count': 1,
        'note': '现代 GPU 绘制的圆润水滴：浮点网格、连续法线与逐像素光照。保留原游戏的位置、旋转、相机和虚线环。',
        'material_note': '网页使用简化金色材质展示曲面；实际游戏使用独立 Metal shader，高光以游戏效果对照为准。',
        'source_note': '原生资源包 · SHA-256 '+pack['manifest_sha256']}
    resource.setdefault('variants', []).append({'key': 'native', 'label': '原生水滴 · 3,968 面',
        'file': 'models/5600-native.json', 'overrides': overrides})
    if not (folder/'acceptance.json').exists():
        return None
    accepted = json.loads((folder/'acceptance.json').read_text())
    if accepted['status'] != 'verified-bounded-native-run' or accepted['asset_manifest_sha256'] != pack['manifest_sha256']:
        raise RuntimeError('Native marker evidence or mesh drift')
    for relative, expected in accepted['evidence_files'].items():
        path = folder/relative
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError('Native marker evidence file drift: '+relative)
    frames = []
    for i, frame in enumerate(accepted['frames']):
        path = folder/frame['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != frame['sha256']:
            raise RuntimeError('Native marker frame drift')
        destination = f'evidence/5600-native-{i}.png'
        shutil.copy2(path, out/destination)
        frames.append({**frame, 'path': destination})
    shutil.copy2(folder/'acceptance.json', out/'evidence/5600-native-acceptance.json')
    overrides['status'] = 'live'
    overrides['note'] += ' 已从空存档运行女主开场至第一话战术地图。'
    return {'frames': frames, 'summary': accepted['summary']}
