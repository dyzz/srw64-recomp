"""Freeze six source samples and the exact RGB inputs used by the benchmark."""
from __future__ import annotations
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image, ImageDraw
from tools.hd_ai.extract_samples import ROOT, OUT, indexed, rgba16
from srw64_rom.resources import ResourceTable

MAP_PROMPT='对图1进行忠实的高清修复，用于1999年二维手绘战略游戏的原画高清纹理替换。严格保持图1的每一处海岸线、岛屿、湖泊、山脉的位置、外轮廓、面积、相对比例和整个构图，不可增加、删除、移动任何地形。保持原有绿色植被、黄褐色地表、深蓝海洋色板与二维手绘画风。只清理像素锯齿、优化已有山脉和地表的细腻度，不改成卫星照片或写实三维地图。海面保持原来的纯色。保持正方形画幅，完整保留原图所有边缘，不裁切、不旋转、不镜像。不添加文字、地名、标记、网格、边框、UI或任何新物体。'


def main() -> None:
    table=ResourceTable((ROOT/'rom.z64').read_bytes())
    dumps=ROOT/'build/recomp/font-probe/replay-original-1/textures'
    (OUT/'inputs').mkdir(exist_ok=True)
    europe=Image.open(OUT/'extract/map-5604.png').transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB').convert('RGBA')
    entries=[('map-europe',europe,
              {'resource_id':5604,'transform':'map atlas crop, vertical flip, palette RGB made opaque for model input; original alpha restored in runtime pack'},MAP_PROMPT)]
    map2=Image.open(OUT/'extract/map-5605.png').transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    # A 640x320 covered source crop meets both super-resolution aspect limits.
    map2=map2.crop((32,0,672,320))
    entries.append(('map-asia',map2,{'resource_id':5605,'transform':'atlas crop, vertical flip, then crop [32,0,672,320]'},MAP_PROMPT.replace('保持正方形画幅','保持2:1横向画幅')))
    for address,rid,name in [(3041816,29,'portrait'),(2788752,1296,'ui-border')]:
        path=next(p for p in dumps.glob('*.rice.json') if json.loads(p.read_text())['texture']['address']==address)
        palette_path=path.with_name(path.name.replace('.rice.json','.rice.palette.rdram'))
        raw=palette_path.read_bytes();raw=bytes(raw[i^3] for i in range(len(raw)))
        data,_=table.extract(rid)
        fmt,w,h,_=struct.unpack_from('>4H',data)
        image=indexed(data[8:],(w,h),rgba16(raw),8 if fmt==15 else 4)
        source={'resource_id':rid,'palette_source':str(palette_path.relative_to(ROOT)),
                'palette_sha256':hashlib.sha256(raw).hexdigest(),'texture_load_address':address}
        if name=='ui-border':
            image=image.crop((320,0,400,16));source['crop']=[320,0,400,16]
            prompt='忠实高清修复图1的游戏金属装饰窗口边框。保持细长矩形的精确位置、长度、厚度、左右角形状、所有凹槽和银白色、蓝紫色配色，只平滑锯齿和整理原有线条。不要增加装饰、按钮、文字、标志或新图案。保持整张画幅和纯灰背景，不移动、不放大、不改变细长边框的布局。'
        else:
            prompt='忠实高清修复图1这张1999年日式二维赛璐璐游戏人物头像。严格保留同一位白发老人、脸型、皱纹、胡须、发型、眼神、原有表情、朝向、黑色礼服与领结、所有轮廓、位置、配色和原画风。仅将已有像素线条恢复为清楚细腻的二维手绘线条。不要美化变年轻，不要增加服饰细节，不要改成写实照片或三维。画幅、人物占比、姿势、裁切和纯灰背景必须与输入相同，不添加文字或物体。'
        entries.append((name,image,source,prompt))
    data,_=table.extract(688);pal,_=table.extract(1010)
    entries.append(('mech-icon',indexed(data[8:],(16,16),rgba16(pal[8:]),4),
        {'resource_id':688,'palette_resource_id':1010,'palette_pairing':'shared team palette, visual check only; live mapping pending'},
        '忠实高清修复图1的16像素日式战略游戏机械单位图标。保持完全相同的正面机体、头部双角、绿色眼部、躯干、肩部、四肢、紫蓝白配色、正方形构图和外轮廓比例。只整理已有线条和色块，不臆造机械部件或武器，不改变姿势，不改成立体模型或写实渲染。保持原图位置、大小、朝向和纯灰背景，不添加文字或任何新元素。'))
    data,_=table.extract(6228);pal,_=table.extract(6237)
    terrain=indexed(data[8:],(512,512),rgba16(pal[8:]),8).crop((272,16,304,48))
    entries.append(('terrain-forest',terrain,{'resource_id':6228,'palette_resource_id':6237,'crop':[272,16,304,48],
        'scope_adjustment':'confirmed terrain tile replaces unverified scene background palette'},
        '忠实高清修复图1的俯视角游戏森林地形图块。保持原图所有树木、深色树冠、草地的数量、位置、形状、色板、俯视角和边缘布局。仅细化已有的树冠叶片与地表像素，不增加新树、路径、建筑、阴影或物体。不改成照片，不改变透视。四条边的地物位置和颜色必须保持，保持原作二维手绘游戏贴图风格。不添加文字、网格、边框或标志。'))
    samples=[]
    for name,image,source,prompt in entries:
        folder=OUT/'sources';folder.mkdir(exist_ok=True)
        image.save(folder/f'{name}.png')
        if name=='ui-border':
            canvas=Image.new('RGBA',(96,96),(0,0,0,0));canvas.paste(image,(8,40));image=canvas
            source['canvas_padding']={'size':[96,96],'offset':[8,40]}
        rgb=Image.new('RGBA',image.size,(100,100,112,255));rgb.alpha_composite(image);rgb=rgb.convert('RGB')
        native_path=OUT/'sources'/f'{name}-canvas.png';image.save(native_path)
        # The SR service receives the native canvas (minimum 64); editing receives
        # nearest-neighbor enlargement solely to meet model input size limits.
        sr=rgb.resize((max(64,rgb.width),max(64,rgb.height)),Image.Resampling.NEAREST)
        sr.save(OUT/'inputs'/f'{name}-sr.png')
        factor=max(1,(512+min(rgb.size)-1)//min(rgb.size))
        edit=rgb.resize((rgb.width*factor,rgb.height*factor),Image.Resampling.NEAREST)
        edit.save(OUT/'inputs'/f'{name}.png')
        samples.append({'id':name,'source':f'sources/{name}.png','canvas':str(native_path.relative_to(OUT)),
            'source_dimensions':list(Image.open(folder/f'{name}.png').size),'canvas_dimensions':list(image.size),
            'input':f'inputs/{name}.png','sr_input':f'inputs/{name}-sr.png','input_dimensions':list(edit.size),
            'output_size':'2048*1024' if name=='map-asia' else '2048*2048',
            'prompt':prompt,'source_record':source,'alpha_policy':'source canvas mask preserved outside inference',
            'input_sha256':hashlib.sha256((OUT/'inputs'/f'{name}.png').read_bytes()).hexdigest()})
    (OUT/'samples.json').write_text(json.dumps({'schema':'srw64.hd-ai-samples.v1','samples':samples},ensure_ascii=False,indent=2)+'\n')
    print('Prepared',len(samples),'samples; forest terrain replaces an unverified scene background.')


if __name__=='__main__': main()
