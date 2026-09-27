"""Build a hash-checked review snapshot for the integrated tactical map bundle."""
import argparse
import html
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from tools.hd_ai.tactical_kit_compose import sha


def build(kit, bundle):
    package = json.loads((bundle / 'bundle.json').read_text())
    specs = {m['map']: m for m in json.loads((kit / 'manifest.json').read_text())['maps']}
    notes_file = bundle / 'visual-review.json'
    notes = json.loads(notes_file.read_text()).get('maps', {}) if notes_file.exists() else {}
    rows = []
    for item in package['rows']:
        mid = item['map']
        directory = bundle / f'map-{mid:03d}'
        for name, digest in item['files'].items():
            if sha(directory / name) != digest:
                raise ValueError(f'Bundle asset changed: {directory / name}')
        source = kit / specs[mid]['source']
        note = notes.get(str(mid), {})
        if note and (note['source_sha256'] != sha(source) or note['base_sha256'] != item['files']['base.png']):
            raise ValueError(f'Stale visual review: {mid}')
        evidence_file = directory / 'runtime-evidence/evidence.json'
        runtime = False
        if evidence_file.exists():
            try:
                evidence = json.loads(evidence_file.read_text())
            except json.JSONDecodeError:  # The active capture queue may be writing it.
                evidence = None
            if evidence:
                for name in ('base', 'index', 'meta'):
                    suffix = 'json' if name == 'meta' else 'png'
                    if evidence[f'{name}_sha256'] != item['files'][f'{name}.{suffix}']:
                        raise ValueError(f'Stale runtime evidence: {mid} {name}')
                for name, digest in evidence['files'].items():
                    if sha(evidence_file.parent / name) != digest:
                        raise ValueError(f'Changed capture evidence: {mid} {name}')
                runtime = evidence['native_host_smoke_verified']
        rows.append({'map': mid, 'family': item['family'],
                     'source': os.path.relpath(source, bundle),
                     'base': f'map-{mid:03d}/base.png', 'runtime': runtime,
                     'colony_instances': item['colony_instances'], 'note': note})
    snapshot = {'generated_at': datetime.now(timezone.utc).isoformat(), 'rows': rows,
                'runtime_checked': sum(r['runtime'] for r in rows),
                'overview_reviewed': sum(bool(r['note']) for r in rows),
                'full_acceptance_proven': False}
    (bundle / 'review-progress.json').write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + '\n')
    options = ''.join(f'<option value="{r["map"]}">map-{r["map"]:03d} · {html.escape(r["family"])} · {"运行已检查" if r["runtime"] else "运行待检查"}</option>' for r in rows)
    template = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>全量地图运行对照</title>
<style>body{margin:24px;background:#14212b;color:#e7eef4;font:16px system-ui}a{color:#a4d4ff}p{line-height:1.7}select,button{padding:10px;background:#314653;color:inherit;border:1px solid #789}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%;background:#000}figcaption{padding:8px}.overview img{height:65vh;object-fit:contain}@media(max-width:700px){.pair{grid-template-columns:1fr}}</style>
<h1>全量地图 · 原版与高清对照</h1><p>__COUNT__ 张地图中，__RUNTIME__ 张完成加载、滚动及原版／高清切换检查，__OVERVIEW__ 张有整图目视记录。此页是生成时的进度快照；运行检查通过不等于完整美术验收。荒漠、云下仍等待样图确认。</p>
<p><a href="../colony-alpha-v4/review.html">卫星 8 帧同步对照</a> · <a href="review-progress.json">核对记录</a> · <a href="runtime-queue.log">实时运行日志</a></p><label>地图 <select id="map">__OPTIONS__</select></label><button id="next">下一张</button><p id="note"></p><p><a id="repair" hidden>查看这张地图的返修候选</a></p><div class="pair overview"><figure><img id="source" alt="原版整图"><figcaption>原版整图</figcaption></figure><figure><img id="base" alt="高清静态底图"><figcaption>高清静态底图；卫星另行绘制</figcaption></figure></div><div id="runtime"></div>
<script>const data=__DATA__, $=id=>document.getElementById(id);function show(){const r=data.rows.find(r=>r.map===Number($('map').value));if(!r)return;const prefix=`map-${String(r.map).padStart(3,'0')}/runtime-evidence/`;$('source').src=r.source;$('base').src=r.base;$('note').textContent=(r.note.overview_note||'整图目视记录待补。')+(r.colony_instances?` 本图 ${r.colony_instances} 个卫星已从静态底图移除；游戏截图显示独立动画。`:'')+(r.note.native_note?' 游戏截图复核：'+r.note.native_note:'');$('repair').hidden=!r.note.repair_candidate;if(r.note.repair_candidate)$('repair').href=r.note.repair_candidate;$('runtime').replaceChildren();if(r.runtime){for(const [key,title] of [['center','初始视角'],['scrolled-up-left','向左上滚动后'],['scrolled-down-right','向右下滚动后']]){const h=document.createElement('h2');h.textContent=title;const pair=document.createElement('div');pair.className='pair';for(const mode of ['original','hd']){const f=document.createElement('figure'),im=new Image,c=document.createElement('figcaption');im.src=prefix+key+'-'+mode+'.png';im.alt=title+' '+mode;im.loading='lazy';c.textContent=mode==='original'?'原版':'高清';f.append(im,c);pair.append(f)}$('runtime').append(h,pair)}}else{$('runtime').textContent='本快照尚未收录这张地图的完整运行截图。'}history.replaceState(null,'','#'+r.map)}$('map').onchange=show;$('next').onclick=()=>{$('map').selectedIndex=($('map').selectedIndex+1)%data.rows.length;show()};if(data.rows.some(r=>r.map===Number(location.hash.slice(1))))$('map').value=Number(location.hash.slice(1));show();</script></html>'''
    for key, value in {'__COUNT__':len(rows), '__RUNTIME__':snapshot['runtime_checked'],
                       '__OVERVIEW__':snapshot['overview_reviewed'], '__OPTIONS__':options,
                       '__DATA__':json.dumps(snapshot, ensure_ascii=False).replace('</', '<\\/')}.items():
        template = template.replace(key, str(value))
    (bundle / 'review.html').write_text(template)
    return {k:v for k,v in snapshot.items() if k != 'rows'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kit', type=Path, default=Path('assets/hd-ai/tactical-kit'))
    parser.add_argument('--bundle', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.kit.resolve(), args.bundle.resolve())))
