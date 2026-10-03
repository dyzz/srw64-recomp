"""HD textures of the battle ground models, by RT64 texture replacement.

The ground behind a battle is 3D model nodes (resources 5584-6066, the battle scenes
catalog's `models`); their textures go through the RDP like any other, so RT64 can
swap them by its TMEM hash (docs/design/battle-animation-rendering.md §9). The hashes
are computed here without running the game: each model's display list is walked with
a small RDP model (SETTIMG, SETTILE, SETTILESIZE, LOADBLOCK, LOADTILE, LOADTLUT, TEXTURE)
that fills a 4 KB TMEM the way the hardware does (odd rows word-swapped, TLUT entries
quadricated in the upper half), and every textured draw is hashed as RT64 v5 hashes it
(common/rt64_tmem_hasher.h: the sampled rows, the TLUT entries the texels use, then
width, height, tlut, line, siz, fmt). The pilot's sixteen hashes from a real TMEM dump
(assets/hd-ai/battle-backgrounds/test-1/keys.json) must come out unchanged.

    .venv/bin/python tools/hd_ai/battle_ground_hd.py keys
    build/esrgan-venv/bin/python tools/hd_ai/battle_ground_hd.py run
    .venv/bin/python tools/hd_ai/battle_ground_hd.py pack [--bind]

keys writes assets/hd-ai/battle-ground/keys.json and original/<hash>.png; run
upscales each 4x with the unit poses' blend (4x-UltraSharpV2 + 4x-PixelPerfectV4),
padded by the texture's own wrap mode so repeating tiles stay seamless, into hd/;
pack copies them into the RT64 pack the art manifest names (`battle-<hash>.png`)
and, with --bind, lists them there as kind `battle`.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT)]
OUT = ROOT / 'assets/hd-ai/battle-ground'
SOURCE = ROOT / 'assets/models/3d-2026-09-09'
CATALOG = ROOT / 'assets/hd-ai/battle-backgrounds/catalog/catalog.json'
PILOT = ROOT / 'assets/hd-ai/battle-backgrounds/test-1/keys.json'
MANIFEST = ROOT / 'content/art/stage1-hd.json'
TLUT_RGBA16 = 0x8000     # othermode textLUT the ground draws with (the pilot's dumps)
PAD, SCALE = 16, 4


class Tile:
    def __init__(self):
        self.fmt = self.siz = self.line = self.tmem = self.palette = 0
        self.cms = self.cmt = self.masks = self.maskt = 0
        self.uls = self.ult = self.lrs = self.lrt = 0


class Rdp:
    def __init__(self):
        self.tmem = bytearray(4096)
        self.tiles = [Tile() for _ in range(8)]
        self.image = (0, 0, 0, 0)      # address, fmt, siz, width
        self.texture_on, self.texture_tile = False, 0

    def write(self, address: int, data: bytes) -> None:
        for i, b in enumerate(data):
            self.tmem[(address + i) & 0xFFF] = b

    def command(self, data: bytes, a: int, b: int) -> None:
        op = a >> 24
        if op == 0xFD:
            self.image = (b & 0xFFFFFF, a >> 21 & 7, a >> 19 & 3, (a & 0xFFF) + 1)
        elif op == 0xF5:
            t = self.tiles[b >> 24 & 7]
            t.fmt, t.siz, t.line, t.tmem = a >> 21 & 7, a >> 19 & 3, a >> 9 & 0x1FF, a & 0x1FF
            t.palette, t.cmt, t.maskt = b >> 20 & 15, b >> 18 & 3, b >> 14 & 15
            t.cms, t.masks = b >> 8 & 3, b >> 4 & 15
        elif op == 0xF2:
            t = self.tiles[b >> 24 & 7]
            t.uls, t.ult, t.lrs, t.lrt = a >> 12 & 0xFFF, a & 0xFFF, b >> 12 & 0xFFF, b & 0xFFF
        elif op == 0xD7:
            self.texture_on, self.texture_tile = bool(a & 0xFF), a >> 8 & 7
        elif op == 0xF3:                                     # LOADBLOCK
            t = self.tiles[b >> 24 & 7]
            uls, ult, lrs, dxt = a >> 12 & 0xFFF, a & 0xFFF, b >> 12 & 0xFFF, b & 0xFFF
            address, _, siz, width = self.image
            bpp = (1 << siz) / 2                             # bytes per texel of the load size
            start = address + int((ult * width + uls) * bpp)
            count = int((lrs - uls + 1) * bpp)
            count = (count + 7) // 8 * 8
            block = bytearray(data[start:start + count].ljust(count, b'\0'))
            counter = 0
            for w in range(0, count, 8):
                if (counter >> 11) & 1:
                    block[w:w + 8] = block[w + 4:w + 8] + block[w:w + 4]
                counter += dxt
            self.write(t.tmem * 8, bytes(block))
        elif op == 0xF4:                                     # LOADTILE
            t = self.tiles[b >> 24 & 7]
            uls, ult, lrs, lrt = (a >> 12 & 0xFFF) >> 2, (a & 0xFFF) >> 2, (b >> 12 & 0xFFF) >> 2, (b & 0xFFF) >> 2
            address, _, siz, width = self.image
            bpp = (1 << siz) / 2
            row_bytes = int((lrs - uls + 1) * bpp)
            for y in range(ult, lrt + 1):
                src = address + int((y * width + uls) * bpp)
                row = bytearray(data[src:src + row_bytes].ljust((row_bytes + 7) // 8 * 8, b'\0'))
                if (y - ult) & 1:
                    for w in range(0, len(row), 8):
                        row[w:w + 8] = row[w + 4:w + 8] + row[w:w + 4]
                self.write(t.tmem * 8 + (y - ult) * t.line * 8, bytes(row))
        elif op == 0xF0:                                     # LOADTLUT
            t = self.tiles[b >> 24 & 7]
            count = ((b >> 14) & 0x3FF) + 1
            address = self.image[0]
            for i in range(count):
                entry = data[address + 2 * i:address + 2 * i + 2]
                self.write(t.tmem * 8 + i * 8, entry * 4)

    def sample(self, tile: Tile) -> tuple[int, int]:
        clamp_s, clamp_t = tile.masks == 0 or tile.cms & 2, tile.maskt == 0 or tile.cmt & 2
        tw = max((tile.lrs - tile.uls + 4) // 4, 1) if clamp_s else 0xFFFF
        th = max((tile.lrt - tile.ult + 4) // 4, 1) if clamp_t else 0xFFFF
        return min(tw, 1 << tile.masks if tile.masks else 0xFFFF), min(th, 1 << tile.maskt if tile.maskt else 0xFFFF)


def tmem_hash(tmem: bytes, tile: Tile, width: int, height: int, tlut: int, xxh) -> str:
    """RT64 TMEMHasher::hash, version 5, for 4- and 8-bit colour-indexed tiles."""
    assert tile.siz in (0, 1) and tlut
    size, mask = 2048, 2047                                  # a TLUT draw hashes the lower half
    per_row = max(width << tile.siz >> 1, 1)
    line = tile.line << 3
    base = (tile.tmem << 3) & mask
    out, used = bytearray(), set()

    def update(chunk: bytes) -> None:
        out.extend(chunk)
        if tile.siz == 0:
            for v in chunk:
                used.add(v & 15); used.add(v >> 4)
        else:
            used.update(chunk)

    def rows(address: int, count: int, odd: bool) -> None:
        if address + count > size:
            first = size - address
            update(tmem[address:address + first])
            count, address = min(count - first, address), 0
        if odd:
            words = count // 8
            if words:
                update(tmem[address:address + words * 8]); address += words * 8; count -= words * 8
            if count > 4:
                update(tmem[address:address + count - 4]); update(tmem[address + 4:address + 8])
            elif count > 0:
                update(tmem[address + 4:address + 4 + count])
        else:
            update(tmem[address:address + count])

    if line > per_row:
        for i in range(height):
            rows((base + i * line) & mask, per_row, bool(i & 1))
    else:
        rows(base, line * (height - 1) + per_row, False)
    palette = 2048 + (tile.palette << 7 if tile.siz == 0 else 0)
    if tile.siz == 0 and used >= set(range(16)):
        out.extend(tmem[palette:palette + 0x80])
    else:
        for i in sorted(used):
            out.extend(tmem[palette + i * 8:palette + i * 8 + 8])
    out.extend(struct.pack('<HHIHBB', width, height, tlut, tile.line, tile.siz, tile.fmt))
    return xxh(bytes(out))


def decode(tmem: bytes, tile: Tile, width: int, height: int) -> Image.Image:
    """The tile as RGBA, read back from TMEM (odd rows word-swapped, quadricated TLUT)."""
    stride, base = tile.line * 8, tile.tmem * 8
    palette = 0x800 + (tile.palette * 16 * 8 if tile.siz == 0 else 0)
    pixels = bytearray()
    for y in range(height):
        start = (base + y * stride) & 0xFFF
        row = bytearray(tmem[start:start + stride] if start + stride <= 4096 else (tmem[start:] + tmem[:start + stride - 4096]))
        if y % 2:
            for x in range(0, len(row) - 7, 8):
                row[x:x + 8] = row[x + 4:x + 8] + row[x:x + 4]
        for x in range(width):
            i = (row[x // 2] >> 4 if x % 2 == 0 else row[x // 2] & 15) if tile.siz == 0 else row[x]
            v = struct.unpack_from('>H', tmem, palette + i * 8)[0]
            pixels += bytes((round((v >> 11 & 31) * 255 / 31), round((v >> 6 & 31) * 255 / 31),
                             round((v >> 1 & 31) * 255 / 31), 255 * (v & 1)))
    return Image.frombytes('RGBA', (width, height), bytes(pixels))


def ground_models() -> list[int]:
    sets = json.loads(CATALOG.read_text())['sets']
    return sorted({m['id'] for s in sets for m in s['models']})


def keys() -> None:
    from tools.hd_ai.rt64_hash import hasher
    xxh = hasher()
    geometry = json.loads((SOURCE / 'geometry-data.json').read_text())
    (OUT / 'original').mkdir(parents=True, exist_ok=True)
    found: dict[str, dict] = {}
    skipped = Counter()
    models = ground_models()
    for rid in models:
        data = (SOURCE / f'resource-{rid}.bin').read_bytes()
        rdp = Rdp()
        for group in geometry[str(rid)]:
            for offset in range(group['start'], group['end'], 8):
                a, b = struct.unpack_from('>II', data, offset)
                op = a >> 24
                if op in (0x05, 0x06) and rdp.texture_on:
                    tile = rdp.tiles[rdp.texture_tile]
                    if tile.fmt != 2 or tile.siz not in (0, 1):
                        skipped[f'fmt{tile.fmt}/siz{tile.siz}'] += 1
                        continue
                    width, height = rdp.sample(tile)
                    if width > 1024 or height > 1024:
                        skipped['unbounded'] += 1
                        continue
                    digest = tmem_hash(bytes(rdp.tmem), tile, width, height, TLUT_RGBA16, xxh)
                    entry = found.setdefault(digest, {'hash': digest, 'models': [], 'width': width, 'height': height,
                                                      'siz': tile.siz, 'cms': tile.cms, 'cmt': tile.cmt,
                                                      'masks': tile.masks, 'maskt': tile.maskt})
                    if rid not in entry['models']:
                        entry['models'].append(rid)
                    if not (OUT / 'original' / f'{digest}.png').exists():
                        decode(bytes(rdp.tmem), tile, width, height).save(OUT / 'original' / f'{digest}.png')
                else:
                    rdp.command(data, a, b)
    rows = sorted(found.values(), key=lambda r: r['hash'])
    pilot = json.loads(PILOT.read_text())
    missing = [k['hash'] for k in pilot if k['hash'] not in found]
    (OUT / 'keys.json').write_text(json.dumps({'schema': 'srw64.battle-ground-keys.v1', 'models': len(models),
                                               'pilot_matched': len(pilot) - len(missing), 'textures': rows}, indent=1) + '\n')
    print(f'{len(models)} models, {len(rows)} textures, skipped {dict(skipped)}; pilot {len(pilot) - len(missing)}/{len(pilot)} hashes reproduced')
    if missing:
        raise SystemExit(f'pilot hashes not reproduced: {missing}')


def run() -> None:
    import numpy as np
    import spandrel
    import torch
    from tools.hd_ai.esrgan_pose import fill_transparent, upscale
    device = torch.device('mps' if torch.backends.mps.is_available() else 'cpu')
    models = [spandrel.ModelLoader().load_from_file(str(next((ROOT / 'build/esrgan-models').glob(f'{name}.*')))).to(device).eval()
              for name in ('4x-UltraSharpV2', '4x-PixelPerfectV4')]

    def pad(a, axis, mode):
        n = a.shape[axis]
        index = np.arange(-PAD, n + PAD)
        return np.take(a, np.clip(index, 0, n - 1) if mode & 2 else index % n, axis=axis)

    def both(image):
        outs = [np.asarray(upscale(m, image, device)).astype(np.float32) for m in models]
        return Image.fromarray(((outs[0] + outs[1]) / 2).round().astype(np.uint8), 'RGB')

    rows = json.loads((OUT / 'keys.json').read_text())['textures']
    (OUT / 'hd').mkdir(exist_ok=True)
    for n, k in enumerate(rows, 1):
        target = OUT / 'hd' / f"{k['hash']}.png"
        if target.exists():
            continue
        src = Image.open(OUT / 'original' / f"{k['hash']}.png").convert('RGBA')
        a = pad(pad(np.asarray(src), 1, k['cms']), 0, k['cmt'])
        padded = Image.fromarray(a, 'RGBA')
        box = (PAD * SCALE, PAD * SCALE, (PAD + src.width) * SCALE, (PAD + src.height) * SCALE)
        out = both(fill_transparent(padded)).crop(box).convert('RGBA')
        if np.asarray(src)[..., 3].min() < 255:
            out.putalpha(both(padded.getchannel('A').convert('RGB')).convert('L').crop(box))
        out.save(target)
        print(f"{n}/{len(rows)} {k['hash']} {src.size}", flush=True)


def pack(bind: bool) -> None:
    from srw64_native.catalog import sha
    from tools.hd_ai.esrgan_pose import fill_transparent
    manifest = json.loads(MANIFEST.read_text())
    target = ROOT / manifest['source']['path']
    database = json.loads((target / 'rt64.json').read_text())
    entries = {e['hashes']['rt64']: e for e in database['textures']}
    added = {}
    for k in json.loads((OUT / 'keys.json').read_text())['textures']:
        digest = k['hash']
        if digest in entries and not entries[digest]['path'].startswith('battle-'):
            raise ValueError(f'{digest} already names another texture')
        image = Image.open(OUT / 'hd' / f'{digest}.png').convert('RGBA')
        if image.getchannel('A').getextrema()[0] < 255:          # straight alpha: no dark fringes
            filled = fill_transparent(image)
            filled.putalpha(image.getchannel('A'))
            image = filled
        name = f'battle-{digest}.png'
        image.save(target / name)
        entries[digest] = {'hashes': {'rt64': digest}, 'path': name}
        added[digest] = sha((target / name).read_bytes())
    database['textures'] = sorted(entries.values(), key=lambda e: e['hashes']['rt64'])
    (target / 'rt64.json').write_text(json.dumps(database, indent=2) + '\n')
    print(f'{len(added)} battle textures -> {target.relative_to(ROOT)}')
    if bind:
        rows = [r for r in manifest['textures'] if r['kind'] != 'battle']
        rows += [{'hash': h, 'kind': 'battle', 'sha256': v} for h, v in sorted(added.items())]
        manifest['textures'] = rows
        manifest['source']['manifest_sha256'] = sha((target / 'rt64.json').read_bytes())
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        print('bound', MANIFEST.relative_to(ROOT), len(rows), 'textures')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=('keys', 'run', 'pack'))
    parser.add_argument('--bind', action='store_true')
    args = parser.parse_args()
    {'keys': keys, 'run': run}.get(args.step, lambda: pack(args.bind))()


if __name__ == '__main__':
    main()
