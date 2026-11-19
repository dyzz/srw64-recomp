"""Read-only HD coverage and visual review. Run with .venv/bin/python tools/content/audit_battle_art.py.
Writes build/review/battle-art-20261105; never changes the bound art pack.
Human follow-on atlas pairings are visual/static candidates, not runtime observations.
"""
from __future__ import annotations
import csv
import hashlib
import json
import os
from collections import defaultdict
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
from srw64_rom.resources import ResourceTable
from srw64_native.battle_graphics import read_triplets, read_map_movies, parse_scene, decode_atlas, render_scene
from PIL import Image, ImageDraw

OUT = ROOT / 'build/review/battle-art-20261105'
LABELS = dict(gap='其他动作缺图候选', cutin='Cut-in', movie='地图演出', human='多蒙／东方不败', prop='武器／零件', shield='盾', defence='防御', reaction='反应', brush='招式文字', explosion='击毁', hit='命中', weapon='武器特效', unscripted='无脚本直接引用', follow_on='后续场景（部分配对为推断）')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'originals').mkdir(exist_ok=True)
    rom = (ROOT / 'rom.z64').read_bytes()
    table = ResourceTable(rom)
    manifest = json.loads((ROOT / 'content/art/stage1-hd.json').read_text())
    index, packs = {}, []
    for kind, name in [('units', 'units.json'), ('unit_extras', 'unit-extras.json'), ('battle_sprites', 'battle-sprites.json')]:
        folder = ROOT / manifest[kind]['path']
        source = folder / name
        packs.append({'kind': kind, 'path': str(source.relative_to(ROOT)), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
        for row in json.loads(source.read_text())['images']:
            key = (row['scene'], row['atlas'], row['palette'], row.get('frame', 0))
            index[key] = (row, folder / row['file'])
    source_rows = json.loads((ROOT / 'assets/hd-ai/cutins/frames.json').read_text())['frames']
    groups = {}
    def group(kind, identity, s, a, p, title, basis):
        key = (kind, identity, s, a, p)
        if key not in groups:
            groups[key] = dict(kind=kind, identity=identity, scene=s, atlas=a, palette=p, title=title, basis=basis, frames=[])
        return groups[key]
    def frame(g, f, original, box):
        hit = index.get((g['scene'], g['atlas'], g['palette'], f))
        row, path = hit if hit else ({}, None)
        g['frames'].append(dict(frame=f, original=os.path.relpath(original, OUT), box=box,
            hd=os.path.relpath(path, OUT) if path and path.exists() else None,
            method=row.get('method', 'whole-pose' if hit else 'missing'),
            state='present' if path and path.exists() else 'file-missing' if hit else 'key-missing'))
    weapons = json.loads((ROOT / 'assets/original-graphics/animations.json').read_text())['weapon']
    refs = defaultdict(list)
    for w in weapons:
        for a in w['actors']:
            label = f"{w['index']} {w['weapon']}"
            if label not in refs[a['registry']]: refs[a['registry']].append(label)
    movies = {i:(title, trigger, ts) for i,title,trigger,ts in read_map_movies(rom)}
    for r in source_rows:
        kind = r['set']
        identity = r.get('registry', r.get('movie', r.get('unit', r.get('code', r.get('base')))))
        title = f'{LABELS[kind]} {identity}'
        if kind == 'cutin': title += ' · ' + (' / '.join(refs[identity]) or '武器记录无直接引用')
        if kind == 'movie': title += ' · ' + movies[identity][0]
        g = group(kind, identity, r['scene'], r['atlas'], r['palette'], title, 'frames.json；按当前包精确键核对，未运行游戏')
        frame(g, r['frame'], ROOT / f"assets/hd-ai/cutins/inputs/{r['input']}.png", r['box'])
    for name, start, atlas, palette in [('多蒙',4972,4930,4931),('东方不败',4976,4942,4943)]:
        at, _ = decode_atlas(table.extract(atlas)[0], table.extract(palette)[0])
        for s in range(start,start+4):
            sc = parse_scene(table.extract(s)[0]); ims,_ = render_scene(sc,at)
            g = group('human',name,s,atlas,palette,f'{name} · 场景 {s}',
                '站姿三元组已登记' if s == start else '相邻场景＋同图集离线目视匹配；动作调用和实际配对待运行时核对')
            g['steps'] = sc.steps
            for f,parts in enumerate(sc.frames):
                if not parts: continue
                box=[min(q.x for q in parts), min(q.y for q in parts), max(q.x+q.w for q in parts), max(q.y+q.h for q in parts)]
                original=OUT / f'originals/{s}-f{f}.png'
                x0,y0,_,_=sc.bounds()
                ims[f].crop((box[0]-x0,box[1]-y0,box[2]-x0,box[3]-y0)).save(original)
                frame(g,f,original,box)
    # Expand the missing-pose-follow-on audit to every ROM resource. These are
    # structural candidates, NOT a claim that the inferred atlas is used at runtime.
    poses = read_triplets(rom, 'unit_poses')
    pose_atlases = {a for s,a,p in poses if s}
    registered = defaultdict(set)
    for s,a,p in read_triplets(rom, 'battle_scenes') + poses:
        if s: registered[s].add((a,p))
    names = defaultdict(list)
    with (ROOT/'assets/original-graphics/units.csv').open(encoding='utf-8-sig') as stream:
        for row in csv.DictReader(stream):
            names[int(row['atlas'])].append(f"{row['id']} {row['name']}")
    parsed, atlases, candidates = {}, {}, []
    for n in range(table.count):
        try: parsed[n] = parse_scene(table.extract(n)[0])
        except (ValueError, IndexError, AssertionError): pass
    for scene, sc in parsed.items():
        if scene in registered or not any(sc.frames): continue
        base = scene-1
        while base in parsed and scene-base <= 40:
            found=[]
            for a,pal in sorted(registered.get(base, ())):
                if a not in pose_atlases: continue
                if (a,pal) not in atlases:
                    atlases[a,pal] = decode_atlas(table.extract(a)[0], table.extract(pal)[0])[0]
                w,h = atlases[a,pal].size
                if all(q.s+q.w <= w and q.t+q.h <= h for parts in sc.frames for q in parts): found.append((a,pal))
            if found:
                for a,pal in found:
                    missing=[f for f,parts in enumerate(sc.frames) if parts and
                             ((scene,a,pal,f) not in index or not index[scene,a,pal,f][1].exists())]
                    if not missing: continue
                    candidates.append(dict(scene=scene,atlas=a,palette=pal,base=base,missing=missing,units=names[a]))
                    if scene in (4973,4974,4975,4977,4978,4979): continue  # already in human cards
                    g=group('gap',base,scene,a,pal,' / '.join(names[a])+f' · 候选 {scene}',
                            f'仅结构候选：最近可容纳的登记场景 {base}，同站姿图集；未证实调用与图集配对，可能误配')
                    ims,_=render_scene(sc,atlases[a,pal]);x0,y0,_,_=sc.bounds()
                    for f,parts in enumerate(sc.frames):
                        if not parts: continue
                        box=[min(q.x for q in parts),min(q.y for q in parts),max(q.x+q.w for q in parts),max(q.y+q.h for q in parts)]
                        original=OUT/f'originals/candidate-{scene}-{a}-{pal}-f{f}.png'
                        ims[f].crop((box[0]-x0,box[1]-y0,box[2]-x0,box[3]-y0)).save(original)
                        frame(g,f,original,box)
                break
            base-=1
    scenes=read_triplets(rom,'battle_scenes')
    # Independently enumerate the full registry, rather than trusting export coverage.
    registry=[]
    for i,(s,a,p) in enumerate(scenes):
        if not s: continue
        sc=parse_scene(table.extract(s)[0])
        fs=[f for f,parts in enumerate(sc.frames) if parts]
        missing=[f for f in fs if (s,a,p,f) not in index or not index[s,a,p,f][1].exists()]
        registry.append(dict(registry=i,scene=s,atlas=a,palette=p,nonempty_frames=len(fs),missing=missing,weapons=refs[i]))
    movie_audit=[]
    for i,(title,trigger,ts) in movies.items():
        keys=[(s,a,p,f) for s,a,p in ts for f,parts in enumerate(parse_scene(table.extract(s)[0]).frames) if parts]
        missing=[k for k in keys if k not in index or not index[k][1].exists()]
        movie_audit.append(dict(movie=i,title=title,trigger=trigger,triplets=len(ts),nonempty_frames=len(keys),missing=missing))
    cards=list(groups.values())
    cards.sort(key=lambda g: (['human','cutin','movie'].index(g['kind']) if g['kind'] in ['human','cutin','movie'] else 3,g['kind'],str(g['identity']),g['scene']))
    for g in cards:
        sc = parsed[g['scene']]
        if g['scene'] == 4983 and (ROOT/'output/imagegen/albert-4983-v1/index.html').exists():
            g['trial'] = '../../../output/imagegen/albert-4983-v1/index.html'
        if 4972 <= g['scene'] <= 4987 and (ROOT/'output/imagegen/human-actions-v2/index.html').exists():
            g['trial'] = f"../../../output/imagegen/human-actions-v2/index.html?scene={g['scene']}"
        g['steps'] = sc.steps
        g['loop_step'] = sc.loop_step
        g['bounds'] = sc.bounds()
        g['empty_frames'] = [f for f,parts in enumerate(sc.frames) if not parts]
        g['animated'] = len({f for f,t in sc.steps}) > 1
    for g in cards:
        if g['kind'] == 'gap' and g['scene'] in (2491,2492):
            g['basis'] += '；离线图像明显残缺，配对可疑，禁止据此直接补包'
    summary={k:dict(groups=sum(g['kind']==k for g in cards),frames=sum(len(g['frames']) for g in cards if g['kind']==k),missing=sum(f['state']!='present' for g in cards if g['kind']==k for f in g['frames'])) for k in LABELS}
    data=dict(rom_sha256=hashlib.sha256(rom).hexdigest(),packs=packs,summary=summary,cards=cards,registry=registry,movies=movie_audit,pose_follow_on_candidates=candidates)
    (OUT/'audit.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
    html=(Path(__file__).with_name('battle_art_review.html')).read_text()
    html=html.replace('__DATA__',json.dumps({'cards':cards},ensure_ascii=False).replace('<','\\u003c')).replace('__LABELS__',json.dumps(LABELS,ensure_ascii=False))
    (OUT/'index.html').write_text(html)
    # Contact sheets for visual QA, showing representative frames from each scene.
    for kind in ['human','cutin','movie','gap']:
        own=[g for g in cards if g['kind']==kind]
        sheet=Image.new('RGB',(1000,((len(own)+4)//5)*150),(45,50,62));draw=ImageDraw.Draw(sheet)
        for i,g in enumerate(own):
            f=g['frames'][len(g['frames'])//2];im=Image.open(OUT/f['original']).convert('RGBA');im.thumbnail((185,120));x=i%5*200;y=i//5*150
            sheet.paste(im,(x+(190-im.width)//2,y+22),im);draw.text((x+5,y+3),f"{g['identity']} / {g['scene']} f{f['frame']}",fill='white')
        sheet.save(OUT/f'{kind}-contact.png')
    human_frames = [(g, f) for g in cards if g['kind'] == 'human' for f in g['frames']]
    sheet = Image.new('RGB', (960, ((len(human_frames)+5)//6)*145), (45,50,62))
    draw = ImageDraw.Draw(sheet)
    for i, (g, f) in enumerate(human_frames):
        im = Image.open(OUT/f['original']).convert('RGBA')
        factor = min(4, 140/im.width, 105/im.height)
        im = im.resize((round(im.width*factor), round(im.height*factor)), Image.Resampling.NEAREST)
        x, y = i%6*160, i//6*145
        sheet.paste(im, (x+(160-im.width)//2, y+30), im)
        draw.text((x+4,y+4), f"{g['scene']} f{f['frame']} {f['state']}", fill='white')
    sheet.save(OUT/'human-all-frames.png')
    print(json.dumps(summary,ensure_ascii=False,indent=2))
    print('pose follow-on candidates:',len(candidates),'missing keys:',sum(len(r['missing']) for r in candidates))
    print('cutin registry:',len([r for r in registry if 984<=r['registry']<=1038]),'missing:',sum(len(r['missing']) for r in registry if 984<=r['registry']<=1038))
    print('map movies:',len(movie_audit),'missing:',sum(len(r['missing']) for r in movie_audit))

if __name__=='__main__': main()
