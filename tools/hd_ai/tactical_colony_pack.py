"""Split the generated colony sheet using the kit's explicit gutter/frame boxes.

This only packages the eight generated frames, preserving RGB or RGBA. Runtime
overlay/masking and animation acceptance are separate work.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

from PIL import Image
from srw64_rom.resources import ResourceTable

from tools.content.map_dynamics import COLONY_ATLAS, COLONY_RECT, cell_grid
from tools.hd_ai.tactical_map_hd import ROOT, ROM_SHA256, load_map
from tools.content.map_dynamics import index_atlas
from tools.hd_ai.pixel_scale import magnify


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def frame_box(box, source_size, generated_size):
    return tuple(round(v*generated_size[i%2]/source_size[i%2]) for i,v in enumerate(box))


def colony_cells(data):
    """Record each dynamic atlas cell, including flips; never guess instance bounds."""
    if data['atlas']!=COLONY_ATLAS or data['mode']!=1:
        return []
    (width,_),cells=cell_grid(data['layout_bytes'])
    columns=width//2;left,top,right,bottom=COLONY_RECT
    result=[]
    for i,(flags,tile) in enumerate(cells):
        sx=(tile&15)*8+((tile&0x300)>>1)
        sy=((tile&0xf0)>>1)+((tile&0xc00)>>3)
        if left<=sx<right and top<=sy<bottom:
            result.append({'map_xy':[i%columns*16,i//columns*16],
                           'frame_xy':[sx-left,sy-top],'flip_x':bool(flags&0x40),'flip_y':bool(flags&0x80)})
    return result


def colony_instances(data):
    cells = colony_cells(data)
    groups = {}
    for cell in cells:
        origin = tuple(a-b for a,b in zip(cell['map_xy'],cell['frame_xy']))
        groups.setdefault(origin, []).append(cell)
    expected = {(x,y) for x in range(0,64,16) for y in range(0,48,16)}
    for origin, parts in groups.items():
        if len(parts) != 12 or {tuple(c['frame_xy']) for c in parts} != expected or any(c['flip_x'] or c['flip_y'] for c in parts):
            raise ValueError(f'Incomplete or flipped colony block at {origin}')
    return [list(xy) for xy in sorted(groups)]


def runtime_frames(rom, frames, output):
    """Package generated alpha frames with the ROM's frame-specific palette indices."""
    data = load_map(rom, 87)  # Validates the pinned ROM and obtains palette 6258.
    manifest = json.loads((frames/'manifest.json').read_text())
    if manifest['resource'] != 6235 or manifest['palette'] != data['palette_id'] or len(manifest['frames']) != 8:
        raise ValueError('Colony frame manifest differs from ROM resources')
    atlas = index_atlas(ResourceTable(rom).extract(6235)[0])
    if atlas.size != (512,48):
        raise ValueError('Colony frame strip has unexpected dimensions')
    output.mkdir(parents=True,exist_ok=False)
    for i,row in enumerate(manifest['frames']):
        source = frames/row['file']
        if row['frame'] != i or sha(source) != row['sha256']:
            raise ValueError('Colony frame hash or order changed')
        image = Image.open(source)
        if image.mode != 'RGBA' or image.size != (256,192) or image.getextrema()[3][0] != 0:
            raise ValueError('Expected a transparent 256x192 colony frame')
        folder = output/f'frame-{i:02d}';folder.mkdir()
        shutil.copyfile(source,folder/'base.png')
        magnify(atlas.crop((i*64,0,(i+1)*64,48)),data['palette'],4).save(folder/'index.png')
        meta = {'schema':'srw64.hd-map-runtime.v0','layout':0,'width':64,'height':48,'scale':4,'alpha':True,
                'frame':i,'resource':6235,'palette':6258,'reference_palette':data['palette'],
                'rom_sha256':ROM_SHA256,'files':{n:sha(folder/n) for n in ('base.png','index.png')}}
        (folder/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
    (output/'manifest.json').write_text(json.dumps({'schema':'srw64.hd-colony-runtime.v1',
        'source_manifest_sha256':sha(frames/'manifest.json'),'frames':8,'frame_counter':'0x80178C6D',
        'ticks_per_frame':27,'runtime_verified':False},indent=2)+'\n')


def export_layouts(rom,kit,path):
    manifest=json.loads((kit/'manifest.json').read_text())
    rows=[]
    for spec in manifest['maps']:
        if spec['family']!='space':
            continue
        data=load_map(rom,spec['map']);cells=colony_cells(data)
        if not cells:
            continue
        groups={}
        for cell in cells:
            origin=tuple(a-b for a,b in zip(cell['map_xy'],cell['frame_xy']))
            groups.setdefault(origin,[]).append(cell)
        expected={(x,y) for x in range(0,64,16) for y in range(0,48,16)}
        instances=[]
        for origin,parts in groups.items():
            verified=len(parts)==12 and {tuple(c['frame_xy']) for c in parts}==expected and not any(c['flip_x'] or c['flip_y'] for c in parts)
            instances.append({'map_xy':list(origin),'size':[64,48],'complete_unflipped_block':verified,'cell_count':len(parts)})
        rows.append({'map':spec['map'],'layout':data['layout'],'mode':data['mode'],'cells':cells,'instances':instances})
    report={'schema':'srw64.tactical-colony-layouts.v1','rom_sha256':ROM_SHA256,'ticks_per_frame':27,
            'frame_counter_address':'0x80178C6D','maps':rows,'runtime_integrated':False}
    with path.open('x') as stream:
        stream.write(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    return report


def pack(kit, output, sheet=None):
    colony=json.loads((kit/'manifest.json').read_text())['colony']
    if sha(kit/colony['input'])!=colony['input_sha256']:
        raise ValueError('colony input changed')
    records=json.loads((kit/'outputs/generation-records.json').read_text())['records']
    record=next(r for r in reversed(records) if r['id']=='colony-sheet' and '/archive/' not in r['output'])
    canonical=kit/colony['output']
    if sha(canonical)!=record['sha256']:
        raise ValueError('colony generation record is stale')
    generated_path=sheet or canonical
    source=Image.open(kit/colony['source'])
    generated=Image.open(generated_path)
    transparent=generated.mode=='RGBA'
    generated=generated.convert('RGBA' if transparent else 'RGB')
    aspect_error=abs((generated.width/generated.height)/(source.width/source.height)-1)
    if aspect_error>.005 or len(colony['frames'])!=8:
        raise ValueError('sheet aspect ratio or frame count differs from manifest')
    output.mkdir(parents=True,exist_ok=False)
    rows=[]
    target=tuple(v*4 for v in colony['frame'])
    for i,box in enumerate(colony['frames']):
        pixel_box=frame_box(box,source.size,generated.size)
        if not (0<=pixel_box[0]<pixel_box[2]<=generated.width and 0<=pixel_box[1]<pixel_box[3]<=generated.height):
            raise ValueError(f'frame {i} is out of bounds')
        image=generated.crop(pixel_box).resize(target,Image.Resampling.LANCZOS)
        name=f'frame-{i:02d}.png';image.save(output/name)
        rows.append({'frame':i,'source_box':box,'generated_box':pixel_box,'file':name,'sha256':sha(output/name)})
    meta={'schema':'srw64.tactical-colony-frames.v1','resource':colony['resource'],'palette':colony['palette'],
          'sheet_sha256':sha(generated_path),'source_sheet_sha256':sha(kit/colony['source']),
          'canonical_generation_sha256':record['sha256'],
          'size':list(target),'scale':4,'ticks_per_frame':27,'frames':rows,'background':'generated alpha preserved' if transparent else 'opaque black retained',
          'runtime_integrated':False,'animation_acceptance_proven':False}
    (output/'manifest.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    return meta


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kit',type=Path,default=ROOT/'assets/hd-ai/tactical-kit')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sheet',type=Path,help='Optional derived sheet, e.g. imagegen transparent extraction')
    args=parser.parse_args()
    result=pack(args.kit,args.output,args.sheet)
    print(json.dumps({'output':str(args.output),'frames':len(result['frames']),'size':result['size'],'runtime_integrated':False}))


if __name__=='__main__':
    main()
