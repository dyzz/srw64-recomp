#!/usr/bin/env python3
"""Verify the first native marker's bounded live run and controlled GPU replays."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.model5600.prepare_native_marker import ROOT, digest, validate


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT/'build/recomp/native-marker')
    args = parser.parse_args()
    folder = args.directory.resolve()
    pack = validate(folder/'assets')
    evidence = {}

    def read(relative):
        evidence[relative] = digest(folder/relative)
        return json.loads((folder/relative).read_text())

    runs = {name: read(name+'/report.json') for name in
        ('replay-3','replay-original-2','replay-rejected-1','replay-occluded-1','replay-visible-control-1','live-1')}
    for name,run in runs.items():
        require(run['exit_code']==0 and run['frames'], 'Incomplete GPU run: '+name)
        for frame in run['frames']:
            require(digest(folder/name/frame['path'])==frame['sha256'], 'GPU frame drift')
            require(frame['metadata']['GPU_completion']=='completed','GPU not completed')
    replay = runs['replay-original-2']
    for name in ('replay-3','replay-rejected-1','replay-occluded-1','replay-visible-control-1'):
        require(runs[name]['binary_sha256']==replay['binary_sha256'],'Paired replay binaries differ')
        if name!='replay-rejected-1':
            require(runs[name]['input_sha256']==replay['input_sha256'],'Paired replay tasks differ')
        summary = read(name+'/native-model-summary.json')
        require(summary['native_draws']==(0 if name=='replay-rejected-1' else 180),'Unexpected native draw count')
        require(summary['suppressed_triangles']==summary['native_draws']*7,'Original body draw leaked')

    def image(name):
        return Image.open(folder/name/'present-60.png').convert('RGB')

    baseline = image('replay-original-2')
    diff = ImageChops.difference(baseline,image('replay-3'))
    roi = (450,308,510,374)
    outside = diff.copy(); outside.paste((0,0,0),roi)
    changed = sum(any(p) for p in diff.get_flattened_data())
    require(changed>500 and outside.getbbox() is None,'Native pixels escaped marker bounds')
    require(ImageChops.difference(baseline,image('replay-rejected-1')).getbbox() is None,'Identity rejection changed output')
    hidden = ImageChops.difference(baseline,image('replay-occluded-1'))
    hidden.paste((0,0,0),roi)
    require(hidden.getbbox() is None,'Native mesh drew over the opaque portrait')
    visible = ImageChops.difference(baseline,image('replay-visible-control-1'))
    require(visible.crop((800,300,875,378)).getbbox() is not None,'Occlusion positive control never drew')
    live = runs['live-1']
    require(live['rom_variant']=='jp' and live['rom_sha256']==digest(ROOT/'rom.z64'),'Live ROM differs from original')
    require(live['status']=='native-graphics-frames-captured' and live['counters']['vis']==16800,'Live run incomplete')
    require(live['initial_save'] is None and live['native_marker']['manifest_sha256']==pack['manifest_sha256'],'Wrong live setup')
    summary = read('live-1/native-model-summary.json')
    require(summary['native_draws']>1000 and summary['suppressed_triangles']==summary['native_draws']*7,'Live hook incomplete')
    log = folder/'live-1/native-model-draws.jsonl'
    evidence[str(log.relative_to(folder))] = digest(log)
    draws = [json.loads(x) for x in log.read_text().splitlines()]
    require(len({tuple(x['world']) for x in draws})>10,'No changing scene transforms observed')
    require(all(x['shared_scene_depth'] and x['depth_compare'] and x['depth_write'] for x in draws),'Missing scene depth')
    for relative, expected in live['source_sha256'].items():
        require(digest(ROOT/relative)==expected,'Host source changed after live run: '+relative)

    frames=[]
    for relative,title in [
        ('replay-original-2/present-60.png','原版 · 同任务 GPU 对照'),
        ('replay-3/present-60.png','原生水滴 · 同任务 GPU 对照'),
        ('live-1/present-3540.png','实际游戏 · 欧洲大陆剧情'),
        ('live-1/present-4920.png','实际游戏 · 撒丁岛剧情'),
        ('live-1/present-8400.png','实际游戏 · 已进入战术地图'),
        ('replay-occluded-1/present-60.png','受控回放 · 水滴位于头像后方')]:
        frames.append({'path':relative,'title':title,'sha256':digest(folder/relative)})
    accepted={'schema':'srw64.native-marker-acceptance.v1','status':'verified-bounded-native-run',
        'asset_manifest_sha256':pack['manifest_sha256'],'evidence_files':evidence,'frames':frames,
        'paired_replay':{'same_binary':True,'same_task':True,'changed_pixels':changed,'bbox':diff.getbbox(),
            'outside_marker_roi_changes':0,'identity_mismatch_changes':0,'portrait_occlusion':'verified with visible positive control'},
        'live':{'vis':live['counters']['vis'],'elapsed_seconds':live['elapsed_seconds'],
            'binary_sha256':live['binary_sha256'],'native_draws':summary['native_draws'],
            'distinct_sampled_transforms':len({tuple(x['world']) for x in draws}),'rom_modified':False},
        'summary':'原版 ROM + 现代浮点网格、连续法线与 Metal 逐像素光照。已从空存档运行女主开场至第一话战术地图（16,800 VI，约 4 分 40 秒）。前两图是同一快照、同一程序的开关对照，差异仅在标记本体周围；其余游戏图来自实际运行。另有受控回放验证头像能遮住水滴。当前覆盖 macOS Metal 开场，材质为不透明金色。',
        'limits':['Opening story and first tactical map only; not full-route acceptance.',
                  'Metal prototype; no Vulkan or D3D12 native mesh backend.',
                  'Opaque studio-lit material; no refraction, dynamic shadows or reflection environment.']}
    (folder/'acceptance.json').write_text(json.dumps(accepted,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'status':accepted['status'],'changed_pixels':changed,'live':accepted['live']},ensure_ascii=False))


if __name__=='__main__':
    main()
