#!/usr/bin/env python3
"""Build a local-only viewer from the reviewed SRW64 static geometry survey."""
from __future__ import annotations
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCE = ROOT / 'build/analysis/3d-2026-09-09'
OUT = ROOT / 'build/model-viewer'


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, separators=(',', ':'))+'\n')


def texture(data: bytes, state: dict, errors: list[str]) -> str | None:
    if not state.get('on') or not all(k in state for k in ('tex','pal','size','width','height','palette_count')):
        return None
    width,height,size = state['width'],state['height'],state['size']
    bits=4 if size==0 else 8 if size==1 else 0
    if not bits:
        errors.append('unsupported-texture-size');return None
    key=[state.get(k) for k in ('tex','pal','size','width','height','palette_count','load','source_width','load_x','load_y')]
    name=hashlib.sha256(json.dumps(key).encode()+hashlib.sha256(data).digest()).hexdigest()[:20]+'.png'
    path=OUT/'textures'/name
    if path.exists():return 'textures/'+name
    pal=state['pal']; count=state['palette_count']
    if count not in (16,256) or pal+count*2>len(data):
        errors.append('unverified-palette');return None
    palette=[]
    for i in range(count):
        v=struct.unpack_from('>H',data,pal+i*2)[0]
        palette.append(tuple(round(((v>>s)&31)*255/31) for s in (11,6,1))+(255*(v&1),))
    # F3 block loads use the render-tile width. F4 source rectangles use the
    # declared source width; the loader's source pixel size must agree.
    stride=width
    x0=y0=0
    if state.get('load')=='tile':
        if state.get('source_size') != size:
            errors.append('unverified-load-tile-size');return None
        stride=state['source_width'];x0=state['load_x'];y0=state['load_y']
    pixels=[]
    for y in range(height):
        for x in range(width):
            i=(y+y0)*stride+x+x0;offset=state['tex']+i*bits//8
            if offset>=len(data):
                errors.append('texture-out-of-bounds');return None
            value=data[offset] if bits==8 else (data[offset]>>(4 if i%2==0 else 0))&15
            if value>=len(palette):
                errors.append('palette-index-out-of-bounds');return None
            pixels.extend(palette[value])
    Image.frombytes('RGBA',(width,height),bytes(pixels)).save(path)
    return 'textures/'+name


def batches(data: bytes, groups: list[dict]) -> tuple[list[dict], list[str]]:
    result=[];errors=[]
    for part,g in enumerate(groups):
        state={'on':False,'color':[200,200,200,255]}; face_index=0;active=None
        for offset in range(g['start'],g['end'],8):
            a,b=struct.unpack_from('>II',data,offset);op=a>>24
            if op==0xFA:state['color']=list(b.to_bytes(4,'big'))
            elif op==0xD7:state['on']=bool(a&0xff)
            elif op==0xFD:
                if b>>24!=4:raise RuntimeError('Unexpected texture segment')
                fmt=a>>21&7
                if fmt==0:state['pal']=b&0xffffff
                elif fmt==2:
                    state['tex']=b&0xffffff;state['source_width']=(a&4095)+1;state['source_size']=a>>19&3
            elif op==0xF0:state['palette_count']=((b>>14)&1023)+1
            elif op==0xF5 and (b>>24&7)==0:
                state['size']=a>>19&3;state['wrap_s']=(b>>8)&3;state['wrap_t']=(b>>18)&3
            elif op==0xF2:
                state['width']=((b>>12&4095)-(a>>12&4095))//4+1
                state['height']=((b&4095)-(a&4095))//4+1
                state['tile_x']=(a>>12&4095)/4;state['tile_y']=(a&4095)/4
            elif op==0xF3:state['load']='block'
            elif op==0xF4:
                state['load']='tile';state['load_x']=(a>>12&4095)//4;state['load_y']=(a&4095)//4
            elif op in (0x05,0x06):
                material={'texture':texture(data,state,errors),'color':state['color'],
                          'wrap_s':state.get('wrap_s',2),'wrap_t':state.get('wrap_t',2)}
                key=(part,json.dumps(material))
                if active is None or active[0]!=key:
                    batch={'part':part,'positions':[],'uvs':[],**material};result.append(batch);active=(key,batch)
                batch=active[1]
                for _ in range(2 if op==0x06 else 1):
                    face=g['faces'][face_index];face_index+=1
                    for v in face:
                        vertex=g['vertices'][str(v)]
                        batch['positions'].extend(vertex[:3])
                        batch['uvs'].extend([(vertex[4]/32-state.get('tile_x',0))/state.get('width',1),
                                            (vertex[5]/32-state.get('tile_y',0))/state.get('height',1)])
        if face_index!=len(g['faces']):raise RuntimeError('Face material traversal mismatch')
    return result,sorted(set(errors))


