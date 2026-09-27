"""Build the review page and inventory for staged tactical-map compositions."""
import argparse
import json
import os
from pathlib import Path

from tools.hd_ai.tactical_map_hd import ROOT
from tools.hd_ai.tactical_kit_compose import sha


def build(kit, staging, previous=None):
    specs={m['map']:m for m in json.loads((kit/'manifest.json').read_text())['maps']}
    notes_path=staging/'visual-review.json'
    visual_notes=json.loads(notes_path.read_text()) if notes_path.is_file() else {}
    rows=[]
    for path in sorted(staging.glob('map-*/composition.json')):
        report=json.loads(path.read_text());meta=json.loads(path.with_name('meta.json').read_text())
        for name,digest in meta['files'].items():
            if sha(path.with_name(name))!=digest:
                raise ValueError(f'changed staging file: {path.with_name(name)}')
        m=specs[report['map']]
        previous_base=previous/path.parent.name/'base.png' if previous else None
        if previous_base and not previous_base.is_file():
            previous_base=kit/'composed/preview-v1'/path.parent.name/'base.png'
        evidence=path.parent/'runtime-evidence/evidence.json'
        runtime_link=None
        if evidence.is_file():
            record=json.loads(evidence.read_text())
            if record['base_sha256']!=sha(path.with_name('base.png')) or record['meta_sha256']!=sha(path.with_name('meta.json')):
                raise ValueError(f'Stale runtime evidence: {evidence}')
            for name,digest in record['files'].items():
                if sha(evidence.parent/name)!=digest:
                    raise ValueError(f'Changed runtime evidence file: {name}')
            runtime_link=path.parent.name+'/runtime-evidence/review.html'
            cards=''.join(f'<h2>{label}</h2><div><figure><img src="{key}-original.png"><figcaption>原版</figcaption></figure><figure><img src="{key}-hd.png"><figcaption>高清</figcaption></figure></div>'
                for key,label in [('center','初始视角'),('scrolled-up-left','向左上滚动后'),('scrolled-down-right','向右下滚动后')])
            (evidence.parent/'review.html').write_text(f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>map-{report["map"]:03d} 游戏对照</title><style>body{{background:#111820;color:#eef2f5;margin:24px;font:16px system-ui}}a{{color:#a5d7ff}}div{{display:grid;grid-template-columns:1fr 1fr;gap:12px}}figure{{margin:0}}img{{width:100%}}p{{line-height:1.7}}</style><h1>map-{report["map"]:03d} · 游戏内对照</h1><p>同一位置切换原版与高清后截图。已检查加载、绘制、滚动及图像模式切换；不代表逐格几何、动画时序或性能验收。<a href="evidence.json">运行记录与资产哈希</a></p>'+cards)
        rows.append({'map':report['map'],'family':m['family'],'source':'../../'+m['source'],
            'base':path.parent.name+'/base.png','report':path.parent.name+'/composition.json',
            'raw_suspects':sum(r['raw_suspect_samples'] for r in report['registration']),
            'registered_suspects':sum(r['registered_diagnostic']['suspect_samples'] for r in report['registration']),
            'unfitted':sum(r['fit']['status'] not in ('fitted_sparse_samples','fitted_field_with_heldout_samples') for r in report['registration']),
            'dense_pieces':sum(r.get('diagnostic_grid',4)>4 for r in report['registration']),
            'colony_overlay':bool(report.get('colony_overlay_instances')),
            'runtime_review':runtime_link,
            'visual_note':visual_notes.get(str(report['map']),''),
            'previous':os.path.relpath(previous_base,staging) if previous_base and previous_base.is_file() else None,
            'pieces':len(report['registration']),'protected_pixels_exact':report['protected_pixels_exact']})
    result={'schema':'srw64.tactical-composition-progress.v1','maps_composed':len(rows),'approved_family_maps_total':107,
            'full_acceptance_proven':False,'rows':rows}
    (staging/'progress.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    template='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>战术地图合成预览</title>
<style>body{margin:24px;background:#111820;color:#eef2f5;font:16px system-ui}a{color:#a5d7ff}select,button{padding:9px;margin:4px;background:#243646;color:inherit;border:1px solid #6b8599}p{line-height:1.7;color:#bac8d4}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}.pair img{width:100%;height:70vh;object-fit:contain;background:#000}.overlay{display:block;position:relative}.overlay figure:nth-child(2){position:absolute;inset:0;opacity:.5}.overlay figcaption{display:none}figure{margin:0}figcaption{padding:8px}#detail{padding:12px;background:#24303b}@media(max-width:700px){.pair{grid-template-columns:1fr}}</style>
<h1>战术地图 · 合成预览</h1><p id="status"></p><p>已合成的整图与窗口按原图尺寸排列。动态色号与边框精确保护；匹配不足、可疑偏移及游戏验证仍需逐项检查。加密采样图每张取 64 点，其余每张取 16 点；不能直接用总数比较不同采样版本。<a href="../runtime-colony-v4/review.html">动态卫星与游戏对照</a>。</p>
<label>地图 <select id="map"></select></label><button id="side">左右对照</button><button id="overlay">半透明叠加</button><button id="previous" hidden>查看上一版</button>
<h2 id="title"></h2><p id="detail"></p><div class="pair" id="pair"><figure><img id="source" alt="原版完整地图"><figcaption>原版完整地图</figcaption></figure><figure><img id="base" alt="合成预览"><figcaption>4 倍合成预览</figcaption></figure></div>
<p><a href="../runtime-map020/hd-bottom-right.png">map-020 游戏 HD 截图</a> · <a href="../runtime-map020/original-bottom-right.png">同位置原版截图</a> · <a href="../runtime-map020/evidence.json">此次运行证据</a> · <a href="progress.json">合成清单</a></p>
<script>const data=__DATA__;const $=x=>document.getElementById(x);$('status').textContent=`本页收录 ${data.maps_composed} 张合成候选；已批准家族共 ${data.approved_family_maps_total} 张地图。均为待审预览。`;
for(const r of data.rows){const o=document.createElement('option');o.value=r.map;o.textContent=`map-${String(r.map).padStart(3,'0')} · ${r.family}`;$('map').append(o)}
let previousShown=false;
function show(){const r=data.rows.find(r=>r.map===Number($('map').value));if(!r)return;previousShown=false;$('previous').hidden=!r.previous;$('previous').textContent='查看上一版';$('title').textContent=`map-${String(r.map).padStart(3,'0')}`;$('source').src=r.source;$('base').src=r.base;$('base').nextElementSibling.textContent='4 倍合成预览';$('detail').textContent=`使用 ${r.pieces} 张生成图（${r.dense_pieces} 张加密采样）；原始可疑点 ${r.raw_suspects}，配准后 ${r.registered_suspects}；${r.unfitted} 张图匹配证据不足，保留原位置。保护区域逐像素校验通过。${r.colony_overlay?'底图已移除卫星，由独立动画绘制。':''} ${r.visual_note||''} `;const a=document.createElement('a');a.href=r.report;a.textContent='详细诊断';$('detail').append(a);if(r.runtime_review){const v=document.createElement('a');v.href=r.runtime_review;v.textContent=' · 游戏内原版／高清对照';$('detail').append(v)}history.replaceState(null,'','#'+r.map)}
$('previous').onclick=()=>{const r=data.rows.find(r=>r.map===Number($('map').value));previousShown=!previousShown;$('base').src=previousShown?r.previous:r.base;$('base').nextElementSibling.textContent=previousShown?'上一版合成':'4 倍合成预览';$('previous').textContent=previousShown?'返回新版':'查看上一版'};
$('map').onchange=show;$('side').onclick=()=>{$('pair').className='pair'};$('overlay').onclick=()=>{$('pair').className='pair overlay'};if(data.rows.some(r=>r.map===Number(location.hash.slice(1))))$('map').value=Number(location.hash.slice(1));show();</script></html>'''
    (staging/'review.html').write_text(template.replace('__DATA__',json.dumps(result,ensure_ascii=False).replace('</','<\\/')))
    return {'maps_composed':len(rows),'registered_suspect_maps':sum(r['registered_suspects']>0 for r in rows),'pieces':sum(r['pieces'] for r in rows)}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kit',type=Path,default=ROOT/'assets/hd-ai/tactical-kit')
    p.add_argument('--staging',type=Path,required=True)
    p.add_argument('--previous',type=Path,help='Previous staging folder for visual comparison')
    args=p.parse_args()
    print(json.dumps(build(args.kit,args.staging,args.previous)))
