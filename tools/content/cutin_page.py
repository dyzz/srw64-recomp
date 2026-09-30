#!/usr/bin/env python3
"""Build a single-file web page of the battle cut-ins, grouped by move.

Reads the images from the graphics export (assets/original-graphics, see
export_graphics.py) and the actor fields from the ROM; the descriptions are
the ones in docs/data/battle-cutins.md. Output is local-only under assets/.
"""
from __future__ import annotations

import argparse
import base64
import html
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

from srw64_native.battle_graphics import CUTIN_REGISTRY, read_animation_bank, read_triplets

# (title, owner, weapon ids, note, registry ids in play order)
MOVES = (
    ("超電磁スピン", "コン・バトラーV", (778,), "", (985, 984)),
    ("ファイナルゴッドマーズ", "ゴッドマーズ／ガイヤー", (742,), "", (988, 986, 989, 987)),
    ("ロケットミサイル", "ジャイアントロボ", (821,), "", (991, 990)),
    ("ロケットバズーカ", "ジャイアントロボ", (820,), "", (994, 992, 993)),
    ("V-MAX", "レイズナー", (1075,), "", (997, 995, 996)),
    ("V-MAX", "ニューレイズナー", (1062,), "背景与 レイズナー 共用 997", (997, 998, 999)),
    ("ドモン 开场特写", "シャイニング／ゴッドガンダム", (18, 24, 29, 1216),
     "三个 フィンガー 系和 ラブラブ天驚拳 共用的开场", (1011, 1009, 1010)),
    ("シャイニングフィンガー", "シャイニングガンダム", (29, 24),
     "先播 ドモン 开场特写；24 是没有机体装备的同招记录", (1014, 1015, 1013, 1012, 1016)),
    ("爆熱ゴッドフィンガー", "ゴッドガンダム", (18,),
     "先播 ドモン 开场特写，最后接明鏡止水", (1007, 1008, 1004, 1005, 1006)),
    ("明鏡止水", "ゴッドガンダム", (18, 19), "爆熱ゴッドフィンガー 与 石破天驚拳 共用", (1002, 1000, 1001, 1003)),
    ("石破天驚拳", "ゴッドガンダム", (19,), "接在明鏡止水之后", (1036, 1034, 1035)),
    ("石破ラブラブ天驚拳", "ゴッドガンダム＋ライジングガンダム", (1216,), "先播 ドモン 开场特写",
     (1024, 1023, 1020, 1022, 1021, 1025, 1026)),
    ("シャッフル同盟拳", "ゴッド＋マックスター＋ローズ＋ドラゴン＋ボルト", (1217,),
     "1027–1029 超出 140×120 的遮框窗，宽屏下最需要实机看", (1031, 1027, 1028, 1029, 1030, 1032, 1033)),
    ("断空光牙剣", "ダンクーガ及各兽战机", (874, 881), "881 是没有机体装备的同招记录",
     (1038, 1037, 1017, 1018, 1019)),
)
CONTENT = {
    984: "コン・バトラーV 摆架势、带电、合拢成钻头旋转", 985: "背景：紫色云层",
    986: "ゴッドマーズ 举剑、闪电、剑尖特写", 987: "白色斩线与十字闪光",
    988: "背景：绿色能量云，第二帧全黑", 989: "胸口「M」标志",
    990: "肩上导弹筒抬起、开火，最后切面部特写", 991: "背景：蓝色斜向速度线",
    992: "火箭筒炮口伸出，最后切面部特写", 993: "炮口尾烟", 994: "背景：蓝色横向速度线",
    995: "レイズナー 头部特写、全身、变成蓝色剪影", 996: "眼部黄光、放射线、浅蓝剪影",
    997: "背景：暗绿竖线，第二帧全白", 998: "ニューレイズナー 头部特写、全身、蓝色剪影",
    999: "眼部黄光、放射线、浅蓝剪影",
    1000: "金色 ドモン 合掌、ゴッドガンダム 胸部、背翼展开", 1001: "放射线、日轮、King of Hearts 纹章",
    1002: "背景：金色竖线，第二帧全黑", 1003: "单独的纹章",
    1004: "手掌张开、护甲展开、发光", 1005: "红色剪影（火焰状）", 1006: "背景：全黑、金色斜光",
    1007: "ゴッドガンダム 面部与握拳，静止", 1008: "红色弧光",
    1009: "ドモン 面部，举起右手亮出手背", 1010: "手背上的 King of Hearts 纹章", 1011: "背景：蓝色竖线",
    1012: "手臂伸出、护甲展开、闪白、发绿光的手掌", 1013: "背景：紫色光带",
    1014: "シャイニングガンダム 面部与握拳，静止", 1015: "红色弧光", 1016: "全黑背景",
    1017: "断空剣 自上而下入画，ダンクーガ 持剑全身", 1018: "橙色、品红能量柱与斩击弧光", 1019: "蓝色闪电",
    1020: "背景：橙绿放射能量", 1021: "King of Hearts 纹章", 1022: "红心王像：远景、面部特写、披风全身",
    1023: "ドモン 与 レイン：白月下相拥、两手交握、并肩出掌", 1024: "背景：金色光带、绿底星点",
    1025: "1023 的零件残片", 1026: "1023 的零件残片",
    1027: "五人金色半身像从右向左依次滑入排成一列", 1028: "五个红色光点（各人手背纹章位置）",
    1029: "五人全身出掌、聚拢", 1030: "背景光带（同 1031 图）", 1031: "背景：淡金色光带",
    1032: "五枚纹章聚拢成一枚", 1033: "全黑、爆闪、全白",
    1034: "金色 ドモン 面部、双掌推出", 1035: "红色光环", 1036: "背景：蓝白斜向光雨",
    1037: "忍 呐喊，随后 沙羅、雅人、亮 的眼部横条逐个叠上", 1038: "背景：蓝色竖线",
}

