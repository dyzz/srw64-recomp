"""Narrow RT64 v5 CI4 font hashing, verified against real TMEM dumps by the pack builder."""
from pathlib import Path
import ctypes
import struct
import subprocess
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'assets/hd-ai/native-support'

def hasher():
    BUILD.mkdir(parents=True, exist_ok=True)
    source=BUILD/'hash.c';library=BUILD/'libhash.dylib'
    source.write_text('#define XXH_INLINE_ALL\n#include "xxhash.h"\nunsigned long long srw64_xxh3(const void* p, size_t n) { return XXH3_64bits(p,n); }\n')
    if not library.exists():
        subprocess.run(['clang','-shared','-fPIC','-O2','-I',str(ROOT/'build/recomp/upstream/RT64/src/contrib/xxHash'),str(source),'-o',str(library)],check=True)
    function=ctypes.CDLL(str(library)).srw64_xxh3
    function.argtypes=[ctypes.c_void_p,ctypes.c_size_t];function.restype=ctypes.c_uint64
    return lambda data:f'{function(data,len(data)):016x}'

def font_hash(rows, palette, width, xxh):
    assert width in (8,14) and len(rows)==14 and len(palette)>=32
    # RT64 v5 hashes 7-byte CI4 odd rows as the last 3 bytes then first 4.
    data=b''.join((row[4:]+row[:4]) if y%2 and width==14 else row for y,row in enumerate(rows))
    used=sorted({n for byte in data for n in (byte>>4,byte&15)})
    data+=b''.join(palette[i*2:i*2+2]*4 for i in used)
    data+=struct.pack('<HHIHBB',width,14,32768,1,0,2)
    return xxh(data)

def map_hash(pixels, palette, xxh):
    assert len(pixels)==2048 and len(palette)==32
    physical=[]
    for y in range(64):
        row=pixels[y*32:(y+1)*32]
        if y%2:row=b''.join(row[x+4:x+8]+row[x:x+4] for x in range(0,32,8))
        physical.append(row)
    data=b''.join(physical)
    used=sorted({n for v in data for n in (v>>4,v&15)})
    data+=b''.join(palette[i*2:i*2+2]*4 for i in used)
    return xxh(data+struct.pack('<HHIHBB',64,64,32768,4,0,2))
