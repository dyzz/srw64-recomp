#!/usr/bin/env python3
"""Export the pinned ROM's opening text pages in original playback order."""
from __future__ import annotations
import argparse
from dataclasses import asdict
import hashlib
import html
import json
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont
from srw64_rom.resources import ResourceTable, decode_i4_texture

ROOT = Path(__file__).resolve().parents[2]
ROM_SHA256 = "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e"


def sequences(rom: bytes) -> list[list[tuple[int, int]]]:
    def offset(address: int) -> int:
        if not 0x801C4500 <= address < 0x801CC150:
            raise ValueError("intro table pointer outside the verified overlay")
        return 0x10DA50 + address - 0x801C4500
    groups = []
    for pointer in struct.unpack_from(">5I", rom, offset(0x801CB230)):
        pages = []
        for index in range(12):
            model, texture = struct.unpack_from(">hh", rom, offset(pointer + index * 4))
            if model == -1:
                break
            if not (5537 <= model <= 5543 and 5506 <= texture <= 5535):
                raise ValueError("unexpected opening page resource")
            pages.append((model, texture))
        else:
            raise ValueError("opening sequence has no terminator")
        groups.append(pages)
    if [len(group) for group in groups] != [11, 6, 6, 5, 5]:
        raise ValueError("opening sequence lengths differ from audited ROM")
    return groups


def palette_rgba(data: bytes) -> list[tuple[int, ...]]:
    if len(data) != 40 or data[:8] != bytes.fromhex("0003002000000000"):
        raise ValueError("unexpected opening palette format")
    return [tuple(round(((value >> shift) & 31) * 255 / 31) for shift in (11, 6, 1))
            + (255 * (value & 1),) for (value,) in struct.iter_unpack(">H", data[8:])]


def compose_page(texture: Image.Image, model: bytes) -> tuple[Image.Image, list[dict]]:
    # 0101 sprites use 16-byte descriptors followed by their quad vertices.
    # Textures are atlases: e.g. model 5537 moves its first tile to (256, 0).
    # A flat export of the 304-pixel atlas would scramble the first text tile.
    if model[:8] != bytes.fromhex("01010008ff000008"):
        raise ValueError("unexpected opening page geometry")
    tiles = []
    for pos in range(8, len(model)-15, 16):
        flags,u,v,w,h,x,y,vertices = struct.unpack_from(">HHHBBhhI", model, pos)
        if flags == 0x8000:
            break
        if flags or not w or not h or u+w>texture.width or v+h>texture.height or vertices+64>len(model):
            raise ValueError("invalid opening tile descriptor")
        # Validate the actual vertex quad used by the zoom animation as well.
        xyz = [struct.unpack_from(">hhh", model, vertices + i*16) for i in range(4)]
        if set(xyz) != {(x,-y,0),(x,-y-h,0),(x+w,-y,0),(x+w,-y-h,0)}:
            raise ValueError("opening quad differs from 2D tile placement")
        tiles.append(dict(u=u,v=v,width=w,height=h,x=x,y=y,vertices=vertices))
    else:
        raise ValueError("missing sprite terminator")
    left,top = min(t['x'] for t in tiles),min(t['y'] for t in tiles)
    width = max(t['x']+t['width'] for t in tiles)-left
    height = max(t['y']+t['height'] for t in tiles)-top
    result = Image.new("RGBA", (width,height))
    coverage = set()
    for t in tiles:
        x,y=t['x']-left,t['y']-top
        pixels={(xx,yy) for xx in range(x,x+t['width']) for yy in range(y,y+t['height'])}
        if coverage & pixels:
            raise ValueError("overlapping opening page tiles")
        coverage |= pixels
        result.paste(texture.crop((t['u'],t['v'],t['u']+t['width'],t['v']+t['height'])),(x,y))
    if len(coverage) != width*height:
        raise ValueError("opening page has uncovered pixels")
    return result,tiles


