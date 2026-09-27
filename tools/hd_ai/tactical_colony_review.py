"""Archive the colony runtime smoke evidence and build a static comparison page."""
import argparse
import json
import shutil
from pathlib import Path
from PIL import Image
from tools.hd_ai.tactical_kit_compose import sha


def build(runtime, run):
    evidence=runtime/'evidence-map087';evidence.mkdir(exist_ok=True)
    for pattern in ('*colony*.png','colony-captures.json','hd-colony-events.jsonl','hd-map-summary.json','image-mode-events.jsonl'):
        for path in run.glob(pattern):
            shutil.copy2(path,evidence/path.name)
    captures=json.loads((evidence/'colony-captures.json').read_text())
    phases={}
    for mode in ('hd','original'):
        phases[mode]=[]
        for f in range(8):
            name=f'{mode}-colony-frame-{f}.png'
            rows=[r for r in captures if r['file']==name and r['observed_before']==r['observed_after']]
            if not rows or rows[-1]['observed_before']['frame']!=f:
                raise ValueError(f'Missing stable phase capture {name}')
            phases[mode].append(f)
    events=[json.loads(x) for x in (evidence/'hd-colony-events.jsonl').read_text().splitlines()]
    # Mode transitions can occur partway through a frame, so exclude those pairs.
    periods=[b['vi']-a['vi'] for a,b in zip(events,events[1:])
             if a['hd']==b['hd'] and a['countdown']==b['countdown']==26 and
             b['frame']==(a['frame']+1)%8 and a['camera']==b['camera']]
    inventory=[];thumbs=[]
    for path in sorted(runtime.glob('map-*/meta.json')):
        meta=json.loads(path.read_text());instances=meta.get('colony_instances',[])
        if not instances: continue
        for name,digest in meta['files'].items():
            if sha(path.parent/name)!=digest: raise ValueError(f'Changed asset {path.parent/name}')
        index=Image.open(path.with_name('index.png'));mask=Image.open(path.with_name('protected.png'))
        for x,y in instances:
            box=(x*4,y*4,(x+64)*4,(y+48)*4)
            if index.crop(box).getextrema()!=(meta['colony_background_index'],)*2 or mask.crop(box).getbbox():
                raise ValueError('Static colony indices or mask remain')
            thumbs.append(f'<figure><figcaption>map-{meta["map"]:03d} · ({x}, {y})</figcaption><div class="crop"><img alt="卫星区域静态星空" src="{path.parent.name}/base.png" style="width:{meta["width"]*2}px;left:{-(x-16)*2}px;top:{-(y-16)*2}px"></div></figure>')
        inventory.append({'map':meta['map'],'instances':instances,'meta_sha256':sha(path)})
    result={'schema':'srw64.colony-runtime-evidence.v1','map':87,'run':str(run.resolve()),
        'native_host_smoke_verified':True,'all_maps_runtime_verified':False,'complete_acceptance_proven':False,
        'captured_phases':phases,'observed_frame_period_vis':sorted(set(periods)),
        'period_samples':len(periods),'source_frame_ticks':27,
        'runtime_maps':inventory,'runtime_instances':sum(len(r['instances']) for r in inventory),
        'files':{p.name:sha(p) for p in evidence.iterdir() if p.is_file() and p.name!='evidence.json'},
        'scope':['map-087 HD/original eight-phase captures','map-087 horizontal scroll, left edge clip and mode switch',
                 'static colony index/mask clearing verified for every exported instance'],
        'limitations':['Other maps have not been exercised in the native host.',
                       'Sparse geometry audit is not exhaustive visual acceptance.',
                       'Frame events capture the game counter; screenshots do not prove exact GPU scheduling at a phase boundary.',
                       'Observed VI intervals are reported separately. Constant wall-clock cadence and performance are not certified.']}
    (evidence/'evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>动态卫星 · 游戏对照</title>
<style>body{margin:24px;background:#111820;color:#edf3fa;font:16px system-ui}a{color:#a5d7ff}p{line-height:1.7}button,select{padding:10px;margin:6px;background:#263d50;color:white;border:1px solid #678}figure{margin:0}figcaption{padding:8px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}.pair img{width:100%}.grid{display:grid;grid-template-columns:repeat(auto-fit,192px);gap:14px}.crop{position:relative;width:192px;height:160px;overflow:hidden;background:black}.crop img{position:absolute;max-width:none}summary{cursor:pointer;padding:15px;background:#263d50}@media(max-width:650px){.pair{grid-template-columns:1fr}}</style>
<h1>动态卫星 · 游戏对照</h1><p>map-087 已在游戏程序中抓到高清与原版的全部 8 帧，完成横向卷动、左边缘裁切和高清 / 原版切换。动画读取原游戏帧号，每 27 次游戏更新切帧。本次截图运行的 VI 间隔见证据文件，尚不能证明恒定帧率。其他地图尚未逐张运行验收。</p>
<label>帧号 <select id="phase">'''+''.join(f'<option>{i}</option>' for i in range(8))+'''</select></label><button id="next">下一帧</button>
<div class="pair"><figure><img id="original" alt="原版卫星同帧截图"><figcaption>原版 · 同一帧号</figcaption></figure><figure><img id="hd" alt="高清卫星同帧截图"><figcaption>高清 · 独立透明动画层</figcaption></figure></div>
<p><a href="evidence-map087/hd-colony-scroll-3.png">卫星移出画面时的裁切</a> · <a href="evidence-map087/evidence.json">运行证据与文件哈希</a> · <a href="../preview-v1/review.html">全部地图合成预览</a></p>
<details open><summary>静态底图的卫星区域 · __COUNT__ 个实例</summary><p>以下只显示底图：卫星的位置留作星空，运行时再叠加动画。每块额外显示 16 源像素边缘供检查接缝。</p><div class="grid">__THUMBS__</div></details>
<script>const phase=document.getElementById('phase');function show(){for(const mode of ['original','hd'])document.getElementById(mode).src=`evidence-map087/${mode}-colony-frame-${phase.value}.png`;}phase.onchange=show;document.getElementById('next').onclick=()=>{phase.value=(Number(phase.value)+1)%8;show()};show();</script></html>'''
    (runtime/'review.html').write_text(page.replace('__COUNT__',str(result['runtime_instances'])).replace('__THUMBS__',''.join(thumbs)))
    return {'maps':len(inventory),'instances':result['runtime_instances'],'period_vis':result['observed_frame_period_vis']}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--run',type=Path,required=True)
    a=p.parse_args();print(json.dumps(build(a.runtime,a.run)))
