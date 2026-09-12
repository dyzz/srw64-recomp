"""Prepare the first-stage portrait inputs; output 512px, game textures 4x."""
from pathlib import Path
import hashlib,json,shutil
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/hd-ai/stage1-zh'
NAMES={29:'劳伦斯',33:'玛娜米',166:'加里森',169:'万丈'}

def main():
    rows=json.loads((OUT/'portraits/portraits.json').read_text())['portraits']
    target=OUT/'portraits-ai';(target/'inputs').mkdir(parents=True,exist_ok=True)
    samples=[]
    for rid,name in NAMES.items():
        candidates=[r for r in rows if r['resource_id']==rid]
        r=max(candidates,key=lambda r:sum(Image.open(OUT/'portraits'/r['image']).convert('RGB').resize((1,1)).getpixel((0,0))))
        source=Image.open(OUT/'portraits'/r['image']).convert('RGBA')
        original=target/'inputs'/f'portrait-{rid}-source.png';source.save(original)
        flat=Image.new('RGBA',source.size,(100,100,112,255));flat.alpha_composite(source)
        inp=target/'inputs'/f'portrait-{rid}.png';flat.convert('RGB').resize((576,576),Image.Resampling.NEAREST).save(inp)
        sample={'id':f'portrait-{rid}','character':name,'resource_id':rid,'source':str(original.relative_to(target)),
                'input':str(inp.relative_to(target)),'input_sha256':hashlib.sha256(inp.read_bytes()).hexdigest(),
                'output_size':'512*512','runtime_size':[384,384], 'binding':r,
                'prompt':'忠实高清修复图1这张1999年日式二维赛璐璐游戏人物头像。严格保留同一个人物的脸型、五官、年龄、发型、眼神、原有表情、朝向、服装、所有轮廓、位置、配色和原画风。仅将已有像素线条恢复为清楚细腻的二维手绘线条。不要美化变年轻，不要增加服饰细节，不要改成写实照片或三维。画幅、人物占比、姿势、裁切和纯灰背景必须与输入相同，不添加文字或物体。'}
        if rid==29:
            sample['reuse_reviewed_output']=str(ROOT/'build/hd-ai/2026-09-08/runs/portrait--qwen-image-3.0--2/output.png')
            sample['reuse_sha256']='45ac1a1c6719c2c117853f7f8b6844f0117809b55f2b9e2d31d7dfb24e13406f'
        samples.append(sample)
    (target/'samples.json').write_text(json.dumps({'schema':'srw64.hd-ai-samples.v1','purpose':'native female stage1; qwen-image-3.0 512px only, original Lawrence candidate reused','samples':samples},ensure_ascii=False,indent=2)+'\n')
    print({'new_requests':len(samples)-1,'output_size':'512*512','runtime_size':[384,384]})
if __name__=='__main__':main()
