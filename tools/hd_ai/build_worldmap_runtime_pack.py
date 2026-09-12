"""Bind an AI-redrawn resource-5604 world map to the real dialogue map mesh."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageChops, ImageFilter
from srw64_rom.resources import ResourceTable
from tools.hd_ai.extract_samples import ROOT, map_image
from tools.hd_ai.rt64_hash import hasher, map_hash
from tools.hd_ai.build_map_probe import water_mask


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ai', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--detail', type=Path, help='higher-density resource crop and its manifest')
    parser.add_argument('--fonts', type=Path, help='verified replacement font pack')
    parser.add_argument('--scale', type=int, choices=(4,8), default=8, help='runtime texture density; independent of original ROM texture size')
    args = parser.parse_args()
    scale = args.scale
    base = ROOT/'build/hd-ai/dialogue-runtime/v3/pack'
    capture = ROOT/'build/hd-ai/dialogue-type/live-medium-4x'
    meta = json.loads((args.ai/'manifest.json').read_text())
    assert sha(args.ai/'map-ai.png') == meta['asset_sha256']
    assert sha(args.ai/'source-map.png') == meta['source_map_sha256']
    assert sha(args.ai/'prompt.txt') == meta['prompt_sha256']
    assert sha(base/'rt64.json') == '97b08e976acff666a13f16af64da254eecdbc41968270c1f075f14d31df39724'
    rom = ROOT/'rom.z64'
    assert sha(rom) == 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e'
    decoded = ResourceTable(rom.read_bytes()).extract(5604)[0]
    ram = (capture/'latest-gfx-rdram.bin').read_bytes()
    address = ram.find(decoded)
    assert address >= 0 and ram.find(decoded,address+1) < 0
    native, layout = map_image(decoded)
    source = native.transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB')
    assert source.tobytes() == Image.open(args.ai/'source-map.png').convert('RGB').tobytes()
    assert source.size == (512,512)
    generated = Image.open(args.ai/'map-ai.png').convert('RGB')
    assert generated.width == generated.height and generated.width >= 1024
    high = generated.resize((512*scale,512*scale),Image.Resampling.LANCZOS)
    detail_report = None
    if args.detail:
        detail_report = json.loads((args.detail/'manifest.json').read_text())
        assert sha(args.detail/'map-ai.png') == detail_report['asset_sha256']
        assert sha(args.detail/'source-crop.png') == detail_report['source_crop_sha256']
        assert sha(args.detail/'prompt.txt') == detail_report['prompt_sha256']
        box = detail_report['source_crop_xyxy']
        assert source.crop(box).tobytes() == Image.open(args.detail/'source-crop.png').convert('RGB').tobytes()
        patch = Image.open(args.detail/'map-ai.png').convert('RGB')
        width,height = box[2]-box[0],box[3]-box[1]
        assert patch.width >= width*4 and patch.height >= height*4
        patch = patch.resize((width*scale,height*scale),Image.Resampling.LANCZOS)
        feather = Image.frombytes('L',patch.size,bytes(
            min(255,round(min(x,y,patch.width-1-x,patch.height-1-y)*255/(8*scale)))
            for y in range(patch.height) for x in range(patch.width)))
        high.paste(patch,(box[0]*scale,box[1]*scale),feather)
    baseline = source.resize(high.size,Image.Resampling.BILINEAR)
    # Preserve the original coast, ocean, alpha and the crop boundary. Masking
    # happens on the assembled map before slicing; it cannot introduce tile seams.
    interior = ImageChops.invert(water_mask(source)).filter(ImageFilter.MinFilter(3))
    pixels = interior.load()
    for y in range(512):
        for x in range(512):
            pixels[x,y] = min(pixels[x,y], max(0,min(255,min(x,y,511-x,511-y)*85)))
    allowed = interior.resize(high.size,Image.Resampling.NEAREST)
    mask = ImageChops.multiply(allowed,interior.resize(high.size,Image.Resampling.BILINEAR))
    # A generated sea pixel must never paint a new lake into source land.
    candidate_land = ImageChops.invert(water_mask(high)).filter(ImageFilter.MinFilter(3))
    mask = ImageChops.multiply(mask,candidate_land)
    protected = ImageChops.invert(mask.point(lambda v:255 if v else 0))
    mapped = Image.composite(high,baseline,mask)
    difference = ImageChops.difference(mapped,baseline)
    assert all(not ImageChops.multiply(channel,protected).getbbox() for channel in difference.split())
    texture_atlas = mapped.transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGBA')
    alpha = native.getchannel('A').resize(high.size,Image.Resampling.BILINEAR)
    texture_atlas.putalpha(alpha)
    prior = json.loads((base.parent/'build.json').read_text())
    hashes = {r['rt64_hash']:r for r in prior['replacements']}
    xxh = hasher(); verified = {}
    x0,y0,_,_ = layout['crop']
    for record in layout['tiles']:
        t,p = record['texture_offset'],record['palette_offset']
        digest = map_hash(decoded[t:t+2048],decoded[p:p+32],xxh)
        if digest in hashes:
            x,y = record['xy']; xy=[x-x0,y-y0]
            assert xy == hashes[digest]['source_xy']
            assert digest not in verified
            verified[digest] = {**hashes[digest], 'palette_offset':p, 'display_list_offset':record['display_list_offset']}
    assert len(verified) == len(hashes) == 57
    args.output.mkdir(parents=True,exist_ok=False)
    pack = args.output/'pack'; shutil.copytree(base,pack)
    db = json.loads((pack/'rt64.json').read_text())
    entries = {entry['hashes']['rt64']:entry for entry in db['textures']}
    mapped.save(args.output/'protected-map.png')
    mask.save(args.output/'editable-mask.png')
    texture_atlas.save(args.output/'runtime-atlas.png')
    replacements=[]
    for digest,record in sorted(verified.items()):
        x,y = record['source_xy']; box=(x*scale,y*scale,(x+64)*scale,(y+64)*scale)
        tile = texture_atlas.crop(box)
        # Same source alpha as the previously accepted runtime map.
        previous = Image.open(base/entries[digest]['path']).convert('RGBA')
        if scale == 4:
            assert tile.getchannel('A').tobytes() == previous.getchannel('A').tobytes()
        else:
            assert tile.getchannel('A').tobytes() == alpha.crop(box).tobytes()
        name = f'worldmap-resource5604-{digest}.png';tile.save(pack/name)
        entries[digest]['path'] = name
        replacements.append({**record,'path':name,'sha256':sha(pack/name)})
    font_report = None; font_hashes = set()
    if args.fonts:
        font_report = json.loads((args.fonts/'report.json').read_text())
        assert font_report['font_name'] == ['PingFang SC','Medium']
        fonts_db = json.loads((args.fonts/'pack/rt64.json').read_text())
        font_hashes = {entry['hashes']['rt64'] for entry in fonts_db['textures']}
        previous_fonts = {digest for digest,entry in entries.items() if entry['path'].startswith('font-')}
        assert font_hashes == previous_fonts, 'Font coverage must equal the previous pack'
        for entry in fonts_db['textures']:
            digest = entry['hashes']['rt64'];name=f'pingfang-{digest}.png'
            shutil.copyfile(args.fonts/'pack'/entry['path'],pack/name)
            entries[digest]['path'] = name
    (pack/'rt64.json').write_text(json.dumps(db,indent=2)+'\n')
    (pack/'srw64-worldmap-hd.json').write_text(json.dumps({
        'schema':'srw64.worldmap-hd.v1','resource_id':5604,'source_tile_size':64,
        'replacement_tile_size':64*scale,'atlas_size':512*scale,
        'hashes':sorted(verified)},indent=2)+'\n')
    untouched = [item for item in db['textures'] if item['hashes']['rt64'] not in verified and item['hashes']['rt64'] not in font_hashes]
    assert all(sha(pack/item['path']) == sha(base/item['path']) for item in untouched)
    report = {'schema':'srw64.worldmap-runtime-pack.v1','resource_id':5604,
        'resource_decoded_sha256':hashlib.sha256(decoded).hexdigest(),
        'source_capture_sha256':sha(capture/'latest-gfx-rdram.bin'),
        'exact_source_rdram_address':address,'source_dimensions':[512,512],
        'ai_dimensions':list(generated.size),'runtime_atlas_dimensions':list(texture_atlas.size),
        'ai_manifest_sha256':sha(args.ai/'manifest.json'),'ai_asset_sha256':meta['asset_sha256'],
        'detail':detail_report,'font':{'name':font_report['font_name'],
            'file_sha256':font_report['font_sha256'],'report_sha256':sha(args.fonts/'report.json'),
            'replaced_texture_count':len(font_hashes)} if font_report else None,
        'base_manifest_sha256':sha(base/'rt64.json'),'coast_protection_native_pixels':1,
        'protected_region_changed_pixels':0,'alpha_source':'original palette alpha, globally bilinear scaled',
        'runtime_tile_dimensions':[64*scale,64*scale],'runtime_texture_scale':scale,
        'unchanged_texture_entries':len(untouched),'replacements':replacements,
        'total_textures':len(db['textures']),
        'excluded_ambiguous_hashes':prior['ambiguous_shared_hashes_excluded'],
        'scope':'Dialogue world-map resource, Europe crop; tactical map resources are not included',
        'pack_manifest_sha256':sha(pack/'rt64.json'),
        'builder_sha256':sha(Path(__file__)),
        'files_sha256':{p.name:sha(p) for p in sorted(pack.iterdir()) if p.is_file()}}
    (args.output/'build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('resource_id','unchanged_texture_entries','total_textures','pack_manifest_sha256')},indent=2))


if __name__ == '__main__':
    main()
