"""Build the local comparison page and resumable inventory without modifying images."""
import json, hashlib, html, datetime, shutil
from pathlib import Path
from PIL import Image, ImageStat

root=Path(__file__).resolve().parents[2]/'assets/hd-ai/tactical-kit'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=root/'outputs'
manifest=json.loads((root/'manifest.json').read_text())
records=json.loads((out/'generation-records.json').read_text())['records']
current={r['id']:r for r in records if '/archive/' not in r['output']}
style_manifest_path=root/'style/manifest.json'
style_manifest=json.loads(style_manifest_path.read_text()) if style_manifest_path.is_file() else {}
approved_families=[r['family'] for r in style_manifest.get('references',[]) if r.get('user_approved') and (root/r['path']).is_file() and hashlib.sha256((root/r['path']).read_bytes()).hexdigest()==r['sha256']]
geometry_path=out/'geometry-audit.json'
geometry=json.loads(geometry_path.read_text()) if geometry_path.is_file() else None
geometry_rows={r['id']:r for r in geometry['rows']} if geometry else {}
composition_path=root/'composed/preview-v1/progress.json'
composition=json.loads(composition_path.read_text()) if composition_path.is_file() else None
colony_frames_path=root/'composed/colony-alpha-v5-final/manifest.json'
colony_frames=json.loads(colony_frames_path.read_text()) if colony_frames_path.is_file() else None
runtime_evidence_path=root/'composed/runtime-map020/evidence.json'
runtime_smoke=json.loads(runtime_evidence_path.read_text()) if runtime_evidence_path.is_file() else None
colony_evidence_path=root/'composed/runtime-colony-v5/evidence-map087/evidence.json'
colony_evidence=json.loads(colony_evidence_path.read_text()) if colony_evidence_path.is_file() else None
smoke_maps=([runtime_smoke['map']] if runtime_smoke else [])+([colony_evidence['map']] if colony_evidence else [])
registration_versions=[p for p in (root/'composed').glob('registration-v*')
                       if p.name.removeprefix('registration-v').isdigit() and (p/'progress.json').is_file()]
registration_root=max(registration_versions,key=lambda p:int(p.name.removeprefix('registration-v'))) if registration_versions else root/'composed/registration-v2'
registration_path=registration_root/'progress.json'
registration=json.loads(registration_path.read_text()) if registration_path.is_file() else None
registration_smoke=[]
for path in sorted(registration_root.glob('map-*/runtime-evidence/evidence.json')):
    record=json.loads(path.read_text());folder=path.parent.parent
    assert record['base_sha256']==sha(folder/'base.png') and record['index_sha256']==sha(folder/'index.png'),path
    assert record['meta_sha256']==sha(folder/'meta.json'),path
    for name,digest in record['files'].items():
        assert sha(path.parent/name)==digest,path.parent/name
    if record.get('native_host_smoke_verified'):
        registration_smoke.append(record['map'])
smoke_maps=sorted(set(smoke_maps+registration_smoke))
bundle_path=root/'composed/runtime-integrated-v1/bundle.json'
bundle=json.loads(bundle_path.read_text()) if bundle_path.is_file() else None
bundle_smoke=[]
if bundle:
    for path in sorted(bundle_path.parent.glob('map-*/runtime-evidence/evidence.json')):
        try:
            record=json.loads(path.read_text())
        except json.JSONDecodeError:
            continue  # A live capture may be finishing this record right now.
        folder=path.parent.parent
        assert record['base_sha256']==sha(folder/'base.png') and record['index_sha256']==sha(folder/'index.png'),path
        assert record['meta_sha256']==sha(folder/'meta.json'),path
        for name,digest in record['files'].items():
            assert sha(path.parent/name)==digest,path.parent/name
        if record.get('native_host_smoke_verified'):bundle_smoke.append(record['map'])
    smoke_maps=sorted(set(smoke_maps+bundle_smoke))