CSS = """
:root{color-scheme:dark;--bg:#14151a;--card:#1d1f27;--line:#2c2f3a;--text:#e6e7ec;--dim:#9498a8;--hot:#f0b35a;--warn:#e0705f}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 -apple-system,"PingFang SC","Hiragino Sans",sans-serif}
header{padding:28px 24px 8px;max-width:1280px;margin:auto}
h1{margin:0 0 4px;font-size:26px}
header p{margin:0;color:var(--dim)}
nav{position:sticky;top:0;z-index:2;background:rgba(20,21,26,.94);border-bottom:1px solid var(--line);
 padding:10px 24px;display:flex;gap:6px;flex-wrap:wrap;align-items:center}
nav a{color:var(--text);text-decoration:none;padding:3px 10px;border:1px solid var(--line);border-radius:14px;font-size:13px}
nav a:hover{border-color:var(--hot);color:var(--hot)}
nav label{margin-left:auto;color:var(--dim);font-size:13px;display:flex;gap:12px}
main{max-width:1280px;margin:auto;padding:8px 24px 60px}
section{margin-top:34px;scroll-margin-top:70px}
h2{margin:0;font-size:20px}
h2 small{font-weight:400;color:var(--dim);font-size:14px;margin-left:10px}
.note{color:var(--dim);margin:2px 0 12px;font-size:13px}
.grid{display:flex;flex-wrap:wrap;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px;max-width:100%}
.card.unused{border-color:var(--warn);border-style:dashed}
.stage{background:#5a285a;border-radius:4px;overflow-x:auto;line-height:0;min-width:256px;text-align:center}
body.black .stage{background:#000}
.stage img{image-rendering:pixelated;zoom:2}
.card .strip{display:none;margin-top:6px}
body.strips .card .strip{display:block}
.strip img{zoom:1}
.id{font-weight:600;color:var(--hot);margin-top:8px}
.id b{color:var(--warn);font-weight:600;margin-left:8px;font-size:12px}
.what{max-width:300px}
.meta{color:var(--dim);font-size:12px;font-variant-numeric:tabular-nums;max-width:300px}
"""


