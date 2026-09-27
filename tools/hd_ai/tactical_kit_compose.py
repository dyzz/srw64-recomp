"""Deterministically register/assemble the imagegen kit into reviewable 4x assets.

This is the post-generation pipeline specified by tactical-kit/README.md, not
another image generator. Inputs are immutable. Outputs are staging assets only:
sparse registration never certifies every feature or the game runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageMath, ImageStat

from tools.hd_ai.pixel_scale import magnify
from tools.hd_ai.tactical_map_audit import diagnose, protected_mask
from tools.hd_ai.tactical_map_hd import ROOT, ROM_SHA256, load_map, masks, render
from tools.hd_ai.tactical_colony_pack import colony_instances, runtime_frames
from tools.hd_ai.tactical_registration_field import fit_field, place_field

SCALE = 4
COLOUR_RADIUS = 48
BASE_WEIGHT = .02


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def line(points):
    mx = sum(p for p, _ in points) / len(points)
    my = sum(q for _, q in points) / len(points)
    spread = sum((p-mx)**2 for p, _ in points)
    slope = sum((p-mx)*(q-my) for p, q in points) / spread if spread else 0
    return slope, my-slope*mx


def fit_samples(samples, box):
    """Source-coordinate -> normalized generated-coordinate scale and shift.

    Reject poorly distributed, inconsistent or excessive fits. Never treat an
    identity fallback as a successful registration. Exclude dynamic/ambiguous
    matches before fitting, and retain rejected points for the later diagnostic.
    """
    result = {'x': [1., 0.], 'y': [1., 0.], 'status': 'insufficient_evidence'}
    points = [(s['center_source_px'][0]-box[0], s['center_source_px'][1]-box[1],
               s['dx_source_px'], s['dy_source_px']) for s in samples
              if s['status'] in ('sample_within_half_cell', 'suspect_shift_over_half_cell')]
    result['reliable_points'] = len(points)
    if len(points) < 6:
        return result
    width, height = box[2]-box[0], box[3]-box[1]
    kept = points
    for _ in range(3):
        if len(kept) < 6 or max(p[0] for p in kept)-min(p[0] for p in kept) < width*.4 or max(p[1] for p in kept)-min(p[1] for p in kept) < height*.4:
            return result
        sx, bx = line([(x, dx) for x, y, dx, dy in kept])
        sy, by = line([(y, dy) for x, y, dx, dy in kept])
        residuals = [math.hypot(dx-(sx*x+bx), dy-(sy*y+by)) for x, y, dx, dy in kept]
        filtered = [p for p, residual in zip(kept, residuals) if residual <= 3]
        if len(filtered) == len(kept):
            break
        kept = filtered
    else:
        return result
    rms = math.sqrt(sum(r*r for r in residuals)/len(residuals))
    corner = max(math.hypot(sx*x+bx, sy*y+by) for x in (0,width) for y in (0,height))
    if rms > 2 or abs(sx) > .06 or abs(sy) > .06 or corner > 24:
        result['status'] = 'inconsistent_fit'
        return result
    result.update(x=[1+sx,bx], y=[1+sy,by], status='fitted_sparse_samples',
                  inlier_points=len(kept), residual_rms_source_px=rms, max_corner_correction_source_px=corner)
    return result


def place(image, fit, source_size, scale=SCALE, x_landmarks=None):
    """Resample into source-grid pixels, clamping edges instead of adding black."""
    image = image.convert('RGB')
    w,h = image.size
    sw,sh = source_size
    (ax,bx),(ay,by) = fit['x'],fit['y']
    pad = math.ceil(max(w/sw,h/sh)*32)+2
    padded = Image.new('RGB',(w+2*pad,h+2*pad))
    padded.paste(image,(pad,pad))
    padded.paste(image.crop((0,0,w,1)).resize((w,pad)),(pad,0))
    padded.paste(image.crop((0,h-1,w,h)).resize((w,pad)),(pad,h+pad))
    padded.paste(padded.crop((pad,0,pad+1,h+2*pad)).resize((pad,h+2*pad)),(0,0))
    padded.paste(padded.crop((w+pad-1,0,w+pad,h+2*pad)).resize((pad,h+2*pad)),(w+pad,0))
    if x_landmarks:
        # Landmark coordinates describe vertical geometry boundaries in source
        # pixels. Interpolate between them; retain the independently fitted Y.
        result=Image.new('RGB',(sw*scale,sh*scale))
        for (start,mapped_start),(end,mapped_end) in zip(x_landmarks,x_landmarks[1:]):
            left,right=round(start*scale),round(end*scale)
            local_ax=(mapped_end-mapped_start)/(end-start)
            local_bx=mapped_start-local_ax*start
            segment=padded.transform((right-left,sh*scale),Image.Transform.AFFINE,
                (local_ax*w/sw/scale,0,(local_bx+local_ax*(left/scale+.5/scale-.5)+.5)*w/sw-.5+pad,
                 0,ay*h/sh/scale,(by+ay*(.5/scale-.5)+.5)*h/sh-.5+pad),Image.Resampling.BICUBIC)
            result.paste(segment,(left,0))
        return result
    # Pixel-center coordinates, with fit measured at source resolution.
    return padded.transform((sw*scale,sh*scale),Image.Transform.AFFINE,
        (ax*w/sw/scale,0,(bx+ax*(.5/scale-.5)+.5)*w/sw-.5+pad,
         0,ay*h/sh/scale,(by+ay*(.5/scale-.5)+.5)*h/sh-.5+pad),Image.Resampling.BICUBIC)


def weight_image(size, box, full):
    """Window deepest from its inner edges dominates; outer map edges stay full."""
    w,h=size
    far=float(max(full)*SCALE)
    def axis(length,start,end):
        return [min(i+.5 if start else far,length-i-.5 if end else far) for i in range(length)]
    row=Image.new('F',(w,1));row.putdata(axis(w,box[0]>0,box[2]<full[0]))
    col=Image.new('F',(1,h));col.putdata(axis(h,box[1]>0,box[3]<full[1]))
    return ImageMath.lambda_eval(lambda a:(a['min'](a['r'],a['c'])/(64*SCALE))**4,
        r=row.resize(size,Image.Resampling.NEAREST),c=col.resize(size,Image.Resampling.NEAREST))


def seam_mask(old, new, vertical=True):
    """Choose a continuous low-difference cut through a rectangular overlap.

    Solve at source resolution; keep the new pixels to the right (or below).
    Only the cut itself is feathered, so disagreeing details are not averaged
    over the entire overlap. This chooses existing art, never synthesizes it.
    """
    if not vertical:
        return seam_mask(old.transpose(Image.Transpose.TRANSPOSE),
                         new.transpose(Image.Transpose.TRANSPOSE)).transpose(Image.Transpose.TRANSPOSE)
    size=(max(1,old.width//SCALE),max(1,old.height//SCALE))
    difference=ImageChops.difference(old,new).resize(size,Image.Resampling.BOX).convert('L')
    # Charge the neighbourhood too: a cut between two displaced outlines can
    # otherwise retain both copies despite having zero error on the cut itself.
    difference=ImageChops.add(difference,difference.filter(ImageFilter.GaussianBlur(4)))
    width,height=size
    pixels=list(difference.get_flattened_data())
    previous=[pixels[x]+.1*abs(x-(width-1)/2) for x in range(width)]
    parents=[]
    for y in range(1,height):
        row=[];current=[]
        for x in range(width):
            parent=min(range(max(0,x-1),min(width,x+2)),key=lambda q:previous[q])
            row.append(parent)
            current.append(previous[parent]+pixels[y*width+x]+.1*abs(x-(width-1)/2))
        parents.append(row);previous=current
    x=min(range(width),key=lambda q:previous[q]);cut=[x]
    for row in reversed(parents):
        x=row[x];cut.append(x)
    cut.reverse()
    mask=Image.new('L',size);draw=ImageDraw.Draw(mask)
    for y,x in enumerate(cut):
        draw.line((x,y,width-1,y),fill=255)
    return mask.resize(old.size,Image.Resampling.BILINEAR)


def assemble_seams(base, pieces):
    """Raster-order quilting with narrow feathering at content-aware cuts."""
    result=base.copy();colour=base.filter(ImageFilter.GaussianBlur(COLOUR_RADIUS))
    coverage=Image.new('L',base.size);placed=[]
    for box,piece in sorted(pieces,key=lambda item:(item[0][1],item[0][0])):
        area=tuple(v*SCALE for v in box)
        own=piece.filter(ImageFilter.GaussianBlur(COLOUR_RADIUS));target=colour.crop(area)
        corrected=Image.merge('RGB',[ImageMath.lambda_eval(lambda a:a['p']-a['o']+a['t'],
            p=p.convert('F'),o=o.convert('F'),t=t.convert('F')).convert('L')
            for p,o,t in zip(piece.split(),own.split(),target.split())])
        old=result.crop(area);mask=Image.new('L',piece.size,255)
        left=max((b[2]-box[0] for b in placed if b[1]==box[1] and b[0]<box[0]<b[2]),default=0)*SCALE
        top=max((b[3]-box[1] for b in placed if b[1]<box[1]<b[3] and b[0]<box[2] and b[2]>box[0]),default=0)*SCALE
        if left:
            overlap=(0,0,min(left,piece.width),piece.height)
            mask.paste(seam_mask(old.crop(overlap),corrected.crop(overlap)),(0,0))
        if top:
            overlap=(0,0,piece.width,min(top,piece.height))
            upper=seam_mask(old.crop(overlap),corrected.crop(overlap),vertical=False)
            mask.paste(ImageChops.darker(mask.crop(overlap),upper),(0,0))
        mask=mask.filter(ImageFilter.GaussianBlur(2))
        # A cut may never leave uncovered pixels, including first row/column.
        mask=ImageChops.lighter(mask,ImageChops.invert(coverage.crop(area)))
        result.paste(Image.composite(corrected,old,mask),area[:2])
        coverage.paste(255,area);placed.append(box)
    return result


def assemble(base, pieces, blend_mode='weighted'):
    """Blend registered windows, retaining the whole image's large-scale colour."""
    if blend_mode=='seam':
        return assemble_seams(base,pieces)
    if blend_mode!='weighted':
        raise ValueError(f'Unsupported blend mode: {blend_mode}')
    colour=base.filter(ImageFilter.GaussianBlur(COLOUR_RADIUS))
    sums=[ImageMath.lambda_eval(lambda a:a['c']*BASE_WEIGHT,c=c.convert('F')) for c in base.split()]
    weights=Image.new('F',base.size,BASE_WEIGHT)
    full=(base.width//SCALE,base.height//SCALE)
    for box,piece in pieces:
        area=tuple(v*SCALE for v in box)
        own=piece.filter(ImageFilter.GaussianBlur(COLOUR_RADIUS))
        target=colour.crop(area)
        # Float arithmetic avoids intermediate 8-bit clipping of detail.
        corrected=Image.merge('RGB',[ImageMath.lambda_eval(lambda a:a['p']-a['o']+a['t'],
            p=p.convert('F'),o=o.convert('F'),t=t.convert('F')).convert('L')
            for p,o,t in zip(piece.split(),own.split(),target.split())])
        weight=weight_image(piece.size,box,full)
        for i,channel in enumerate(corrected.split()):
            sums[i].paste(ImageMath.lambda_eval(lambda a:a['s']+a['p']*a['w'],
                s=sums[i].crop(area),p=channel.convert('F'),w=weight),area[:2])
        weights.paste(ImageMath.lambda_eval(lambda a:a['a']+a['b'],a=weights.crop(area),b=weight),area[:2])
    return Image.merge('RGB',[ImageMath.lambda_eval(lambda a:a['s']/a['w']+.5,s=c,w=weights).convert('L') for c in sums])


def restore_protected(painted, flat, protected):
    # Feather outside the mask. Every protected pixel itself stays exact.
    feather=ImageChops.lighter(protected,protected.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5)))
    result=Image.composite(flat,painted,feather)
    difference=ImageChops.difference(result,flat)
    for channel in difference.split():
        if ImageChops.multiply(channel,protected).getbbox():
            raise AssertionError('protected palette pixels changed')
    return result