def thumbnail(groups: list[dict], path: Path) -> None:
    def project(v: list) -> tuple:
        x,y,z=v[:3];return (.82*x+.57*z,-y+.34*z-.49*x,.49*x+.7*y-.7*z)
    faces=[[project(g['vertices'][str(v)]) for v in f] for g in groups for f in g['faces']]
    points=[v for f in faces for v in f];low=[min(v[i] for v in points) for i in range(2)];high=[max(v[i] for v in points) for i in range(2)]
    scale=min(212/max(1,high[0]-low[0]),140/max(1,high[1]-low[1]))
    im=Image.new('RGB',(240,168),'#142029');draw=ImageDraw.Draw(im)
    for face in sorted(faces,key=lambda f:sum(v[2] for v in f)):
        a,b,c=face;u=[b[j]-a[j] for j in range(3)];v=[c[j]-a[j] for j in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in n)) or 1
        light=abs((n[0]*.25-n[1]*.6+n[2]*.65)/length);shade=round(95+145*light)
        xy=[(120+(v[0]-(high[0]+low[0])/2)*scale,84+(v[1]-(high[1]+low[1])/2)*scale) for v in face]
        draw.polygon(xy,fill=(int(shade*.65),int(shade*.84),shade),outline=(44,68,82))
    im.save(path)


