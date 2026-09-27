"""Read-only geometry/colour diagnostics for generated tactical-map textures.

This writes JSON only. Local correlation is a diagnostic, not a visual or runtime
acceptance test. Dynamic palette pixels and rotating-colony cells are excluded.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat

from tools.content.map_dynamics import COLONY_ATLAS, COLONY_RECT, cell_grid
from tools.hd_ai.tactical_map_hd import load_map, masks

ROOT = Path(__file__).resolve().parents[2]


def normalized(values):
    mean = sum(values) / len(values)
    centered = [v - mean for v in values]
    norm = math.sqrt(sum(v * v for v in centered))
    return centered, norm


def local_shift(reference, candidate, box, radius=12):
    """Locate a source patch in the candidate, in original source pixels.

    Positive dx/dy means the feature lies farther right/down in the candidate.
    Search exceeds the 8-source-pixel half-cell redraw threshold. A best match
    at the search boundary is inconclusive, not a bounded displacement proof.
    """
    x0, y0, x1, y1 = box
    ref = reference.crop(box).resize((16, 16), Image.Resampling.BOX)
    vector, norm = normalized(ref.tobytes())
    if norm / 16 < 2.5:
        return {"status": "insufficient_texture"}
    scores = []
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            moved = candidate.crop((x0+dx, y0+dy, x1+dx, y1+dy)).resize((16,16), Image.Resampling.BOX)
            other, other_norm = normalized(moved.tobytes())
            score = sum(a*b for a,b in zip(vector,other))/(norm*other_norm) if other_norm else -1
            scores.append((score,dx,dy))
    score,dx,dy = max(scores)
    alternative = max(s for s,x,y in scores if max(abs(x-dx),abs(y-dy))>2)
    margin = score-alternative
    boundary = max(abs(dx),abs(dy)) == radius
    reliable = score >= .7 and margin >= .025 and not boundary
    distance = math.hypot(dx,dy)
    status = "suspect_shift_over_half_cell" if reliable and distance > 8 else "sample_within_half_cell" if reliable else "ambiguous_match"
    return {"status":status,"dx_source_px":dx,"dy_source_px":dy,
            "distance_source_px":round(distance,3),"correlation":round(score,4),
            "distinct_peak_margin":round(margin,4),"search_boundary":boundary}


def protected_mask(data):
    animated,border = masks(data,data['indices'])
    protected = ImageChops.lighter(animated,border)
    if data['mode']==1 and data['atlas']==COLONY_ATLAS:
        (w,_),cells = cell_grid(data['layout_bytes'])
        columns = w//2
        draw = ImageDraw.Draw(protected)
        left,top,right,bottom = COLONY_RECT
        for i,(_,tile) in enumerate(cells):
            sx = (tile & 15)*8 + ((tile & 0x300)>>1)
            sy = ((tile & 0xf0)>>1) + ((tile & 0xc00)>>3)
            if left <= sx < right and top <= sy < bottom:
                x,y = i%columns*16,i//columns*16
                draw.rectangle((x,y,x+15,y+15),fill=255)
    return protected


def diagnose(source,generated,protected,origin=(0,0),grid=4,search_radius=12):
    """Compare on an analytical 1x copy; never modify the source or asset files."""
    candidate = generated.convert('RGB').resize(source.size,Image.Resampling.BOX)
    reference = source.convert('RGB')
    ref_gray = reference.convert('L').filter(ImageFilter.GaussianBlur(1))
    cand_gray = candidate.convert('L').filter(ImageFilter.GaussianBlur(1))
    width,height = reference.size
    samples=[]
    half=min(32,max(12,min(width,height)//12)); radius=search_radius
    for gy in range(grid):
        for gx in range(grid):
            cx=round(half+radius+(width-2*(half+radius))*gx/(grid-1))
            cy=round(half+radius+(height-2*(half+radius))*gy/(grid-1))
            box=(cx-half,cy-half,cx+half,cy+half)
            fraction=ImageStat.Stat(protected.crop(box)).mean[0]/255
            result={"center_source_px":[cx+origin[0],cy+origin[1]],"protected_fraction":round(fraction,4)}
            if fraction > .01:
                result['status']='excluded_dynamic_or_border'
            else:
                result.update(local_shift(ref_gray,cand_gray,box,radius))
            samples.append(result)
    cells=[]
    for y in range(0,height,16):
        for x in range(0,width,16):
            box=(x,y,min(x+16,width),min(y+16,height))
            if ImageStat.Stat(protected.crop(box)).mean[0] > 0:
                continue
            a=ImageStat.Stat(reference.crop(box)).mean
            b=ImageStat.Stat(candidate.crop(box)).mean
            delta=[b[i]-a[i] for i in range(3)]
            cells.append({'cell':[(origin[0]+x)//16,(origin[1]+y)//16],
                          'source_mean_rgb':[round(v,2) for v in a],
                          'generated_mean_rgb':[round(v,2) for v in b],
                          'mean_abs_rgb_delta':round(sum(abs(v) for v in delta)/3,3)})
    reliable=[s for s in samples if s['status'] in ('sample_within_half_cell','suspect_shift_over_half_cell')]
    suspects=[s for s in samples if s['status']=='suspect_shift_over_half_cell']
    return {'status':'needs_visual_registration_review','acceptance_proven':False,
            'reliable_samples':len(reliable),'total_samples':len(samples),
            'suspect_samples':len(suspects),'max_reliable_shift_source_px':max((s['distance_source_px'] for s in reliable),default=None),
            'samples':samples,'static_cells_compared':len(cells),
            'worst_colour_cells':sorted(cells,key=lambda c:c['mean_abs_rgb_delta'],reverse=True)[:20],
            'protected_fraction':round(ImageStat.Stat(protected).mean[0]/255,5)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kit',type=Path,default=ROOT/'assets/hd-ai/tactical-kit')
    parser.add_argument('--rom',type=Path,default=ROOT/'rom.z64')
    args=parser.parse_args()
    manifest=json.loads((args.kit/'manifest.json').read_text())
    rom=args.rom.read_bytes()
    rows=[]
    for m in manifest['maps']:
        jobs=[{**m['whole'],'id':f"map-{m['map']:03d}-00-whole",'box':m['paint_box']},*m['windows']]
        available=[j for j in jobs if (args.kit/j['output']).is_file()]
        if not available: continue
        data=load_map(rom,m['map'])
        protected=protected_mask(data)
        source=Image.open(args.kit/m['source']).convert('RGB')
        for j in available:
            generated=Image.open(args.kit/j['output']).convert('RGB')
            result=diagnose(source.crop(j['box']),generated,protected.crop(j['box']),j['box'][:2])
            result.update(id=j['id'],output=j['output'],sha256=hashlib.sha256((args.kit/j['output']).read_bytes()).hexdigest())
            rows.append(result)
            print(j['id'],f"reliable={result['reliable_samples']}/16",f"suspect={result['suspect_samples']}",flush=True)
    report={'schema':'srw64.tactical-kit.geometry-diagnostics.v1',
            'method':'1x grayscale local normalized correlation, 16 sample patches, +/-12 source-pixel search; source palette cycles/borders/colonies excluded',
            'limitations':['Sparse local samples do not certify every cell or feature.','Changed painted texture can make correlation ambiguous.','Cell mean colour differences are not displacement measurements.','No asset alignment/composition or runtime validation has been performed.'],
            'assets_checked':len(rows),'rows':rows}
    path=args.kit/'outputs/geometry-audit.json'
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(path)


if __name__=='__main__': main()
