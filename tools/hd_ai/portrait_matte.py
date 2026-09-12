"""Remove the connected gray backdrop from the approved AI portrait itself.

The original 96px mask is retained as a QA reference, not reapplied to a
slightly different generated silhouette. Interior gray clothes/hair stay opaque.
"""
from collections import deque
from statistics import median
from PIL import Image, ImageFilter


def matte_portrait(image: Image.Image, reference_alpha: Image.Image):
    image = image.convert('RGB')
    image.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
    w, h = image.size
    rgb = list(image.getdata())
    corner = [rgb[y*w+x] for y in range(max(2,h//32))
              for x in list(range(max(2,w//32))) + list(range(w-max(2,w//32),w))]
    bg = tuple(round(median(p[c] for p in corner)) for c in range(3))
    eligible = bytearray(max(abs(p[c]-bg[c]) for c in range(3)) <= 24 for p in rgb)
    backdrop = bytearray(w*h)
    queue = deque()
    def seed(i):
        if eligible[i] and not backdrop[i]:
            backdrop[i] = 1
            queue.append(i)
    for x in range(w): seed(x); seed((h-1)*w+x)
    for y in range(h): seed(y*w); seed(y*w+w-1)
    while queue:
        i = queue.popleft(); x = i % w
        if x: seed(i-1)
        if x+1<w: seed(i+1)
        if i>=w: seed(i-w)
        if i+w<w*h: seed(i+w)
    hard = Image.frombytes('L', (w,h), bytes(0 if b else 255 for b in backdrop))
    inner = bytes(hard.filter(ImageFilter.MinFilter(5)).tobytes())
    outer = bytes(hard.filter(ImageFilter.MaxFilter(5)).tobytes())
    # Propagate nearby solid foreground colors into only a two-pixel edge band.
    owner = [-1]*(w*h); queue=deque()
    for i, v in enumerate(inner):
        if v:
            owner[i]=i
            if (i%w and not inner[i-1]) or (i%w+1<w and not inner[i+1]) or (i>=w and not inner[i-w]) or (i+w<w*h and not inner[i+w]): queue.append(i)
    while queue:
        i=queue.popleft();x=i%w
        for j in ((i-1 if x else -1),(i+1 if x+1<w else -1),(i-w if i>=w else -1),(i+w if i+w<w*h else -1)):
            if j>=0 and outer[j] and owner[j]<0:
                owner[j]=owner[i];queue.append(j)
    out=bytearray(w*h*4); edge=0
    for i,c in enumerate(rgb):
        if inner[i]: f=c; a=255
        elif not outer[i] or owner[i]<0: f=c; a=0
        else:
            f=rgb[owner[i]]; v=[f[k]-bg[k] for k in range(3)]
            denominator=sum(x*x for x in v)
            alpha=sum((c[k]-bg[k])*v[k] for k in range(3))/denominator if denominator>64 else float(not backdrop[i])
            a=round(255*max(0,min(1,alpha)))
            if a<8: a=0
            edge+=1
        out[i*4:i*4+4]=bytes((*f,a))
    result=Image.frombytes('RGBA',(w,h),bytes(out))
    ref=reference_alpha.resize((w,h),Image.Resampling.NEAREST)
    old=[a>=128 for a in ref.getdata()];new=[a>=128 for a in result.getchannel('A').getdata()]
    intersection=sum(a and b for a,b in zip(old,new));union=sum(a or b for a,b in zip(old,new))
    report={'method':'connected gray backdrop plus local edge decontamination, premultiplied resize',
            'background_rgb':bg,'working_dimensions':[w,h],'edge_pixels':edge,
            'silhouette_iou_against_original':intersection/union,
            'foreground_area_ratio_against_original':sum(new)/sum(old)}
    if not .75 < report['foreground_area_ratio_against_original'] < 1.3:
        raise RuntimeError(f'portrait matte needs inspection: {report}')
    return result,report
