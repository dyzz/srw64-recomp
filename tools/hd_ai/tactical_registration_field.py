"""Small, bounded quadratic displacement fields for measured map distortion.

The six-term field is fit to all reliable coarse correspondences and must also
pass a different sampling grid before export. It never substitutes for artwork
generation or a visual review of the resulting map.
"""
import math
from PIL import Image


def terms(x,y,width,height):
    x/=width;y/=height
    return [1,x,y,x*x,x*y,y*y]


def solve(matrix,values):
    rows=[list(row)+[value] for row,value in zip(matrix,values)]
    for i in range(len(values)):
        pivot=max(range(i,len(rows)),key=lambda q:abs(rows[q][i]))
        rows[i],rows[pivot]=rows[pivot],rows[i]
        if abs(rows[i][i])<1e-10:raise ValueError('Singular registration field')
        divisor=rows[i][i];rows[i]=[v/divisor for v in rows[i]]
        for q in range(len(rows)):
            if q!=i:
                multiplier=rows[q][i]
                rows[q]=[x-multiplier*y for x,y in zip(rows[q],rows[i])]
    return [row[-1] for row in rows]


def displacement(coefficients,x,y,width,height):
    basis=terms(x,y,width,height)
    return [sum(a*b for a,b in zip(c,basis)) for c in coefficients]


def fit_field(samples,box):
    width,height=box[2]-box[0],box[3]-box[1]
    points=[(s['center_source_px'][0]-box[0],s['center_source_px'][1]-box[1],
             s['dx_source_px'],s['dy_source_px']) for s in samples
            if s['status'] in ('sample_within_half_cell','suspect_shift_over_half_cell')]
    if len(points)<18 or any(max(p[q] for p in points)-min(p[q] for p in points)<size*.7 for q,size in enumerate((width,height))):
        raise ValueError('Insufficient distributed evidence for registration field')
    design=[terms(x,y,width,height) for x,y,dx,dy in points]
    normal=[[sum(a[i]*a[j] for a in design) for j in range(6)] for i in range(6)]
    coefficients=[solve(normal,[sum(a[i]*p[q] for a,p in zip(design,points)) for i in range(6)]) for q in (2,3)]
    rms=math.sqrt(sum(sum((a-b)**2 for a,b in zip(displacement(coefficients,x,y,width,height),(dx,dy))) for x,y,dx,dy in points)/len(points))
    if not math.isfinite(rms) or rms>2:raise ValueError('Inconsistent registration field')
    # Displacement is sampled every source pixel, including the outer edges.
    maximum=max(math.hypot(*displacement(coefficients,x,y,width,height))
                for x in range(width+1) for y in range(height+1))
    if maximum>24:raise ValueError('Registration field displacement exceeds 24 source pixels')
    # Each Jacobian entry is linear, so its extrema occur at these corners.
    for u in (0,1):
        for v in (0,1):
            cx,cy=coefficients
            xx=1+(cx[1]+2*cx[3]*u+cx[4]*v)/width
            xy=(cx[2]+cx[4]*u+2*cx[5]*v)/height
            yx=(cy[1]+2*cy[3]*u+cy[4]*v)/width
            yy=1+(cy[2]+cy[4]*u+2*cy[5]*v)/height
            if not (.9<=xx<=1.1 and .9<=yy<=1.1 and abs(xy)<=.08 and abs(yx)<=.08):
                raise ValueError('Registration field excessively scales or shears')
    return {'coefficients':coefficients,'reliable_points':len(points),
            'residual_rms_source_px':rms,'max_displacement_source_px':maximum}


def place_field(image,field,source_size,scale=4):
    width,height=source_size;image=image.convert('RGB');pad=math.ceil(32*max(image.width/width,image.height/height))+2
    padded=Image.new('RGB',(image.width+2*pad,image.height+2*pad));padded.paste(image,(pad,pad))
    padded.paste(image.crop((0,0,image.width,1)).resize((image.width,pad)),(pad,0))
    padded.paste(image.crop((0,image.height-1,image.width,image.height)).resize((image.width,pad)),(pad,image.height+pad))
    padded.paste(padded.crop((pad,0,pad+1,padded.height)).resize((pad,padded.height)),(0,0))
    padded.paste(padded.crop((pad+image.width-1,0,pad+image.width,padded.height)).resize((pad,padded.height)),(pad+image.width,0))
    def point(x,y):
        # Use the same pixel-center convention as the affine registration.
        x+=.5/scale-.5;y+=.5/scale-.5
        dx,dy=displacement(field['coefficients'],x,y,width,height)
        return ((x+dx+.5)*image.width/width-.5+pad,(y+dy+.5)*image.height/height-.5+pad)
    mesh=[]
    for y in range(0,height,16):
        for x in range(0,width,16):
            right,bottom=min(x+16,width),min(y+16,height)
            mesh.append(((x*scale,y*scale,right*scale,bottom*scale),
                         (*point(x,y),*point(x,bottom),*point(right,bottom),*point(right,y))))
    return padded.transform((width*scale,height*scale),Image.Transform.MESH,mesh,Image.Resampling.BICUBIC)