jobs=[]
for m in manifest['maps']:
    whole={**m['whole'],'id':f"map-{m['map']:03d}-00-whole"}
    for j in [whole,*m['windows']]:
        jobs.append({**j,'map':m['map'],'family':m['family'],'anchor':m['anchor'] and (j is whole or j.get('id','').endswith('-01'))})
jobs.append({**manifest['colony'],'id':'colony-sheet','family':'colony','anchor':False})
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
bad_inputs=[]; bad_outputs=[]; pending=[]; rows=[]
lum=lambda x:sum(a*b for a,b in zip(x,[.2126,.7152,.0722]))
for j in jobs:
    if sha(root/j['input'])!=j['input_sha256']: bad_inputs.append(j['id'])
    assert (root/j['prompt']).is_file(),j['prompt']
    r=current.get(j['id'])
    if not r or not (root/j['output']).is_file():
        pending.append(j); continue
    if sha(root/j['output'])!=r['sha256']: bad_outputs.append(j['id'])
    a=Image.open(root/j['input']).convert('RGB'); b=Image.open(root/j['output']).convert('RGB')
    am=ImageStat.Stat(a).mean; bm=ImageStat.Stat(b).mean
    recorded_prompt=out/'recorded-prompts'/f"{j['id']}-{r['sha256'][:12]}.txt"
    recorded_prompt.parent.mkdir(exist_ok=True)
    recorded_prompt.write_text(r.get('prompt') or (root/r['prompt_file']).read_text())
    rows.append({'id':j['id'],'family':j['family'],'family_name':manifest['families'].get(j['family'],{'name_zh':'殖民地动画'})['name_zh'],
        'input':'../'+j['input'],'output':Path(j['output']).name+'?v='+r['sha256'][:12],
        'prompt':str(recorded_prompt.relative_to(out)),'source_prompt':'../'+j['prompt'],
        'size':list(b.size),'source_mean_rgb':[round(x,1) for x in am],'output_mean_rgb':[round(x,1) for x in bm],
        'luma_ratio':round(lum(bm)/lum(am),3),'aspect_error_percent':round(abs((b.width/b.height)/(a.width/a.height)-1)*100,3),
        'review':r.get('visual_review',{}),'anchor':j['anchor'],
        'geometry':{k:geometry_rows[j['id']][k] for k in ('reliable_samples','total_samples','max_reliable_shift_source_px','suspect_samples')}
        if j['id'] in geometry_rows and geometry_rows[j['id']]['sha256']==r['sha256'] else None})