def main() -> None:
    report=json.loads((SOURCE/'geometry-survey.json').read_text())
    geometry=json.loads((SOURCE/'geometry-data.json').read_text())
    if report['resource_count']!=483 or report['resources_with_errors']:
        raise RuntimeError('Expected the reviewed complete geometry survey')
    for folder in ('models','textures','thumbs','vendor','evidence'): (OUT/folder).mkdir(parents=True,exist_ok=True)
    vendor=HERE/'node_modules/three'
    for name in ('three.module.js','three.core.js'):shutil.copy2(vendor/'build'/name,OUT/'vendor'/name)
    shutil.copy2(vendor/'examples/jsm/controls/OrbitControls.js',OUT/'vendor/OrbitControls.js')
    shutil.copy2(vendor/'LICENSE',OUT/'vendor/THREE-LICENSE.txt')
    for name in ('index.html','style.css','app.js'):shutil.copy2(HERE/name,OUT/name)
    manifest=[]
    for record in report['resources']:
        rid=record['resource_id'];data=(SOURCE/f'resource-{rid}.bin').read_bytes();groups=geometry[str(rid)]
        if hashlib.sha256(data).hexdigest()!=record['sha256']:raise RuntimeError('Resource hash drift')
        parts,errors=batches(data,groups)
        model={'schema':'srw64.model-viewer-geometry.v1','id':rid,'batches':parts,'part_count':len(groups)}
        write_json(OUT/'models'/f'{rid}.json',model)
        thumbnail(groups,OUT/'thumbs'/f'{rid}.png')
        title='未标定资源';note='已解析局部几何，具体出现位置尚未确认。';status='unknown'
        if 5584<=rid<=5597 or rid==5607:
            title='舰船／飞行器形状';note='名称按外形暂记；尚未绑定游戏场景、舰名和动画。'
        if rid==5584:
            note='已找到与 5600 共用的对象资源表：5584 为索引 0，5600 为索引 15。对象创建路径已静态定位，出现章节和舰名仍待确认。'
        if rid==5600:
            title='剧情地图标记';note='与欧洲剧情地图同任务调用；包含平面部件与立体尖锥。';status='located'
        elif rid==5601:title='另一份地图标记候选'
        elif rid in (5604,5605):
            title='欧洲剧情地图' if rid==5604 else '亚洲地图图层'
            note='70 个矩形面；原始顶点的 Y 坐标均为 0。地图高低细节主要来自贴图。'
            status='located' if rid==5604 else 'decoded'
        elif rid in (5614,5750):title='场景几何候选'
        manifest.append({'id':rid,'title':title,'note':note,'status':status,'triangles':record['triangles'],
                         'vertices':sum(g['referenced_vertices'] for g in groups),'parts':len(groups),
                         'rank':record['local_coordinate_rank'],'bounds':record['bounds'],
                         'texture_count':len({b['texture'] for b in parts if b['texture']}),
                         'texture_notes':errors,'sha256':record['sha256'],'bytes':len(data)})
    probe=ROOT/'build/analysis/model-5600-probe';evidence=[]
    for label,title in [('baseline','原始画面'),('hidden','隐藏 5600'),('stretched','仅拉伸立体部分')]:
        path=probe/label/'present-60.png'
        if path.exists():
            shutil.copy2(path,OUT/'evidence'/f'5600-{label}.png')
            evidence.append({'title':title,'path':f'evidence/5600-{label}.png','sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    if (probe/'acceptance.json').exists():
        acceptance=json.loads((probe/'acceptance.json').read_text())
        if acceptance.get('status')=='verified':
            next(x for x in manifest if x['id']==5600).update(status='verified',note='同任务 GPU 对照已确认：隐藏后标记及其环形平面消失；仅修改六个顶点后立体部分变形。')
        shutil.copy2(probe/'acceptance.json',OUT/'evidence/5600-acceptance.json')
    highpoly=ROOT/'build/analysis/model-5600-highpoly'
    if (highpoly/'acceptance.json').exists():
        accepted=json.loads((highpoly/'acceptance.json').read_text())
        mesh_bytes=(highpoly/'mesh.json').read_bytes()
        if accepted.get('status')!='verified' or hashlib.sha256(mesh_bytes).hexdigest()!=accepted['mesh_sha256']:
            raise RuntimeError('High-poly acceptance or geometry drift')
        mesh=json.loads(mesh_bytes)
        original=json.loads((OUT/'models/5600.json').read_text())
        # Preserve every original ring draw; replace only the solid batches.
        high_parts=original['batches'][:1]
        for face,color in zip(mesh['faces'],mesh['colors']):
            high_parts.append({'part':0,'positions':[v for i in face for v in mesh['positions'][i]],
                               'uvs':[0]*6,'color':color,'texture':None,'wrap_s':2,'wrap_t':2,'shading':'unlit'})
        write_json(OUT/'models/5600-highpoly.json',{'schema':'srw64.model-viewer-geometry.v1',
                   'id':5600,'variant':'highpoly','batches':high_parts,'part_count':1})
        resource=next(r for r in manifest if r['id']==5600)
        resource['original_variant_label']='原版 · 8 面'
        resource['variants']=[{'key':'highpoly','label':'高模试作 · 96 面','file':'models/5600-highpoly.json',
            'overrides':{'title':'剧情地图标记 · 高模试作','status':'prototype','triangles':104,'vertices':54,'texture_count':1,
                'note':'立体菱形从 8 面增加至 96 面，增加圆角与腰部倒角；保持原尺寸和虚线环。已通过同任务 GPU 对照，尚未接入实时游戏。',
                'material_note':'菱形使用黄色切面颜色，虚线环保留原贴图；灰模与线框可查看新增几何。',
                'source_note':f'原创高模试作 · 几何 SHA-256 {accepted["mesh_sha256"]}'}}]
        frame=highpoly/'highpoly/present-60.png'
        if hashlib.sha256(frame.read_bytes()).hexdigest()!=accepted['frames']['highpoly']['sha256']:
            raise RuntimeError('High-poly GPU frame drift')
        shutil.copy2(frame,OUT/'evidence/5600-highpoly.png')
        shutil.copy2(highpoly/'acceptance.json',OUT/'evidence/5600-highpoly-acceptance.json')
        evidence.append({'title':'高模试作 · 立体 96 面','path':'evidence/5600-highpoly.png',
                         'sha256':accepted['frames']['highpoly']['sha256']})
    live_evidence=None
    live=ROOT/'build/recomp/model-5600'
    if (live/'acceptance.json').exists():
        accepted=json.loads((live/'acceptance.json').read_text())
        if accepted.get('status')!='verified-bounded-native-run':
            raise RuntimeError('Live model experiment not verified')
        if hashlib.sha256((highpoly/'mesh.json').read_bytes()).hexdigest()!=accepted['mesh_sha256']:
            raise RuntimeError('Viewer geometry differs from the live experiment')
        for name,run in accepted['runs'].items():
            if hashlib.sha256((live/name/'report.json').read_bytes()).hexdigest()!=run['report_sha256']:
                raise RuntimeError('Live run report drift')
        live_frames=[]
        for i,frame in enumerate(accepted['frames']):
            path=live/frame['path']
            if hashlib.sha256(path.read_bytes()).hexdigest()!=frame['sha256']:
                raise RuntimeError('Live GPU image drift')
            destination=f'evidence/5600-live-{i}.png'
            shutil.copy2(path,OUT/destination)
            live_frames.append({**frame,'path':destination})
        live_evidence={'frames':live_frames,
            'summary':'高模通过原游戏资源加载器载入，保留原旋转与剧情位置更新，已运行至第一话战术地图（16,800 VI，约 4 分 40 秒）。前两图为同一 VI 的原版与高模，1,032 个差异像素全部位于菱形本体，虚线环与周围画面一致。验证环境为原生重编译游戏 + RT64 / Metal；尚未覆盖完整路线或 N64 硬件。'}
        shutil.copy2(live/'acceptance.json',OUT/'evidence/5600-live-acceptance.json')
        resource=next(r for r in manifest if r['id']==5600)
        for variant in resource.get('variants',[]):
            if variant['key']=='highpoly':
                variant['overrides'].update(status='live',
                    note='立体菱形 8 → 96 面，增加圆角与腰部倒角；保持原尺寸和虚线环。已在实际游戏开场验证加载、旋转、位置更新及切入战术地图。')
    from native_marker import add_native_marker
    native_evidence = add_native_marker(ROOT, OUT, manifest)
    write_json(OUT/'manifest.json',{'schema':'srw64.model-viewer.v1','resources':manifest,'evidence5600':evidence,'liveEvidence5600':live_evidence,'nativeEvidence5600':native_evidence,
        'scope':'局部几何与简化源贴图预览；不模拟原游戏相机、场景变换、材质混合和动画。'})
    print(json.dumps({'output':str(OUT),'models':len(manifest),'textures':len(list((OUT/'textures').glob('*.png'))),
                      'resources_with_texture_notes':sum(bool(x['texture_notes']) for x in manifest)},ensure_ascii=False))


if __name__=='__main__':main()