def apply_dense_review(kit, specs, audit, path, selected):
    """Use explicitly reviewed denser samples, only against their exact inputs."""
    document=json.loads(path.read_text())
    if document.get('schema')!='srw64.tactical-dense-registration-review.v1':
        raise ValueError('Unsupported dense review schema')
    rows={r['id']:r for r in document['rows']}
    jobs={}
    for spec in specs.values():
        for job in [dict(spec['whole'],id=f"map-{spec['map']:03d}-00-whole",box=spec['paint_box']),*spec['windows']]:
            jobs[job['id']]=(spec,job)
    replacements={}
    for identifier in selected:
        if identifier not in rows or identifier not in jobs or identifier not in audit:
            raise ValueError(f'Missing dense review job: {identifier}')
        row=rows[identifier];spec,job=jobs[identifier]
        if (row['map']!=spec['map'] or row['box']!=job['box'] or
            row['source_sha256']!=sha(kit/spec['source']) or
            row['generated_sha256']!=sha(kit/job['output'])):
            raise ValueError(f'Stale dense review inputs: {identifier}')
        fit=fit_samples(row['raw']['samples'],job['box'])
        if fit!=row['fit'] or fit['status']!='fitted_sparse_samples':
            raise ValueError(f'Dense review has no reproducible fit: {identifier}')
        replacements[identifier]=dict(audit[identifier],**row['raw'],sha256=row['generated_sha256'],
            diagnostic_grid=8,sample_evidence={'path':str(path.resolve()),'sha256':sha(path)})
    return dict(audit,**replacements)


