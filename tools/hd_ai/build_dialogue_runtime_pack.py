"""Bind a frozen resource-derived HD Europe map and name styling to live RT64."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image
from srw64_rom.resources import ResourceTable
from tools.hd_ai.rt64_hash import ROOT, hasher, map_hash
from tools.hd_ai.extract_samples import map_image


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frame', action='store_true', help='also rebuild the original dialogue border slices')
    args=parser.parse_args()
    base=ROOT/'build/hd-ai/dialogue-type/medium-pack/pack'
    source=ROOT/'build/hd-ai/2026-09-08/map-probe-final'
    capture=ROOT/'build/hd-ai/dialogue-type/live-medium-4x'
    assert sha(base/'rt64.json')=='bf2164ecc5af9fccefa91ee262ffca8165dd8b745dcb9eb9916d7fa22b08cfb1'
    assert sha(capture/'latest-gfx-rdram.bin')=='237f1b494dd2a35e7a54348ab98928323905f582bafbf4b7446477d1d632ceb5'
    report=json.loads((source/'build.json').read_text())
    assert sha(source/'protected-map.png')==report['protected_map_sha256']
    assert report['generation_sha256']=='5636c2f878b590b7036a7900294853557f73d03713338b2be1a9fc481000aa0d'
    rom=ROOT/'rom.z64'
    assert sha(rom)=='ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e'
    decoded=ResourceTable(rom.read_bytes()).extract(5604)[0]
    ram=(capture/'latest-gfx-rdram.bin').read_bytes()
    address=ram.find(decoded)
    if address<0 or ram.find(decoded,address+1)>=0:
        raise RuntimeError('Resource 5604 must match the live capture uniquely and completely')
    _, layout=map_image(decoded)
    xxh=hasher()
    actual_hashes={}
    for record in layout['tiles']:
        t,p=record['texture_offset'],record['palette_offset']
        actual_hashes[map_hash(decoded[t:t+2048],decoded[p:p+32],xxh)]=record
    args.output.mkdir(parents=True,exist_ok=False)
    pack=args.output/'pack'
    shutil.copytree(base,pack)
    db=json.loads((pack/'rt64.json').read_text())
    entries={item['hashes']['rt64']:item for item in db['textures']}
    # Identical source tiles can appear at different atlas positions. A single
    # RT64 hash cannot select context-dependent HD pixels for those positions.
    conflicts=set(json.loads((base.parent/'report.json').read_text())['map_hash_context_conflicts_kept_original'])
    replacements=[]
    excluded=[]
    for tile in report['tiles']:
        digest=tile['hash']
        assert digest in actual_hashes
        if digest in conflicts:
            excluded.append(digest)
            continue
        asset=source/'hd-pack-stall'/f'{digest}.png'
        assert sha(asset)==tile['replacement_sha256']
        image=Image.open(asset)
        assert image.mode=='RGBA' and image.size==(256,256)
        filename=f'map-resource5604-hd-{digest}.png'
        shutil.copy2(asset,pack/filename)
        old=entries.get(digest)
        if old:
            old['path']=filename
        else:
            entry={'hashes':{'rt64':digest},'path':filename}
            db['textures'].append(entry);entries[digest]=entry
        replacements.append({'rt64_hash':digest,'texture_offset':actual_hashes[digest]['texture_offset'],
                             'source_xy':tile['source_xy'],'path':filename,'sha256':sha(pack/filename)})
    assert len(replacements)==57 and excluded==['b820c653dd7fc5ec']
    frame=None
    if args.frame:
        from tools.hd_ai.dialogue_frame_asset import build_frame
        frame=build_frame(pack,args.output,capture)
        for item in frame['textures']:
            assert item['hashes']['rt64'] not in entries
            entry={'hashes':item['hashes'],'path':item['path']}
            db['textures'].append(entry);entries[item['hashes']['rt64']]=entry
    assert len(entries)==len(db['textures'])
    (pack/'rt64.json').write_text(json.dumps(db,indent=2)+'\n')
    (pack/'srw64-dialogue-name-blue-v1').write_text('Opening dialogue name rows: RGB #69BFFF, source glyph alpha; restore RDP state after each draw.\n')
    result={'schema':'srw64.dialogue-runtime-pack.v1','resource_id':5604,
        'source_rom_sha256':sha(rom),'decoded_sha256':hashlib.sha256(decoded).hexdigest(),
        'decoded_size':len(decoded),'exact_live_rdram_address':address,
        'capture_rdram_sha256':sha(capture/'latest-gfx-rdram.bin'),
        'base_manifest_sha256':sha(base/'rt64.json'),
        'map_source_report_sha256':sha(source/'build.json'),
        'frozen_generation_sha256':report['generation_sha256'],
        'map_pixels':'Frozen resource-derived HD candidate; original sea/coast margin and palette alpha retained',
        'scope':'57 resource-5604 tiles in the previously verified 512x512 crop; ambiguous shared hashes stay original; other map tiles keep the existing pack',
        'ambiguous_shared_hashes_excluded':excluded,
        'frame':frame,
        'name_color':'#69BFFF; native RDP combiner with unchanged glyph coverage',
        'replacements':replacements,'total_textures':len(db['textures']),
        'files_sha256':{p.name:sha(p) for p in sorted(pack.iterdir()) if p.is_file()}}
    (args.output/'build.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('resource_id','decoded_size','exact_live_rdram_address','total_textures','scope')},indent=2))


if __name__=='__main__':
    main()
