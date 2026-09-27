"""Register imagegen's separate colony layers to the original sprite coordinates.

Artwork comes from imagegen. This only resamples and composites the layers. The
same ring pixels are reused in all eight frames, so its shape cannot wobble.
"""
import argparse
import json
from pathlib import Path
from PIL import Image
from tools.hd_ai.tactical_colony_pack import sha

SIZE=(256,192)


def solid(image):
    return image.getchannel('A').point(lambda x:255 if x>=128 else 0)


def ring_layer(image):
    box=solid(image).getbbox()
    if box is None: raise ValueError('Empty ring')
    ring=Image.new('RGBA',SIZE)
    ring.paste(image.crop(box).resize((84,172),Image.Resampling.LANCZOS),(16,8))
    return ring,box


def body_layer(image):
    mask=solid(image);left,top,right,bottom=mask.getbbox()
    # Original body spans x=9..62. Anchor to the far endcap, not the
    # x=55..59 strip: a generated rotating panel can enter that strip.
    # Preserve aspect ratio so a changing silhouette cannot stretch a phase.
    xscale=(right-left)/212
    sx0=round(left+200*xscale);sx1=round(left+208*xscale)
    core=mask.crop((sx0,0,sx1,image.height)).getbbox()
    if not core: raise ValueError('Missing endcap anchor')
    center_y=(core[1]+core[3])/2
    transform=(xscale,0,left-36*xscale,0,xscale,center_y-96*xscale)
    frame=image.transform(SIZE,Image.Transform.AFFINE,transform,Image.Resampling.BICUBIC)
    # Keep a two-pixel antialias margin around the registered body's horizontal
    # bounds. Low-alpha imagegen specks outside this extent must not tint the ring.
    clipped=Image.new('RGBA',SIZE)
    clipped.paste(frame.crop((34,0,250,192)),(34,0))
    frame=clipped
    bounds=solid(frame).getbbox()
    if bounds[0]<=0 or bounds[1]<=0 or bounds[2]>=256 or bounds[3]>=192:
        raise ValueError(f'Registered body clips the frame: {bounds}')
    return frame,{'generated_bounds':[left,top,right,bottom],'generated_core_strip':[sx0,core[1],sx1,core[3]],
                  'output_to_generated_affine':transform,'registered_bounds':bounds}


def build(source,out):
    out.mkdir(parents=True,exist_ok=False)
    ring,box=ring_layer(Image.open(source/'ring-generated.png').convert('RGBA'))
    ring.save(out/'ring.png');rows=[]
    sheet=Image.new('RGBA',(1216,448))
    frames=[]
    for i in range(8):
        path=source/f'body-{i:02d}-generated.png'
        body,fit=body_layer(Image.open(path).convert('RGBA'))
        body.save(out/f'body-{i:02d}.png')
        frame=Image.alpha_composite(ring,body)
        frame.save(out/f'frame-{i:02d}.png');frames.append(frame)
        sheet.paste(frame,((i%4)*320,(i//4)*256))
        rows.append({'frame':i,'file':f'frame-{i:02d}.png','sha256':sha(out/f'frame-{i:02d}.png'),
                     'body_generated_sha256':sha(path),'registration':fit,'ring_sha256':sha(out/'ring.png')})
    # The leftmost ring strip has no rotating parts in the original geometry.
    for frame in frames[1:]:
        if frames[0].crop((0,0,32,192)).tobytes()!=frame.crop((0,0,32,192)).tobytes():
            raise AssertionError('Stationary ring pixels changed')
    sheet.save(out/'sheet.png')
    report={'schema':'srw64.tactical-colony-frames.v1','resource':6235,'palette':6258,'size':list(SIZE),'scale':4,
            'ticks_per_frame':27,'frames':rows,'shared_ring':{'generated_sha256':sha(source/'ring-generated.png'),
            'generated_box':box,'target_box':[16,8,100,180],'sha256':sha(out/'ring.png')},
            'shared_ring_exact':True,'stationary_left_strip_exact':True,'runtime_integrated':False,
            'animation_acceptance_proven':False,'sheet_sha256':sha(out/'sheet.png')}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=build(a.source,a.output);print(json.dumps({'frames':len(r['frames']),'shared_ring_exact':r['shared_ring_exact']}))
