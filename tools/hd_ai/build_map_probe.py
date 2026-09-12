"""Build a capture-bound map replacement with deterministic coast protection."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter
from tools.hd_ai.extract_samples import ROOT, OUT, indexed, rgba16
from srw64_rom.resources import ResourceTable


def water_mask(image: Image.Image) -> Image.Image:
    return Image.frombytes('L',image.size,bytes(255 if b>r+20 and g>r+15 and b>g else 0
        for r,g,b in image.convert('RGB').get_flattened_data()))


def main() -> None:
    source=Image.open(OUT/'inputs/map-europe.png').convert('RGB')
    request=json.loads((OUT/'runs/map-europe--qwen-image-3.0-pro--1/request.json').read_text())
    assert request['status']=='completed'
    generated=Image.open(OUT/request['output']).convert('RGB')
    assert generated.size==(2048,2048) and source.size==(512,512)
    # Non-negative bilinear weights avoid the coastal ringing seen with Lanczos.
    baseline=source.resize(generated.size,Image.Resampling.BILINEAR)
    # Only interior land may take generated pixels; sea and a 3-native-pixel
    # margin remain byte-for-byte equal to the deterministic baseline.
    land=ImageChops.invert(water_mask(source))
    interior=land.filter(ImageFilter.MinFilter(7))
    mask=interior.resize(generated.size,Image.Resampling.NEAREST)
    softened=interior.resize(generated.size,Image.Resampling.LANCZOS).filter(ImageFilter.GaussianBlur(3))
    mask=ImageChops.multiply(mask,softened)
    hybrid=Image.composite(generated,baseline,mask)
    folder=OUT/'map-probe-final';folder.mkdir(exist_ok=True)
    baseline.save(folder/'bilinear.png');hybrid.save(folder/'protected-map.png');mask.save(folder/'editable-mask.png')
    protected=ImageChops.invert(mask.point(lambda x:255 if x else 0))
    diff=ImageChops.difference(hybrid,baseline)
    assert all(not ImageChops.multiply(channel,protected).getbbox() for channel in diff.split())
    binding=json.loads((OUT/'map-capture-binding.json').read_text())
    extraction=json.loads((OUT/'extraction.json').read_text())['maps'][0]['tiles']
    crop=extraction['crop'];assert crop[2]-crop[0]==crop[3]-crop[1]==512
    assert binding['decoded_matches_captured_rdram']
    data=ResourceTable((ROOT/'rom.z64').read_bytes()).extract(5604)[0]
    unflipped=hybrid.transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGBA')
    source_alpha=Image.open(OUT/'extract/map-5604.png').getchannel('A')
    alpha=source_alpha.resize(unflipped.size,Image.Resampling.BILINEAR)
    unflipped.putalpha(alpha)
    textures=[];tile_report=[]
    for variant in ('original-pack-stall','hd-pack-stall'):(folder/variant).mkdir(exist_ok=True)
    for item in binding['bindings']:
        assert item['pixel_bytes_equal'] and item['palette_equal']
        record=item['record'];x,y=record['xy'];x-=crop[0];y-=crop[1]
        if not(0<=x<512 and 0<=y<512):continue
        name=item['hash']+'.png';offset=record['texture_offset'];pal=record['palette_offset']
        original=indexed(data[offset:offset+2048],(64,64),rgba16(data[pal:pal+32]),4)
        original.save(folder/'original-pack-stall'/name)
        high=unflipped.crop((x*4,y*4,(x+64)*4,(y+64)*4)).convert('RGBA')
        assert not ImageChops.difference(high.getchannel('A'),alpha.crop((x*4,y*4,(x+64)*4,(y+64)*4))).getbbox()
        high.save(folder/'hd-pack-stall'/name)
        textures.append({'hashes':{'rt64':item['hash']},'path':name})
        tile_report.append({'hash':item['hash'],'source_xy':[x,y],'original_size':[64,64],'replacement_size':[256,256],
                            'replacement_sha256':hashlib.sha256((folder/'hd-pack-stall'/name).read_bytes()).hexdigest()})
    config={'configuration':{'autoPath':'rt64','configurationVersion':3,'hashVersion':5,
                            'defaultOperation':'stall','defaultShift':'none'},'textures':textures}
    for variant in ('original-pack-stall','hd-pack-stall'):
        (folder/variant/'rt64.json').write_text(json.dumps(config,indent=2)+'\n')
    result={'schema':'srw64.hd-map-probe.v1','evidence_scope':'capture-specific texture pack; live integration pending',
        'model':request['model'],'candidate':request['candidate'],'source_sha256':request['input_sha256'],
        'loading_operation':'stall for isolated replay; preload assertion recorded separately',
        'protected_baseline':'bilinear upscale; nonnegative weights avoid Lanczos ringing',
        'alpha_source':'original RGBA16 palette alpha, upscaled globally with bilinear; not AI output',
        'generation_sha256':request['output_sha256'],'protected_region_difference_pixels':0,
        'coast_protection_native_pixels':3,'modified_texture_count':len(textures),'tiles':tile_report,
        'protected_map_sha256':hashlib.sha256((folder/'protected-map.png').read_bytes()).hexdigest()}
    (folder/'build.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('modified_texture_count','protected_region_difference_pixels')}))


if __name__=='__main__': main()
