"""Assemble stage-one outline text, compact AI portraits and local Lanczos maps."""
from pathlib import Path
import argparse,hashlib,json,shutil
from PIL import Image
from srw64_rom.resources import ResourceTable
from tools.hd_ai.extract_samples import ROOT,map_image,indexed,rgba16
from tools.hd_ai.rt64_hash import hasher,map_hash
from tools.hd_ai.portrait_matte import matte_portrait

def sha(data):return hashlib.sha256(data).hexdigest()
def upscale(image,scale):return image.convert('RGBa').resize((image.width*scale,image.height*scale),Image.Resampling.LANCZOS).convert('RGBA')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--fonts',type=Path,required=True);ap.add_argument('--portraits',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=False);pack=args.output/'pack';shutil.copytree(args.fonts/'pack',pack)
    db=json.loads((pack/'rt64.json').read_text());names={r['hashes']['rt64']:r['path'] for r in db['textures']}
    portrait_records=[]
    for sample in json.loads((args.portraits/'samples.json').read_text())['samples']:
        if 'reuse_reviewed_output' in sample:
            file=Path(sample['reuse_reviewed_output']);expected=sample['reuse_sha256'];model_size=list(Image.open(file).size)
        else:
            request=json.loads((args.portraits/'runs'/f"{sample['id']}--qwen-image-3.0--2/request.json").read_text())
            assert request['status']=='completed' and request['dimensions']==[512,512]
            file=args.portraits/request['output'];expected=request['output_sha256'];model_size=request['dimensions']
        assert sha(file.read_bytes())==expected
        source=Image.open(args.portraits/sample['source']).convert('RGBA')
        matte,matte_report=matte_portrait(Image.open(file),source.getchannel('A'))
        high=matte.convert('RGBa').resize((384,384),Image.Resampling.LANCZOS).convert('RGBA')
        high.save(args.output/f"{sample['id']}-384.png")
        for b in sample['binding']['bindings']:
            x,y=b['xy'];w,h=b['draw_size'];assert w==h==32 and x in (0,32,64) and y in (0,32,64)
            tile=high.crop((x*4,y*4,(x+w)*4,(y+h)*4));name=f"portrait-{b['hash']}.png";tile.save(pack/name)
            assert b['hash'] not in names;names[b['hash']]=name;db['textures'].append({'hashes':{'rt64':b['hash']},'path':name})
        portrait_records.append({'resource_id':sample['resource_id'],'character':sample['character'],'source_sha256':sha((args.portraits/sample['source']).read_bytes()),'generation_sha256':expected,'model_dimensions':model_size,'runtime_dimensions':[384,384],'bound_tiles':len(sample['binding']['bindings']),'matte':matte_report})
    table=ResourceTable((ROOT/'rom.z64').read_bytes());xxh=hasher();maps=[];map_outputs={};conflicts=set()
    gold=json.loads((ROOT/'assets/hd-ai/2026-09-08/map-capture-binding.json').read_text());d=table.extract(5604)[0]
    for b in gold['bindings']:
        r=b['record'];a=r['texture_offset'];p=r['palette_offset'];assert map_hash(d[a:a+2048],d[p:p+32],xxh)==b['hash']
    for rid in (5604,5605):
        d=table.extract(rid)[0];_,layout=map_image(d);atlas=Image.new('RGBA',layout['source_atlas_size'])
        for r in layout['tiles']:
            a=r['texture_offset'];p=r['palette_offset'];atlas.paste(indexed(d[a:a+2048],(64,64),rgba16(d[p:p+32]),4),tuple(r['xy']))
        high=upscale(atlas,4);atlas.save(args.output/f'map-{rid}-source.png');high.save(args.output/f'map-{rid}-lanczos4.png')
        for r in layout['tiles']:
            a=r['texture_offset'];p=r['palette_offset'];hash_=map_hash(d[a:a+2048],d[p:p+32],xxh);x,y=r['xy'];tile=high.crop((x*4,y*4,(x+64)*4,(y+64)*4))
            if hash_ in map_outputs and map_outputs[hash_].tobytes()!=tile.tobytes():conflicts.add(hash_)
            map_outputs[hash_]=tile
        maps.append({'resource_id':rid,'source_dimensions':list(atlas.size),'output_dimensions':list(high.size),'tiles':len(layout['tiles']),'alpha':'original palette alpha; global premultiplied-alpha Lanczos 4x'})
    for hash_,tile in map_outputs.items():
        if hash_ in conflicts:continue
        name=f'map-{hash_}.png';tile.save(pack/name);assert hash_ not in names
        db['textures'].append({'hashes':{'rt64':hash_},'path':name});names[hash_]=name
    (pack/'rt64.json').write_text(json.dumps(db,indent=2)+'\n')
    (pack/'srw64-dialogue-padding-v1').write_text('Opening dialogue: move glyph rectangles right/down by 4 logical pixels; preserve glyph dimensions and UVs.\n')
    report={'schema':'srw64.stage1-hd-pack.v1','portraits':portrait_records,'maps':maps,'font_manifest_sha256':sha((args.fonts/'pack/rt64.json').read_bytes()),'map_captured_hashes_verified':len(gold['bindings']),'map_hash_context_conflicts_kept_original':sorted(conflicts),'total_textures':len(db['textures']),'files_sha256':{p.name:sha(p.read_bytes()) for p in sorted(pack.iterdir()) if p.is_file()}}
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print({'portraits':len(portrait_records),'maps':len(maps),'textures':len(db['textures']),'map_conflicts_kept_original':len(conflicts)})
if __name__=='__main__':main()