assert not bad_inputs,bad_inputs
assert not bad_outputs,bad_outputs
audit={'updated_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'total':len(jobs),'generated':len(rows),'pending':len(pending),
       'input_hashes_verified':len(jobs),'output_hashes_verified':len(rows),'style_approved_by_user':len(approved_families)==len(manifest['families']),
       'approved_style_families':approved_families,'conditional_families':style_manifest.get('conditional_families',{}),
       'runtime_verified':False,'rows':rows,
       'composition_maps_staged':composition['maps_composed'] if composition else 0,
       'registration_revised_maps':[r['map'] for r in registration['rows']] if registration else [],
       'registration_review':'../'+str(registration_root.relative_to(root))+'/review.html' if registration else None,
       'runtime_smoke_maps':smoke_maps,
       'runtime_bundle_maps':bundle['maps'] if bundle else 0,
       'runtime_bundle_smoke_maps':bundle_smoke,
       'colony_overlay_maps':len(colony_evidence['runtime_maps']) if colony_evidence else 0,
       'needs_revision':[r['id'] for r in rows if r['review'].get('status')=='needs_revision'],
       'geometry_suspects':[r['id'] for r in rows if r['geometry'] and r['geometry']['suspect_samples']],
       'requirements':[
        {'requirement':'Generate 8 whole-map and 6 window anchors','status':'generated; visual/style review pending'},
        {'requirement':'Approve and freeze family style references','status':str(len(approved_families))+' of '+str(len(manifest['families']))+' families approved; remaining family gates recorded separately'},
        {'requirement':'Generate all 109 wholes and 772 windows in story batches','status':f"{sum(r['id']!='colony-sheet' for r in rows)} of 881 saved; {sum(j['id']!='colony-sheet' for j in pending)} remaining; family gates still apply"},
        {'requirement':'Generate separate 8-frame colony sheet','status':'generated; transparent frames integrated and all eight phases smoke tested on map 87' if colony_evidence else 'generated; animation/runtime validation pending' if any(r['id']=='colony-sheet' for r in rows) else 'pending'},
        {'requirement':'Register images and verify cell offsets within half a cell','status':'sparse geometry diagnostics available; alignment and exhaustive verification pending' if geometry else 'pending'},
        {'requirement':'Compose whole maps and windows with feathered seams','status':f"{composition['maps_composed']} candidate maps staged; sparse registration and cell-colour diagnostics recorded; full geometry acceptance pending" if composition else 'pending'},
        {'requirement':'Protect water/lights/borders and implement dynamic colony treatment','status':f"18 maps / 29 instances exported with game-driven colony overlay; static colony indices cleared; map 87 eight-phase/scroll/toggle smoke tested" if colony_evidence else f"staged map protection masks verified exactly; {len(colony_frames['frames'])} transparent colony frames cut; runtime colony overlay pending" if composition and colony_frames else 'pending'},
        {'requirement':'Export runtime assets and compare in game','status':f"candidate assets exported; maps {smoke_maps} smoke tested only; full runtime acceptance pending" if smoke_maps else 'pending'},
        {'requirement':'Preserve source inputs','status':'all manifest input hashes verified'},
        {'requirement':'Exclude 22 layout variants from this package','status':'deferred to separate package as README specifies'}]}
(out/'progress.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n')
(out/'pending-jobs.json').write_text(json.dumps(pending,ensure_ascii=False,indent=2)+'\n')
style=[]
for family,info in manifest['families'].items():
    m=next(m for m in manifest['maps'] if m['map']==info['anchor_map'])
    j=m['windows'][0] if m['windows'] else m['whole']
    if family in style_manifest.get('conditional_families',{}):
        j=m['whole']
    dest=out/'style-candidates'/f'{family}.png'; dest.parent.mkdir(exist_ok=True)
    shutil.copy2(root/j['output'],dest)
    style.append({'family':family,'source':j['output'],'candidate':str(dest.relative_to(root)),
                  'sha256':sha(dest),'user_approved':False})