def data_uri(path: Path) -> str:
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode()


def build(rom: bytes, export: Path) -> str:
    manifest = json.loads((export / "manifest.json").read_text())
    images: dict[int, dict] = {}
    strips = {f["path"]: f for f in manifest["files"] if f["kind"] == "battle-cutin-frames"}
    for f in manifest["files"]:
        if f["kind"] == "battle-cutin":
            for bid in f["binding"]["indices"]:
                images.setdefault(bid, f)
    registry = read_triplets(rom, "battle_scenes")
    bank = read_animation_bank(rom, "weapon")
    lo, hi = CUTIN_REGISTRY
    listed = {bid for move in MOVES for bid in move[4]}
    if listed != set(range(lo, hi + 1)) or set(CONTENT) != listed:
        raise ValueError("MOVES and CONTENT must cover every cut-in registry entry")

    nav, sections = [], []
    for n, (title, owner, weapon_ids, note, ids) in enumerate(MOVES):
        actors = {a.registry: a for w in reversed(weapon_ids) for a in bank[w][1].actors}
        cards = []
        for bid in ids:
            f, actor = images[bid], actors.get(bid)
            sid, aid, pid = registry[bid]
            ticks = sum(count for _, count in f["steps"])
            meta = [f"场景 {sid} · 图集 {aid} · 调色板 {pid}",
                    f"{f['width']}×{f['height']} · {f['frames']} 帧 · {ticks} tick"]
            if actor:
                meta.append(f"行为 {actor.behavior} · h4 {actor.h4:X} · h5 {actor.h5} · z {actor.z}"
                            + (f" · 偏移 ({actor.x}, {actor.y})" if actor.x or actor.y else ""))
            strip = f["path"].replace(".png", "-frames.png")
            strip_html = (f'<div class="stage strip"><img src="{data_uri(export / strip)}" alt=""></div>'
                          if strip in strips else "")
            cards.append(
                f'<div class="card{"" if actor else " unused"}">'
                f'<div class="stage"><img src="{data_uri(export / f["path"])}" width="{f["width"]}" '
                f'height="{f["height"]}" alt="{bid}"></div>{strip_html}'
                f'<div class="id">{bid}{"" if actor else "<b>无引用</b>"}</div>'
                f'<div class="what">{html.escape(CONTENT[bid])}</div>'
                f'<div class="meta">{"<br>".join(html.escape(m) for m in meta)}</div></div>')
        weapons = "、".join(str(w) for w in weapon_ids)
        nav.append(f'<a href="#m{n}">{html.escape(title)}</a>')
        sections.append(
            f'<section id="m{n}"><h2>{html.escape(title)}<small>{html.escape(owner)} · 武器 {weapons}</small></h2>'
            f'<div class="note">{html.escape(note)}</div><div class="grid">{"".join(cards)}</div></section>')
    toggles = ('<label><span><input type="checkbox" onchange="document.body.classList.toggle(\'strips\',this.checked)">'
               ' 逐帧</span><span><input type="checkbox" onchange="document.body.classList.toggle(\'black\',this.checked)">'
               ' 黑底</span></label>')
    return (f'<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width,initial-scale=1"><title>战斗 cut-in 总表</title>'
            f'<style>{CSS}</style><header><h1>战斗 cut-in 总表</h1>'
            f'<p>登记号 {lo}–{hi}，共 {hi - lo + 1} 个场景、{len(MOVES)} 段；每段按演出顺序排列，'
            f'虚线框是没有任何武器记录引用的场景。原版 2 倍显示，动画按 1 tick = 1/30 秒预览。</p></header>'
            f'<nav>{"".join(nav)}{toggles}</nav><main>{"".join(sections)}</main></html>\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--export", type=Path, default=ROOT / "assets/original-graphics")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/cutins/index.html")
    args = parser.parse_args()
    page = build(args.rom.read_bytes(), args.export)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(page)
    print(f"{args.output} ({len(page) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
