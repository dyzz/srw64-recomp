"""Recover candidate portraits from live texture dumps by ROM byte equality."""
from pathlib import Path
import argparse,hashlib,json,struct
from PIL import Image,ImageDraw
from srw64_rom.resources import ResourceTable
from tools.hd_ai.extract_samples import indexed,rgba16
ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--textures',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    table=ResourceTable((ROOT/'rom.z64').read_bytes());resources={}
    for rid in range(table.count):
        e=table.entry(rid)
        if e.decoded_size!=8+96*96:continue
        d,_=table.extract(rid)
        if struct.unpack_from('>3H',d)==(15,96,96):resources[rid]=d
    found={}
    for path in sorted(args.textures.glob('*.rice.json')):
        load=json.loads(path.read_text());t=load['texture'];tile=load['tile']
        if load['type']!='Tile' or (t['width'],t['siz'],t['fmt'])!=(96,1,2):continue
        x,y=tile['uls']//4,tile['ult']//4
        if x not in (0,32,64) or y not in (0,32,64):continue
        raw_path=path.with_name(path.name.replace('.rice.json','.rice.rdram'))
        raw=raw_path.read_bytes();raw=bytes(raw[i^3] for i in range(len(raw)))
        wanted=min(len(raw),(96-y)*96-x)
        matches=[rid for rid,d in resources.items() if d[8+y*96+x:8+y*96+x+wanted]==raw[:wanted]]
        if len(matches)!=1:continue
        rid=matches[0]
        pal_path=path.with_name(path.name.replace('.rice.json','.rice.palette.rdram'))
        pal=pal_path.read_bytes();pal=bytes(pal[i^3] for i in range(len(pal)))[:512]
        key=f'{rid}-{hashlib.sha256(pal).hexdigest()[:12]}'
        if key not in found:
            image=indexed(resources[rid][8:],(96,96),rgba16(pal),8)
            image.save(args.output/(key+'.png'))
            found[key]={'resource_id':rid,'image':key+'.png','palette_hex':pal.hex(),'palette_source':str(pal_path.resolve()),'bindings':[]}
        draw=json.loads(path.with_name(path.name.replace('.rice.json','.tile.json')).read_text())
        found[key]['bindings'].append({'hash':path.name.split('.')[0],'xy':[x,y],'draw_size':[draw['width'],draw['height']],'load_address':t['address'],'pixel_bytes_equal':True})
    rows=list(found.values());w=6*170;h=((len(rows)+5)//6)*195
    sheet=Image.new('RGB',(w,max(h,1)),(43,49,52));draw=ImageDraw.Draw(sheet)
    for i,r in enumerate(rows):
        im=Image.open(args.output/r['image']).resize((144,144),Image.Resampling.NEAREST);x=(i%6)*170;y=(i//6)*195
        sheet.paste(im,(x+10,y+8),im);draw.text((x+8,y+156),str(r['resource_id'])+' / '+str(len(r['bindings']))+' tiles',fill='white')
    sheet.save(args.output/'contact-sheet.png')
    (args.output/'portraits.json').write_text(json.dumps({'schema':'srw64.stage1-portrait-discovery.v1','textures':str(args.textures.resolve()),'portraits':rows},indent=2)+'\n')
    print({'source_resources':len(resources),'observed_images':len(rows),'ids':[r['resource_id'] for r in rows]})
if __name__=='__main__':main()