def load_landmark_review(kit, specs, path):
    """Load measured boundary controls bound to exact source and artwork files."""
    document=json.loads(path.read_text())
    if document.get('schema')!='srw64.tactical-landmark-registration.v1':
        raise ValueError('Unsupported landmark review schema')
    jobs={j['id']:(m,j) for m in specs.values() for j in
          [dict(m['whole'],id=f"map-{m['map']:03d}-00-whole",box=m['paint_box']),*m['windows']]}
    result={}
    for row in document['rows']:
        m,j=jobs[row['id']]
        if row['map']!=m['map'] or row['box']!=j['box'] or row['source_sha256']!=sha(kit/m['source']) or row['generated_sha256']!=sha(kit/j['output']):
            raise ValueError(f"Stale landmark inputs: {row['id']}")
        anchors=row['x_landmarks'];width=j['box'][2]-j['box'][0]
        if len(anchors)<2 or anchors[0]!=[0,0] or anchors[-1]!=[width,width]:
            raise ValueError('Landmarks must cover both image edges')
        if any(not all(math.isfinite(v) for v in p) or abs(p[1]-p[0])>8 for p in anchors):
            raise ValueError('Landmark correction exceeds half a source cell')
        for a,b in zip(anchors,anchors[1:]):
            if b[0]<=a[0] or b[1]<=a[1] or abs((b[1]-a[1])/(b[0]-a[0])-1)>.06:
                raise ValueError('Landmarks fold or excessively scale the image')
        result[row['id']]=dict(row,review_path=str(path.resolve()),review_sha256=sha(path))
    return result