(out/'style-candidates/manifest.json').write_text(json.dumps(style,ensure_ascii=False,indent=2)+'\n')
template='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>战术地图 · 原图对照</title>
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,sans-serif;background:#11151a;color:#e9eef4}body{margin:0}header,main{max-width:1500px;margin:auto;padding:24px}h1{font-size:27px;margin:0 0 10px}p{color:#b7c2cf;line-height:1.6;margin:8px 0}a{color:#91ceff}nav{display:flex;gap:12px;flex-wrap:wrap;align-items:center;position:sticky;top:0;background:#182029;padding:14px 24px;z-index:2}select,button{font:inherit;padding:8px 13px;background:#273544;color:inherit;border:1px solid #46596c;border-radius:6px}button.active{background:#345d78;border-color:#90cbe8}label{font-size:14px;color:#b7c2cf}.stage{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:15px}.stage.overlay{display:block;position:relative}.stage.single{display:block}.stage figure{margin:0;background:#080b10;border-radius:7px;overflow:hidden}.stage img{width:100%;height:70vh;object-fit:contain;display:block}.stage.overlay figure:nth-child(2){position:absolute;inset:0;background:none}.stage.overlay figure:nth-child(2) figcaption{display:none}figcaption{padding:10px;font-size:13px;color:#b7c2cf}.stage.single figure.hide{display:none}.metrics{display:flex;gap:12px;flex-wrap:wrap;margin:18px 0}.metric{padding:12px 18px;background:#1a2632;border-radius:7px}.metric b{display:block;font-size:22px}.metric small{color:#a9bbcd}.notes{padding:16px;background:#1c232d;border-left:3px solid #edc26c;line-height:1.7}.thumbs{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:12px;margin-top:24px}.thumb{padding:0;overflow:hidden;text-align:left;cursor:pointer}.thumb img{height:130px;width:100%;object-fit:cover}.thumb span{display:block;padding:10px;font-size:13px}footer{margin:30px 0;color:#a5b4c4;font-size:13px}input[type=range]{width:160px}#blend{display:none}@media(max-width:750px){header,main{padding:16px}.stage{grid-template-columns:1fr}.stage img{height:52vh}nav{position:static;padding:12px}.thumbs{grid-template-columns:repeat(2,1fr)}}
</style>
<header><h1>战术地图 · 原图对照</h1><p>家族样板与已生成地图。查看地貌布局、明暗、动态区域；这些结果尚未完成逐格配准及游戏验证。</p><p id="progress"></p></header>
<nav><label>家族 <select id="family"><option value="all">全部</option></select></label><label>图片 <select id="asset"></select></label><button data-mode="side" class="active">左右对照</button><button data-mode="source">只看原图</button><button data-mode="output">只看新版</button><button data-mode="overlay">叠加检查</button><label id="blend">新版透明度 <input id="opacity" type="range" min="0" max="100" value="50"></label></nav>
<main><h2 id="title"></h2><p id="links"></p><div class="stage" id="stage"><figure id="before"><img id="source" alt="原始输入"><figcaption>原图 · 提取后的硬像素输入</figcaption></figure><figure id="after"><img id="output" alt="修订后的生成图"><figcaption>新版 · 修订后的生成结果</figcaption></figure></div><div class="metrics" id="metrics"></div><div class="notes" id="notes"></div><div class="thumbs" id="thumbs"></div><footer>平均亮度按 RGB 加权统计，只用于发现色调漂移，不证明布局、纹理或动态正确；宇宙图去除殖民地也会改变均值。查看 <a href="geometry-audit.json">稀疏配准诊断</a> · <a href="generation-records.json">全部生成记录</a> · <a href="progress.json">范围与验证状态</a> · <a href="pending-jobs.json">剩余清单</a> · <a href="../composed/preview-v1/review.html">全量合成预览</a> · <a id="registration-link" href="../composed/registration-v2/review.html">配准修订对照</a> · <a href="../composed/colony-alpha-v5-final/review.html">殖民地透明帧</a> · <a href="../composed/runtime-colony-v5/review.html">卫星游戏同帧对照</a>。</footer></main>
<script>
const data=__DATA__;if(data.registration_review)document.getElementById('registration-link').href=data.registration_review;const rows=data.rows;const $=x=>document.getElementById(x);let mode='side';
const notes={ground:'道路、城镇、烧毁建筑和发光坑大体保留；草地和小植被仍有生成差异，河岸需逐格核对。',space:'动态殖民地已去除；保留固定残骸和小行星，恢复原图已有的微弱蓝色背景。星点与细碎残骸仍非逐像素对应。',fortress:'恢复了漏掉的固定设备，清除了新增星点；空洞回到纯黑。面板与岩壁细节仍需逐格核对。',moon:'修回暗冷灰色月壤，保留主要陨石坑、圆形设施和线缆。新增细小石块、浅坑不能视作原版几何证据。',desert:'修回灰米色，减弱地画深沟并去掉新增大灌木。线条宽度与局部轮廓仍需配准。','clouds-sea':'恢复浅色海水和白云；海岸、云层开口仍有局部差异，尚未验证动画合成。',galaxy:'压回暗色星云，核心由偏黄改回冷白；旋臂大体保留，小星点和尘带纹理有差异。','clouds-land':'降低地面的鲜绿色，恢复暖白云层；云层透明度及开口边缘仍存在生成差异。'};
for(const [f,name] of [...new Map(rows.map(r=>[r.family,r.family_name]))]){const o=document.createElement('option');o.value=f;o.textContent=name;$('family').append(o)}
$('progress').textContent=`当前保存 ${data.generated} / ${data.total} 张，剩余 ${data.pending} 张；已确认 ${(data.approved_style_families||[]).length} 个家族画风；整批与运行时验收未完成。`;
function applyMode(){const s=$('stage');s.className='stage'+(mode==='overlay'?' overlay':mode==='side'?'':' single');$('before').className=mode==='output'?'hide':'';$('after').className=mode==='source'?'hide':'';$('after').style.opacity=mode==='overlay'?$('opacity').value/100:1;$('blend').style.display=mode==='overlay'?'inline':'none';document.querySelectorAll('[data-mode]').forEach(b=>b.classList.toggle('active',b.dataset.mode===mode))}
function show(id){const r=rows.find(x=>x.id===id);history.replaceState(null,'','#'+id);$('asset').value=id;$('title').textContent=r.family_name+' · '+r.id;$('source').src=r.input;$('output').src=r.output;$('links').innerHTML=`<a href="${r.input}" target="_blank">原图文件</a> · <a href="${r.output}" target="_blank">新版文件</a> · <a href="${r.prompt}" target="_blank">本次修订提示词</a> · <a href="${r.source_prompt}" target="_blank">初始提示词</a>`;$('metrics').innerHTML=`<div class="metric"><b>${r.size.join(' × ')}</b><small>生成尺寸</small></div><div class="metric"><b>${r.luma_ratio.toFixed(3)} 倍</b><small>新版 / 原图平均亮度</small></div><div class="metric"><b>${r.aspect_error_percent.toFixed(3)}%</b><small>画幅比例偏差</small></div>`;if(r.geometry){const g=r.geometry;$('metrics').innerHTML+=`<div class="metric"><b>${g.reliable_samples} / ${g.total_samples}</b><small>可信配准抽样点；最大偏移 ${g.max_reliable_shift_source_px??'未知'} 源像素</small></div>`;}$('notes').textContent=(r.review.status==='needs_revision'?'待修订，家族尚未通过。 ':'')+(r.review.summary||(r.review.remaining||[]).join(' ')||notes[r.family]||'')+' 此页提供风格及布局预览；稀疏配准抽样不等于逐格验证通过，不代表实机验收。';applyMode()}
function filter(){const rs=rows.filter(r=>$('family').value==='all'||r.family===$('family').value);$('asset').replaceChildren();$('thumbs').replaceChildren();for(const r of rs){const o=document.createElement('option');o.value=r.id;o.textContent=r.id;$('asset').append(o);const b=document.createElement('button');b.className='thumb';b.innerHTML=`<img loading="lazy" src="${r.output}" alt="${r.id}"><span>${r.family_name} · ${r.id}</span>`;b.onclick=()=>{show(r.id);window.scrollTo({top:0,behavior:'smooth'})};$('thumbs').append(b)}if(rs.length)show(rs[0].id)}
$('family').onchange=filter;$('asset').onchange=()=>show($('asset').value);$('opacity').oninput=applyMode;document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=()=>{mode=b.dataset.mode;applyMode()});const initialId=location.hash.slice(1);filter();if(rows.some(r=>r.id===initialId))show(initialId);
</script></html>'''
(out/'review.html').write_text(template.replace('__DATA__',json.dumps(audit,ensure_ascii=False).replace('</','<\\/')))
print(json.dumps({'generated':len(rows),'pending':len(pending),'source_hashes_ok':len(jobs),'max_aspect_error_percent':max(x['aspect_error_percent'] for x in rows),'tone':[{k:r[k] for k in ['id','luma_ratio']} for r in rows]},ensure_ascii=False))
