"""Export the approved-family candidates into one hash-verified runtime directory.

Each map gets its own directory for future evidence; immutable artwork files
are linked to their recorded source. No existing staging file is overwritten.
"""
import argparse
import json
import os
from pathlib import Path
from PIL import Image,ImageChops
from tools.hd_ai.tactical_kit_compose import sha
from tools.hd_ai.tactical_colony_pack import colony_instances
from tools.hd_ai.tactical_map_hd import ROOT,ROM_SHA256,load_map,render


def build(kit,registration,colony_runtime,output,rom_path):
    manifest=json.loads((kit/'manifest.json').read_text())
    styles=json.loads((kit/'style/manifest.json').read_text())
    approved={r['family'] for r in styles['references'] if r.get('user_approved') and sha(kit/r['path'])==r['sha256']}
    specs=[m for m in manifest['maps'] if m['family'] in approved]
    rom=rom_path.read_bytes()
    if sha(rom_path)!=ROM_SHA256:raise ValueError('Unrecognized ROM')
    output.mkdir(parents=True,exist_ok=False)
    rows=[]
    for m in specs:
        name=f"map-{m['map']:03d}";data=load_map(rom,m['map']);instances=colony_instances(data)
        source=next((p/name for p in [registration,colony_runtime,kit/'composed/preview-v1'] if (p/name/'meta.json').is_file()),None)
        if source is None:raise ValueError(f'Missing map: {name}')
        meta=json.loads((source/'meta.json').read_text())
        if (meta['map']!=m['map'] or meta['rom_sha256']!=ROM_SHA256 or
            meta['layout']!=data['layout'] or meta['atlas']!=data['atlas'] or meta['palette']!=data['palette_id'] or
            meta['reference_palette']!=[list(c) for c in data['palette']]):
            raise ValueError(f'Metadata does not match ROM: {name}')
        if meta.get('colony_instances',[])!=instances or (instances and not meta.get('colony_animation_integrated')):
            raise ValueError(f'Missing or incorrect dynamic colony integration: {name}')
        for filename,digest in meta['files'].items():
            if sha(source/filename)!=digest:raise ValueError(f'Changed asset: {source/filename}')
        with Image.open(source/'base.png') as base,Image.open(source/'index.png') as index,Image.open(source/'protected.png') as protected:
            expected_size=(meta['width']*meta['scale'],meta['height']*meta['scale'])
            if base.size!=expected_size or index.size!=expected_size or protected.size!=expected_size or data['indices'].size!=(meta['width'],meta['height']):
                raise ValueError(f'Incorrect dimensions: {name}')
            difference=ImageChops.difference(base.convert('RGB'),render(index,data['palette']))
            if any(ImageChops.multiply(c,protected.convert('L')).getbbox() for c in difference.split()):
                raise ValueError(f'Protected palette pixels differ: {name}')
            for x,y in instances:
                box=(x*4,y*4,(x+64)*4,(y+48)*4)
                if index.crop(box).getextrema()!=(meta['colony_background_index'],meta['colony_background_index']):
                    raise ValueError(f'Static colony index residue: {name}')
        folder=output/name;folder.mkdir()
        for filename in ['base.png','index.png','protected.png','meta.json','composition.json','final-geometry.json']:
            if (source/filename).is_file():(folder/filename).symlink_to(os.path.relpath(source/filename,folder))
        row={'map':m['map'],'family':m['family'],'source':str(source.resolve()),
             'files':dict(meta['files'],**{'meta.json':sha(source/'meta.json')}),
             'protected_pixels_exact':True,'colony_instances':len(instances),'runtime_verified':False,'visual_acceptance_proven':False}
        rows.append(row)
        print(name,'exported',flush=True)
    frames=colony_runtime/'colony'
    frame_rows=[]
    for number in range(8):
        folder=frames/f'frame-{number:02d}';meta=json.loads((folder/'meta.json').read_text())
        for filename,digest in meta['files'].items():
            if sha(folder/filename)!=digest:raise ValueError(f'Changed frame: {folder/filename}')
        frame_rows.append({'frame':number,'files':dict(meta['files'],**{'meta.json':sha(folder/'meta.json')})})
    (output/'colony').symlink_to(os.path.relpath(frames,output),target_is_directory=True)
    result={'schema':'srw64.tactical-runtime-bundle.v1','maps':len(rows),'approved_families':sorted(approved),
            'excluded_family_maps':[m['map'] for m in manifest['maps'] if m['family'] not in approved],
            'colony_maps':sum(bool(r['colony_instances']) for r in rows),'colony_instances':sum(r['colony_instances'] for r in rows),
            'colony_frame_source':str(frames.resolve()),'frames':frame_rows,'rows':rows,
            'full_acceptance_proven':False,'runtime_verified':False}
    (output/'bundle.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    return {k:result[k] for k in ('maps','colony_maps','colony_instances','excluded_family_maps')}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kit',type=Path,default=ROOT/'assets/hd-ai/tactical-kit')
    p.add_argument('--registration',type=Path,required=True)
    p.add_argument('--colony-runtime',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--rom',type=Path,default=ROOT/'rom.z64')
    a=p.parse_args();print(json.dumps(build(a.kit,a.registration,a.colony_runtime,a.output,a.rom)))