def load_field_review(kit,specs,path):
    document=json.loads(path.read_text())
    if document.get('schema')!='srw64.tactical-field-registration.v1':
        raise ValueError('Unsupported field review schema')
    jobs={j['id']:(m,j) for m in specs.values() for j in
          [dict(m['whole'],id=f"map-{m['map']:03d}-00-whole",box=m['paint_box']),*m['windows']]}
    result={}
    for row in document['rows']:
        m,j=jobs[row['id']]
        if row['map']!=m['map'] or row['box']!=j['box'] or row['source_sha256']!=sha(kit/m['source']) or row['generated_sha256']!=sha(kit/j['output']):
            raise ValueError(f"Stale field inputs: {row['id']}")
        if fit_field(row['raw']['samples'],j['box'])!=row['fit']:
            raise ValueError('Field fit is not reproducible from measured samples')
        result[row['id']]=dict(row,review_path=str(path.resolve()),review_sha256=sha(path))
    return result


def compose_map(kit, out, rom, spec, audit, records, colony_overlay=False, blend_mode='weighted', landmarks=None, fields=None):
    data=load_map(rom,spec['map'])
    source=Image.open(kit/spec['source']).convert('RGB')
    protected=protected_mask(data)
    pb=spec['paint_box']; full=(pb[2]-pb[0],pb[3]-pb[1])
    jobs=[dict(spec['whole'],id=f"map-{spec['map']:03d}-00-whole",box=pb),*spec['windows']]
    registered=[];reports=[]
    for job in jobs:
        path=kit/job['output'];digest=sha(path)
        if records[job['id']]['sha256']!=digest or audit[job['id']]['sha256']!=digest:
            raise ValueError(f"stale record/audit for {job['id']}")
        if sha(kit/job['input'])!=job['input_sha256']:
            raise ValueError(f"source changed for {job['id']}")
        box=job['box']; size=(box[2]-box[0],box[3]-box[1])
        fit=fit_samples(audit[job['id']]['samples'],box)
        controls=(landmarks or {}).get(job['id'])
        field=(fields or {}).get(job['id'])
        if field and controls:raise ValueError('Cannot combine field and axis landmarks')
        piece=place_field(Image.open(path),field['fit'],size) if field else place(Image.open(path),fit,size,x_landmarks=controls['x_landmarks'] if controls else None)
        grid=audit[job['id']].get('diagnostic_grid',4)
        if field:grid=8
        after=diagnose(source.crop(box),piece,protected.crop(box),box[:2],grid=grid)
        reports.append({'id':job['id'],'generated_sha256':digest,'fit':fit,
            'raw_suspect_samples':audit[job['id']]['suspect_samples'],'registered_diagnostic':after,
            'diagnostic_grid':grid,'sample_evidence':audit[job['id']].get('sample_evidence')})
        if controls:reports[-1]['landmark_registration']=controls
        if field:
            heldout=diagnose(source.crop(box),piece,protected.crop(box),box[:2],grid=9)
            if heldout['reliable_samples']<12 or heldout['suspect_samples']:
                raise ValueError('Field registration failed independent 9x9 sample grid')
            reports[-1].update(initial_affine_fit=fit,
                fit=dict(field['fit'],status='fitted_field_with_heldout_samples'),
                field_registration=field,heldout_diagnostic=heldout,
                raw_suspect_samples=field['raw']['suspect_samples'],
                sample_evidence={'path':field['review_path'],'sha256':field['review_sha256']})
        registered.append((tuple(v-pb[i%2] for i,v in enumerate(box)),piece))
    base=assemble(registered[0][1],registered[1:],blend_mode=blend_mode)
    index4=magnify(data['indices'],data['palette'],SCALE)
    instances=colony_instances(data) if colony_overlay else []
    # The static layer retains the generated star field. Palette correction here
    # uses the most common opaque black space index; the animated sprite has its
    # own frame index map. Index zero is transparent green in this palette.
    if instances:
        animated_indices={i for c in data['channels'] for i in range(c['first_index'],c['first_index']+c['count'])}
        histogram=data['indices'].histogram()
        neutral=[i for i,c in enumerate(data['palette']) if c==(0,0,0,255) and i not in animated_indices]
        if not neutral:
            raise ValueError('Space palette has no neutral background index')
        background_index=max(neutral,key=lambda i:histogram[i])
        for x,y in instances:
            index4.paste(background_index,(x*SCALE,y*SCALE,(x+64)*SCALE,(y+48)*SCALE))
    flat=render(index4,data['palette']).convert('RGB')
    painted=flat.copy();painted.paste(base,(pb[0]*SCALE,pb[1]*SCALE))
    animated4,_=masks(data,index4)
    exact_mask=ImageChops.lighter(animated4,protected.resize(flat.size,Image.Resampling.NEAREST))
    for x,y in instances:
        exact_mask.paste(0,(x*SCALE,y*SCALE,(x+64)*SCALE,(y+48)*SCALE))
    result=restore_protected(painted,flat,exact_mask)
    folder=out/f"map-{spec['map']:03d}"
    folder.mkdir(parents=True,exist_ok=False)
    result.save(folder/'base.png');index4.save(folder/'index.png');exact_mask.save(folder/'protected.png')
    meta={'schema':'srw64.hd-map-runtime.v0','map':spec['map'],'layout':data['layout'],'atlas':data['atlas'],
          'palette':data['palette_id'],'width':source.width,'height':source.height,'scale':SCALE,
          'source_run':'tactical-kit/imagegen','rom_sha256':ROM_SHA256,'reference_palette':[list(c) for c in data['palette']],
          'files':{n:sha(folder/n) for n in ['base.png','index.png','protected.png']},
          'acceptance_proven':False,'runtime_verified':False,'colony_animation_integrated':bool(instances)}
    if instances:
        meta.update(mode=data['mode'],colony_instances=instances,colony_background_index=background_index)
    (folder/'meta.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    report={'map':spec['map'],'family':spec['family'],'colour_radius_hd_px':COLOUR_RADIUS,
            'blend_mode':blend_mode,
            'composition_code_sha256':sha(Path(__file__)),
            'field_code_sha256':sha(Path(__file__).with_name('tactical_registration_field.py')) if fields else None,
            'protected_pixels_exact':True,'registration':reports,'acceptance_proven':False,
            'limitations':['Sparse samples do not verify all geometry.','Cell colour differences are not displacement proof.',
                           'Colony sprites use original game frame counter; runtime verification is separate.' if instances else
                           'Colony cells, if present, retain original frame zero.','No in-game verification.']}
    report['colony_overlay_instances']=instances
    (folder/'composition.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    return {'map':spec['map'],'folder':str(folder),'assets':len(jobs),
            'raw_suspects':sum(r['raw_suspect_samples'] for r in reports),
            'after_suspects':sum(r['registered_diagnostic']['suspect_samples'] for r in reports),
            'unfitted':sum(r['fit']['status'] not in ('fitted_sparse_samples','fitted_field_with_heldout_samples') for r in reports)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kit',type=Path,default=ROOT/'assets/hd-ai/tactical-kit')
    p.add_argument('--rom',type=Path,default=ROOT/'rom.z64')
    p.add_argument('--maps',type=int,nargs='+',required=True)
    p.add_argument('--output',type=Path,required=True,help='New staging directory; never overwrites a map')
    p.add_argument('--colony-frames',type=Path,help='Transparent frame pack; enables dynamic colony overlays')
    p.add_argument('--dense-review',type=Path,help='Hash-bound dense registration review index')
    p.add_argument('--dense-jobs',nargs='+',help='Explicitly reviewed job IDs from the dense review')
    p.add_argument('--blend-mode',choices=['weighted','seam'],default='weighted')
    p.add_argument('--landmark-review',type=Path,help='Hash-bound measured geometry boundary controls')
    p.add_argument('--field-review',type=Path,help='Hash-bound nonlinear field review; revalidated on a separate grid')
    args=p.parse_args()
    if bool(args.dense_review)!=bool(args.dense_jobs):
        p.error('--dense-review and --dense-jobs must be used together')
    manifest=json.loads((args.kit/'manifest.json').read_text())
    styles=json.loads((args.kit/'style/manifest.json').read_text())
    approved={r['family'] for r in styles['references'] if r.get('user_approved') and sha(args.kit/r['path'])==r['sha256']}
    specs={m['map']:m for m in manifest['maps']}
    landmarks=load_landmark_review(args.kit,specs,args.landmark_review) if args.landmark_review else {}
    fields=load_field_review(args.kit,specs,args.field_review) if args.field_review else {}
    if any(row['map'] not in args.maps for row in landmarks.values()):
        p.error('Every landmark job must belong to a requested map')
    if any(row['map'] not in args.maps for row in fields.values()):
        p.error('Every field job must belong to a requested map')
    for number in args.maps:
        if specs[number]['family'] not in approved:
            p.error(f"map {number}: family is not approved")
        if (args.output/f'map-{number:03d}').exists():
            p.error(f"map {number}: output exists; choose a new staging directory")
    audit={r['id']:r for r in json.loads((args.kit/'outputs/geometry-audit.json').read_text())['rows']}
    if args.dense_review:
        if any(int(identifier.split('-')[1]) not in args.maps for identifier in args.dense_jobs):
            p.error('Every dense job must belong to a requested map')
        audit=apply_dense_review(args.kit,specs,audit,args.dense_review,args.dense_jobs)
    records={r['id']:r for r in json.loads((args.kit/'outputs/generation-records.json').read_text())['records'] if '/archive/' not in r['output']}
    rom=args.rom.read_bytes()
    if args.colony_frames:
        runtime_frames(rom,args.colony_frames,args.output/'colony')
    for number in args.maps:
        print(json.dumps(compose_map(args.kit,args.output,rom,specs[number],audit,records,bool(args.colony_frames),args.blend_mode,landmarks,fields)),flush=True)


if __name__=='__main__':
    main()
