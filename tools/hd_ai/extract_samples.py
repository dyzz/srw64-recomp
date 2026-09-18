"""Extract the narrowly verified SRW64 CI4 map fixture and captured CI textures.

This is an asset experiment, not a general model or texture decoder.
"""
from pathlib import Path
from collections.abc import Iterable
import hashlib
import json
import struct

from PIL import Image, ImageDraw
from srw64_rom.resources import ResourceTable

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets/hd-ai/2026-09-08"


def rgba16(raw: bytes) -> list[tuple[int, int, int, int]]:
    return [tuple(round(((v >> shift) & 31) * 255 / 31) for shift in (11, 6, 1))
            + (255 * (v & 1),) for (v,) in struct.iter_unpack(">H", raw)]


def indexed(raw: bytes, size: tuple[int,int], palette: list[tuple[int,int,int,int]], bits: int) -> Image.Image:
    indices = raw if bits == 8 else [v for b in raw for v in (b >> 4, b & 15)]
    assert len(indices) == size[0] * size[1]
    return Image.frombytes("RGBA", size, bytes(c for v in indices for c in palette[v]))


def map_image(data: bytes) -> tuple[Image.Image, dict]:
    assert data[:4] == bytes.fromhex("36340038")
    assert struct.unpack_from(">I", data, 4)[0] == 3
    start = struct.unpack_from(">I", data, 20)[0]
    cache = {}; tiles = []; pal = tex = None; dims = None
    for pos in range(start, len(data), 8):
        w0, w1 = struct.unpack_from(">II", data, pos)
        op = w0 >> 24
        if op == 0xDF:
            break
        if op == 0x01:
            count = (w0 >> 12) & 255
            first = ((w0 >> 1) & 127) - count
            offset = w1 & 0xFFFFFF
            assert w1 >> 24 == 4
            for k in range(count):
                cache[first+k] = struct.unpack_from(">hhhHhh4B", data, offset + k*16)
        elif op == 0xFD:
            assert w1 >> 24 == 4
            fmt = (w0 >> 21) & 7
            if fmt == 0: pal = w1 & 0xFFFFFF
            elif fmt == 2: tex = w1 & 0xFFFFFF
            else: raise ValueError("unsupported texture")
        elif op == 0xF2:
            dims = (((w1 >> 12) & 4095)//4+1, (w1 & 4095)//4+1)
        elif op == 0x06:
            ids = sorted(set([(w0 >> s & 255)//2 for s in (16,8,0)] +
                             [(w1 >> s & 255)//2 for s in (16,8,0)]))
            assert len(ids) == 4 and dims == (64,64)
            vertices = [cache[i] for i in ids]
            assert len({v[1] for v in vertices}) == 1
            xs = [min(v[0] for v in vertices),max(v[0] for v in vertices)]
            zs = [min(v[2] for v in vertices),max(v[2] for v in vertices)]
            assert xs[1]-xs[0] > 100 and zs[1]-zs[0] > 100
            # Require the observed axis-aligned, unrotated UV orientation.
            for v in vertices:
                assert min(abs(v[0]-x) for x in xs)<=4
                assert min(abs(v[2]-z) for z in zs)<=4
                assert v[4] in ((0,) if abs(v[0]-xs[0])<=4 else (2016,2048))
                # A few triangles use 63 instead of 64 at the bottom edge.
                # Retain exact UVs in the manifest; the source atlas keeps all pixels.
                assert v[5] in ((0,) if abs(v[2]-zs[1])<=4 else (2016,2048))
            image = indexed(data[tex:tex+2048], dims, rgba16(data[pal:pal+32]),4)
            tiles.append((xs[0],zs[1], image, {"texture_offset":tex,"palette_offset":pal,
                         "vertices":[list(v) for v in vertices],"display_list_offset":pos}))
    assert tiles
    def cluster(values: Iterable[int]) -> list[int]:
        result=[]
        for value in sorted(set(values)):
            if not result or abs(value-result[-1])>4: result.append(value)
        return result
    xs=cluster(t[0] for t in tiles); zs=cluster(t[1] for t in tiles)[::-1]
    image=Image.new("RGBA",(len(xs)*64,len(zs)*64))
    seen=set(); records=[]
    for x,z,tile,record in tiles:
        xy=(min(range(len(xs)),key=lambda i:abs(xs[i]-x))*64,
            min(range(len(zs)),key=lambda i:abs(zs[i]-z))*64)
        assert xy not in seen;seen.add(xy)
        image.paste(tile,xy);record["xy"]=list(xy);records.append(record)
    # Exclude absent quads rather than inventing ocean pixels. The benchmark uses
    # the largest fully covered rectangle of the reconstructed source atlas.
    rectangles=[]
    for x0 in range(len(xs)):
        for x1 in range(x0+1,len(xs)+1):
            for y0 in range(len(zs)):
                for y1 in range(y0+1,len(zs)+1):
                    if all((x*64,y*64) in seen for x in range(x0,x1) for y in range(y0,y1)):
                        rectangles.append(((x1-x0)*(y1-y0),(x0*64,y0*64,x1*64,y1*64)))
    _,crop=max(rectangles)
    return image.crop(crop),{"tiles":records,"source_atlas_size":list(image.size),"crop":list(crop)}


def main() -> None:
    OUT.mkdir(parents=True,exist_ok=True)
    extract=OUT/"extract";extract.mkdir(exist_ok=True)
    rom=(ROOT/"rom.z64").read_bytes()
    assert hashlib.sha256(rom).hexdigest()=="ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e"
    table=ResourceTable(rom)
    records=[]
    for rid in (5604,5605):
        d,_=table.extract(rid)
        try: image,tiles=map_image(d)
        except (AssertionError,ValueError) as e:
            print(rid,"unsupported",str(e));continue
        file=extract/f"map-{rid}.png";image.save(file)
        records.append({"resource_id":rid,"decoded_sha256":hashlib.sha256(d).hexdigest(),
                        "image":str(file.relative_to(OUT)),"dimensions":list(image.size),"tiles":tiles})
        print(rid,image.size,len(tiles['tiles']))
    (OUT/"extraction.json").write_text(json.dumps({"schema":"srw64.hd-sample-extraction.v1","maps":records},indent=2)+"\n")


if __name__ == "__main__": main()
