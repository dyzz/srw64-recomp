"""Rebuild the actual resource-1296 dialogue border slices as a scalable frame."""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw
from srw64_rom.resources import ResourceTable
from tools.hd_ai.rt64_hash import ROOT, hasher


def frame_image() -> Image.Image:
    # Draw the whole logical frame first, then cut its original 16x16 slices.
    # Oversampling supplies alpha coverage for diagonals, never bitmap text.
    scale=8
    image=Image.new('RGBA',(192*scale,64*scale))
    draw=ImageDraw.Draw(image)
    def polygon(inset,corner,color):
        x0=y0=inset;x1=192-inset;y1=64-inset
        points=[(x0+corner,y0),(x1-corner,y0),(x1,y0+corner),(x1,y1-corner),
                (x1-corner,y1),(x0+corner,y1),(x0,y1-corner),(x0,y0+corner)]
        draw.polygon([(round(x*scale),round(y*scale)) for x,y in points],fill=color)
    def line(points,color,width):
        draw.line([(round(x*scale),round(y*scale)) for x,y in points],fill=color,width=round(width*scale),joint='curve')
    polygon(1,5,(14,22,34,255))
    polygon(1.7,4.5,(136,153,171,255))
    polygon(2.25,4,(214,223,232,255))
    polygon(3.8,3.25,(47,60,78,255))
    polygon(4.45,3,(65,133,232,255))
    polygon(5.05,2.75,(10,24,42,255))
    polygon(5.65,2.5,(0,0,0,0))
    line([(7,2.2),(185,2.2)],(245,248,253,255),.45)
    line([(7,61.7),(185,61.7)],(170,185,202,255),.5)
    line([(2.25,7),(2.25,57)],(229,237,246,255),.4)
    line([(189.6,7),(189.6,57)],(166,183,204,255),.45)
    return image.convert('RGBa').resize((768,256),Image.Resampling.LANCZOS).convert('RGBA')


def build_frame(pack: Path, output: Path, capture: Path) -> dict:
    data=ResourceTable((ROOT/'rom.z64').read_bytes()).extract(1296)[0]
    assert struct.unpack_from('>4H',data)==(5,512,16,0)
    ram=(capture/'latest-gfx-rdram.bin').read_bytes()
    address=ram.find(data)
    assert address>=0 and ram.find(data,address+1)<0
    palette=ram[0x2f5c00:0x2f5c20]
    assert palette.hex()=='22081095ffffc6338c693331ffc784016b61ad6df801f801f801f801f801f801'
    # These are the actual source coordinates and roles from the captured DL.
    locations={320:(0,0),336:(16,0),352:(16,0),48:(16,0),368:(16,0),384:(176,0),
               464:(0,16),480:(176,16),400:(0,48),208:(16,48),
               416:(16,48),432:(16,48),448:(176,48)}
    dumps=ROOT/'build/recomp/font-probe/replay-original-1/textures'
    captured={}
    for path in dumps.glob('*.rice.json'):
        load=json.loads(path.read_text())
        if load['texture']['address']!=2788752:continue
        tile=json.loads(path.with_name(path.name.replace('.rice.json','.tile.json')).read_text())
        assert (tile['width'],tile['height'],tile['tile']['line'],tile['tile']['fmt'],tile['tile']['siz'])==(16,16,1,2,0)
        tmem=path.with_name(path.name.replace('.rice.json','.tmem')).read_bytes()
        assert b''.join(tmem[2048+i*8:2050+i*8] for i in range(16))==palette
        captured[load['tile']['uls']//2]=path.name.split('.')[0]
    assert captured.keys()==locations.keys()
    image=frame_image();image.save(output/'dialogue-frame-768x256.png')
    xxh=hasher();textures=[]
    for x,(dx,dy) in locations.items():
        rows=[data[8+y*256+x//2:8+y*256+x//2+8] for y in range(16)]
        physical=b''.join(row[4:]+row[:4] if y%2 else row for y,row in enumerate(rows))
        used=sorted({n for v in physical for n in (v>>4,v&15)})
        hashed=physical+b''.join(palette[n*2:n*2+2]*4 for n in used)+struct.pack('<HHIHBB',16,16,32768,1,0,2)
        digest=xxh(hashed)
        assert digest==captured[x], 'Frame source hash must match real RT64 TMEM'
        tile=image.crop((dx*4,dy*4,(dx+16)*4,(dy+16)*4))
        name=f'frame-resource1296-{digest}.png';tile.save(pack/name)
        textures.append({'hashes':{'rt64':digest},'path':name,'source_x':x,'slice_xy':[dx,dy],
                         'sha256':hashlib.sha256((pack/name).read_bytes()).hexdigest()})
    return {'resource_id':1296,'decoded_sha256':hashlib.sha256(data).hexdigest(),
            'exact_live_rdram_address':address,'original_slice_size':[16,16],'replacement_slice_size':[64,64],
            'construction':'Code-drawn silver/blue chamfered frame; globally rasterized alpha; original source slice roles',
            'captured_tmem_hashes_verified':len(textures),'textures':textures}