def export(rom_path: Path, output: Path) -> dict:
    rom = rom_path.read_bytes()
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("expected pinned original Japanese ROM")
    groups = sequences(rom)
    table = ResourceTable(rom)
    output.mkdir(parents=True, exist_ok=True)
    (output / "textures").mkdir(exist_ok=True)
    (output / "decoded").mkdir(exist_ok=True)
    (output / "pages").mkdir(exist_ok=True)
    palette = palette_rgba(table.extract(5536)[0])
    images = {}
    records = []
    for rid in range(5506, 5544):
        raw, consumed = table.extract(rid)
        (output / "decoded" / f"{rid}.bin").write_bytes(raw)
        record = {**asdict(table.entry(rid)), "compressed_consumed": consumed,
                  "decoded_sha256": hashlib.sha256(raw).hexdigest(),
                  "role": "text_texture" if rid < 5536 else "palette" if rid == 5536 else "page_geometry"}
        if rid < 5536:
            indices, flags = decode_i4_texture(raw)
            if flags != 0:
                raise ValueError("unexpected opening texture flags")
            image = Image.frombytes("RGBA", indices.size, bytes(
                channel for index in indices.tobytes() for channel in palette[index]))
            image.save(output / "textures" / f"{rid}.png")
            images[rid] = image
            record.update(dimensions=list(image.size), palette_resource=5536,
                          png=f"textures/{rid}.png", format="0005: 4-bit indices; original RGBA16 palette 5536")
        records.append(record)
    page_records = {}
    for group in groups:
        for model,rid in group:
            if rid in page_records:
                if page_records[rid]['model'] != model:
                    raise ValueError("shared texture uses different page geometry")
                continue
            assembled,tiles=compose_page(images[rid],table.extract(model)[0])
            assembled.save(output / "pages" / f"{rid}.png")
            images[rid]=assembled
            page_records[rid]={"model":model,"texture":rid,"tiles":tiles,
                               "png":f"pages/{rid}.png","dimensions":list(assembled.size)}
    manifest = {"schema": "srw64.intro-assets.v1", "rom_sha256": ROM_SHA256,
                "overlay_rom": "0x0010DA50", "sequence_table_vram": "0x801CB230",
                "groups": [[{"page": i + 1, "model": m, "texture": t}
                            for i, (m, t) in enumerate(group)] for group in groups],
                "resources": records, "assembled_pages": list(page_records.values())}
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")

    candidates = [Path("/System/Library/Fonts/PingFang.ttc"),
                  *sorted(Path("/System/Library/AssetsV2").glob("com_apple_MobileAsset_Font*/*/AssetData/PingFang.ttc")),
                  Path("/Library/Fonts/RODE Noto Sans CJK SC R.otf")]
    font_path = next((path for path in candidates if path.is_file()), None)
    if font_path is None:
        raise ValueError("contact sheets need an installed Chinese font (PingFang or Noto Sans CJK)")
    font = ImageFont.truetype(str(font_path), 22)
    small = ImageFont.truetype(str(font_path), 16)
    group_titles = ["公共序章", "路线序章 1", "路线序章 2", "路线序章 3", "路线序章 4"]
    sections = []
    for group_id, group in enumerate(groups):
        height = 70 + ((len(group) + 2) // 3) * 254
        sheet = Image.new("RGB", (1016, height), "#111827")
        draw = ImageDraw.Draw(sheet)
        draw.text((20, 16), f"{group_titles[group_id]} · {len(group)} 页 · ROM 原始贴图", font=font, fill="#e5efff")
        cards = []
        for index, (model, rid) in enumerate(group):
            image = images[rid]
            x = 20 + (index % 3) * 332
            y = 70 + (index // 3) * 254
            draw.rounded_rectangle((x, y, x + 312, y + 232), radius=8, fill="#030712")
            sheet.paste(image, (x + (312-image.width)//2, y + 8 + (194-image.height)//2), image)
            draw.text((x+8, y+207), f"{index+1:02d} / 纹理 {rid} / {image.width}×{image.height}", font=small, fill="#91a4c0")
            cards.append(f'<article><div class="preview"><a href="pages/{rid}.png" target="_blank">'
                         f'<img src="pages/{rid}.png" width="{image.width}" height="{image.height}" alt="{html.escape(group_titles[group_id])} 第 {index+1} 页"></a></div>'
                         f'<p><b>{index+1:02d}</b>　纹理 {rid} <span>{image.width} × {image.height} · 模型 {model}</span></p></article>')
        sheet.save(output / f"group-{group_id}.png")
        sections.append(f'<section id="group-{group_id}"><h2>{group_titles[group_id]} <small>{len(group)} 页</small></h2><div class="grid">' + "".join(cards) + '</div></section>')
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SRW64 开场文字贴图</title><style>
*{box-sizing:border-box}body{margin:0;background:#111827;color:#e5efff;font-family:-apple-system,BlinkMacSystemFont,"PingFang SC",sans-serif}main{max-width:1480px;margin:auto;padding:36px 28px}h1{font-size:30px;margin:8px 0 16px}header p{color:#aab8cf;line-height:1.7;max-width:950px}nav{display:flex;gap:12px;flex-wrap:wrap;margin:26px 0}a{color:#80cfff}nav a,button{border:1px solid #38465e;border-radius:8px;padding:10px 16px;background:#182238;color:#c7e6ff;text-decoration:none;cursor:pointer;font:inherit}section{margin:40px 0}h2{font-size:23px}small{font-size:16px;font-weight:400;color:#91a4c0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(324px,1fr));gap:18px}article{border:1px solid #2b374e;border-radius:10px;overflow:hidden;align-self:start}.preview{background:#030712;min-height:214px;display:flex;align-items:center;justify-content:center;padding:10px;overflow:auto}.preview img{display:block;max-width:none;image-rendering:pixelated}article p{padding:0 14px;font-size:14px;line-height:1.7}article span{display:block;color:#91a4c0}body.zoom .grid{grid-template-columns:1fr}body.zoom .preview img{width:calc(var(--w)*2px);height:auto}body.checker .preview{background:repeating-conic-gradient(#283140 0% 25%,#1d2533 0% 50%) 50% / 20px 20px}footer{color:#91a4c0;margin-top:40px;font-size:14px}
</style><main><header><small>SUPER ROBOT WARS 64 / 原始资源检查</small><h1>开场缩放文字 · 贴图目录</h1>
<p>30 张独立文字贴图，按原游戏顺序排列成 5 组、共 33 页。透明 PNG 使用 ROM 原始调色板，并按原模型拼回完整文字页；预览默认 1 倍显示，点击可打开原图。这些是提取的日文原始资产，尚未高清重绘或中文替换。</p>
<p>公共序章位于主角选择之前；路线编号沿用游戏内部顺序，尚未标注主角姓名。共享的「A.C.195年。地球……」纹理 5517 在四条路线中各出现一次。</p>
<nav>''' + ''.join(f'<a href="#group-{i}">{title}</a>' for i,title in enumerate(group_titles)) + '''</nav>
<button id="zoom">切换 2 倍预览</button> <button id="bg">切换透明底纹</button></header>''' + ''.join(sections) + '''
<footer>资源 5506–5535：文字；5536：共用调色板；5537–5543：文字页几何。<a href="manifest.json">资源清单与校验信息</a> · 完整文字页在 pages/，原始纹理图集在 textures/，解压数据在 decoded/。</footer></main>
<script>document.querySelectorAll('img').forEach(i=>i.style.setProperty('--w',i.width));document.getElementById('zoom').onclick=()=>document.body.classList.toggle('zoom');document.getElementById('bg').onclick=()=>document.body.classList.toggle('checker');</script></html>'''
    (output / "index.html").write_text(page)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/intro/assets")
    args = parser.parse_args()
    result = export(args.rom, args.output)
    print(json.dumps({"output": str(args.output.resolve()), "textures": 30,
                      "pages": sum(map(len, result["groups"]))}, indent=2))


if __name__ == "__main__":
    main()
