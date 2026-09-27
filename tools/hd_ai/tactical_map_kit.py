"""Build the Codex image_gen kit for the tactical maps (docs/design/tactical-map-hd-kit.md §3).

build  writes, for every root layout, one whole picture (the map without its 2-cell
       border, hard pixels) and 3:2 detail windows of 384x256 source pixels (24x16
       cells, 4x), each with its prompt; the colony frame sheet; a per-family contact
       sheet; manifest.json and a README for the person driving image_gen.

Palette cycles (water, lights) and colony cells are shown as stored (frame 0): the
model paints water flat but removes animated colonies from the static background.
Colony animation is authored separately in the eight-frame sheet. Variants
(3D34) are a second kit made from the composed roots, not part of this one.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

from PIL import Image, ImageDraw, ImageFont

from srw64_rom.resources import ResourceTable
from tools.content.map_dynamics import BORDER_TILES, cell_grid, index_atlas
from tools.hd_ai.pixel_scale import magnify
from tools.hd_ai.tactical_map_hd import (DEFAULT_OUT as MAPS_OUT, ROM_SHA256, align, color_lock, comparison, cycle_palette,
                                         export, fit_scale, load_map, masks, render)
from PIL import ImageChops, ImageFilter, ImageStat

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "assets/hd-ai/tactical-kit"
DYNAMICS = ROOT / "build/content/map-dynamics.json"
CELL = 16
SCALE = 4                                   # HD pixels per source pixel (runtime scale)
WINDOW = (24, 16)                           # cells: 384x256 source -> 1536x1024
STEP = (20, 12)                             # cells: 4-cell overlap
MIN_STATIC = 0.15                           # windows with less non-animated area are skipped
COLONY_RESOURCE, COLONY_PALETTE, COLONY_FRAME = 6235, 6258, (64, 48)
# 3D34 variants: painted later from their root's composed HD (§1.2).
VARIANTS = {**{v: 109 for v in (121, 123, 125, 127, 129, 131, 133, 135, 136)},
            **{v: 111 for v in (141, 143, 145, 147, 149, 151, 153, 155, 156)},
            39: 37, 117: 116, 118: 90, 137: 104}
FAMILIES = {
    6228: ("ground", "地面", 20,
           "top-down terrain of grassland, forests, farmland, roads, towns and city blocks, rivers, lakes and coasts, hills and wasteland",
           "tiny distinct tree crowns in irregular woods, textured meadows and fields, individual roofs and streets in the towns, "
           "clean road surfaces, rocky slopes on the hills. Colours follow Image 1 area by area: olive-green stays olive grass, light "
           "yellow-green stays light meadow, beige or sand-coloured ground stays bare sand, savanna or beach, white and pale grey stays "
           "snow, grey stays road or rock; NEVER turn sand, snow or light meadow into dark green grass or forest. Keep the dim, muted "
           "contrast of Image 1 with near-black conifer woods and muted roofs. Rivers, lakes and sea are painted as calm, flat, uniform water in the SAME grey-blue "
           "tone Image 1 has, never bright blue, with no waves, ripples, foam, reflections or a glowing shoreline (the game animates them)"),
    6229: ("space", "宇宙", 87,
           "deep space seen from above: a black starfield with small crisp stars, drifting asteroid clusters and rock debris, "
           "occasional static wreckage; rotating space colonies are dynamic overlays to omit from the static painting",
           "asteroids as sharply lit rocky boulders with clear edges and cast shadows, fine debris as small distinct fragments; "
           "stars stay tiny sharp points exactly where Image 1 has them, the background stays near-black. Do not add nebulae, planets, "
           "glows, lens flares or new objects; static wreckage keeps its exact silhouette. "
           "EXCEPTION to preserving source objects: erase every rotating space colony (cylinder, mirror panels and ring) "
           "and fill only its footprint with the surrounding near-black starfield. No silhouette, halo, shadow or placeholder "
           "may remain; the colony animation is supplied separately at runtime"),
    6230: ("fortress", "要塞内部", 35,
           "the interior of a huge asteroid fortress seen from above: grey metal decks, corridors, hangars, machinery and pipes, "
           "with black voids of open space between the structures",
           "crisp panel lines, vents, hatches and machinery detail on the decks, subtle metallic shading; the black voids stay pure "
           "dark blue-black with only the stars Image 1 has. Keep every deck outline and void shape exactly"),
    6231: ("moon", "月面", 49,
           "the lunar surface seen from above: grey regolith, craters with raised rims, ridges, boulders and dark maria",
           "fine regolith texture, crisply lit crater rims with cast shadows, scattered boulders; keep every crater's position and size"),
    6232: ("desert", "荒漠", 36,
           "an arid desert plateau seen from above with Nazca-like line drawings, patches of dark scrub and rocky outcrops",
           "fine sand and gravel texture, sharp line-drawing grooves exactly along Image 1's lines, small shrubs in the dark scrub patches. "
           "The pale crests inside the dark scrub are LOW, small rocky ridges that stay mostly hidden in the scrub, as faint as in Image 1: "
           "do not enlarge them into big grey boulders, cliffs or mountains, and keep the scrub dark olive, not grey"),
    6233: ("clouds-sea", "云上海岸", 66,
           "a view from very high above: white clouds over a bright blue sea and a sandy coast with a small town",
           "soft volumetric cloud tops with clear edges, calm flat sea (no waves), fine town detail on the coast"),
    6234: ("galaxy", "银河", 112,
           "a spiral galaxy seen face-on in deep space",
           "crisp stars and dust lanes, a bright core, spiral arms with fine structure exactly where Image 1 has them"),
    6236: ("clouds-land", "云下地表", 74,
           "a view from above through broken clouds onto green land with towns and roads",
           "thin, bright, pure white clouds with soft translucent edges exactly as light and airy as Image 1 (not heavy, grey or yellowish "
           "cumulus), fine town, road and field detail in the gaps; keep each cloud and gap where it is and keep the same overall brightness"),
}

# Features a map has that the model tends to reinterpret; appended to that map's prompts.
MAP_NOTES = {
    41: " Special features of this map: most of the ground is beige sand and beach with sparse scrub; it stays sand-coloured, not "
        "green. The brown ravines are dry gullies.",
    43: " Special features of this map: the light yellow-green lowland is a bright meadow, distinct from the darker olive slopes and "
        "the near-black woods; keep the bright meadow bright. The beige area on the right is bare ground.",
    46: " Special features of this map: this is a dry savanna coast: the beige ground is sand and dry grass, not green grassland; only "
        "the dark green patches are woods. The beige island stays sand.",
    51: " Special features of this map: a snowfield. The white and pale grey ground is snow and stays white; the grey-purple lake is "
        "frozen ice under snow, not open water, and stays grey-purple; the woods are snow-dusted conifers.",
    63: " Special features of this map: a night scene of a bombed city; keep it exactly as dark as Image 1, with only the small "
        "warm lights and fires bright. Do not brighten the streets or buildings.",
    20: " Special features of this map: the yellow-green glowing circle in the upper right is an impact crater with a raised rim, "
        "not a mountain; the black patches with red spots in the town are burnt-out ruins, not vegetation; the grey gravel bank "
        "along the upper left river is bare stony ground.",
}

# What each palette cycle shows, for the prompt: the model must paint these areas flat, in place.
CYCLE_HINTS = {
    "水面": "the flat grey-blue areas are water (river, lake or sea): paint them calm and uniform in the same tone",
    "暗绿循环": "the dark green spots are blinking machinery lights: keep them as small dark-green lights on the structure",
    "绿色慢循环": "the green glowing area is a slow energy glow: keep it a flat glow of the same colour and extent",
    "橙褐循环": "the orange-brown patches are lava or furnace glow: keep them flat, same colour and extent",
    "红色脉动": "the small red spots are fires or warning lights: keep them as small red lights exactly where they are",
    "深蓝快闪": "the scattered bright points are twinkling stars: keep them tiny sharp points, do not add more",
    "黄色循环": "the small yellow spots are blinking lights: keep them as small yellow lights in place",
    "红褐快循环": "the red-brown area is glowing embers or an energy discharge: keep it flat, same colour and extent",
    "红褐循环": "the red-brown area is lava or a heat glow: keep it flat, same colour and extent",
    "橙色循环": "the small orange spots are lights or thruster glow: keep them as small orange lights in place",
}


def auto_notes(m: dict) -> str:
    """Per-map hints derived from map-dynamics.json: cycles with visible pixels and colony cells."""
    hints = [CYCLE_HINTS[c["name"]] for c in m["cycles"] if c.get("pixels") and c["name"] in CYCLE_HINTS]
    if m["colony"]["instances"]:
        n = m["colony"]["instances"]
        hints.append(f"{'a' if n == 1 else n} rotating space colon{'y' if n == 1 else 'ies'} (a cylinder with a ring of mirrors) "
                     f"{'occupies' if n == 1 else 'occupy'} 4x3-cell blocks in the reference only: remove the entire colony "
                     "including its ring and mirror panels from this static painting, replacing it with matching dark starfield; "
                     "preserve all surrounding static asteroid and wreckage positions. Its eight-frame animation is a separate asset")
    return (" Notes for this map: " + "; ".join(hints) + ".") if hints else ""


STYLE_WITH_REFERENCE = ("Image 2 is ONLY a style reference: a finished painted battlefield map of this game's {family} maps in the "
                        "style this game now uses. Match its visual scale, brushwork, lighting and colours; do not copy its layout.")
STYLE_ANCHOR = ("There is no style reference for this family yet: paint it as classic hand-painted 2D strategy-game map art, "
                "moderate rich colour, clear shape definition, uniform lighting from the upper left, not photography, not a 3D render, "
                "not a pixel-art upscale and not a flat noise texture.")

WHOLE_PROMPT = (
    "Use case: style-transfer.\n"
    "Asset: the actual battlefield map texture of a 1999 tactical RPG (map {map}): {scene}. It is cut back into the game's 16-pixel grid "
    "cells and drawn under the units and cursor, so nothing may move.\n"
    "Image 1 is the edit target: the extracted map, upscaled with hard pixels ({ratio}). {style}\n"
    "Redraw IMAGE 1 into a crisp, richly hand-painted 2D strategy-game terrain map, keeping EXACTLY Image 1's framing, aspect ratio, "
    "top-down view and the position, outline and width of every feature: {features}. Do not add, remove, move, mirror or merge any feature, "
    "and do not correct the stylized layout into a realistic one.\n"
    "Paint {detail}.\n"
    "Tone: the overall brightness, contrast and saturation must stay the same as Image 1: do not brighten it, do not make it "
    "more vivid or high-key than Image 1. Every feature keeps its identity and colour: a crater stays a crater, a "
    "burnt ruin stays black rubble with red embers, wreckage stays wreckage, a yellow-green glowing area stays a glowing area; never "
    "turn such things into mountains, hills, bushes, boulders or ordinary buildings.{notes}\n"
    "Composition: fill the whole canvas with the same map bounds as Image 1, same aspect ratio ({w}x{h}), as large as possible, "
    "no margin, no vignette.\n"
    "Critical exclusions: absolutely NO text, labels, numbers, units, robots, ships, cursors, markers, icons, grid lines, frame, border, "
    "dialogue boxes or UI. Only the map.")

WINDOW_PROMPT = (
    "Use case: style-transfer. Asset: a precise crop of an actual battlefield map texture for a 1999 tactical RPG (map {map}, {scene}), "
    "not a finished screen.\n"
    "Image 1 is the source crop to redraw (hard pixels, 4x). Preserve EXACTLY its {w}x{h} rectangle and the position, outline and width of "
    "every feature in it: {features}. The crop's edges cut through terrain: do not extend, complete or reframe it. {style}\n"
    "Create a higher resolution painted terrain texture with hundreds of fine details across the crop: {detail}. Keep miniature map "
    "scale: a single building is about 1 to 2 percent of the canvas width, a tree crown about 0.3 to 0.6 percent; natural variation, "
    "clear hand-painted edges, no repeated stamps.\n"
    "Tone: the overall brightness, contrast and saturation must stay the same as Image 1: do not brighten it, do not make it "
    "more vivid or high-key than Image 1. Every feature keeps its identity and colour: a crater stays a crater, a "
    "burnt ruin stays black rubble with red embers, wreckage stays wreckage, a yellow-green glowing area stays a glowing area; never "
    "turn such things into mountains, hills, bushes, boulders or ordinary buildings.{notes}\n"
    "Absolutely no text, labels, units, robots, ships, cursors, markers, icons, grid lines, frame, border or UI. Fill the image with the "
    "same crop. Output {w}x{h} or larger at the identical aspect ratio, sharp details for close in-game viewing.")

COLONY_PROMPT = (
    "Use case: style-transfer. Asset: a sprite sheet from a 1999 tactical RPG: the SAME space colony (an O'Neill cylinder with a ring "
    "of mirrors, seen from above the battlefield) in 8 phases of one rotation, laid out 4 columns by 2 rows on a black starfield, "
    "read left to right, top to bottom, 8 frames of a looping animation.\n"
    "Image 1 is the edit target (hard pixels, 4x). Redraw it as crisp hand-painted 2D game art: each frame keeps EXACTLY its "
    "silhouette, size, position in the sheet and the rotation phase Image 1 shows; the cylinder body, the ring and the mirror panels "
    "get fine panel lines and metallic shading, lit from the upper left in every frame identically. The starfield stays near-black "
    "with only tiny sharp stars, identical between frames; the gaps between frames stay pure black. Do not merge frames, do not add "
    "objects, glows, text, borders or UI. Same aspect ratio as Image 1 ({w}x{h}), fill the canvas.")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def roots(dynamics: list[dict]) -> tuple[list[dict], list[int]]:
    """Root layouts to paint: referenced, one per layout, variants excluded."""
    unreferenced = [m["map"] for m in dynamics if not m["initial_for"] and not m["switched_to_by"]]
    seen, chosen = set(), []
    for m in dynamics:
        if m["map"] in unreferenced or m["map"] in VARIANTS or m["layout"] in seen:
            continue
        seen.add(m["layout"])
        chosen.append(m)
    return chosen, unreferenced


def first_scene(m: dict) -> int:
    scenes = [s["scene"] for s in m["initial_for"]] + [s for sw in m["switched_to_by"] for s in sw.get("scenes", [])]
    return min(scenes) if scenes else 999


def scene_label(m: dict) -> str:
    if m["initial_for"]:
        s = m["initial_for"][0]
        return f"stage {s['scene']} {s['title']}".strip()
    return "a stage map switched in by the story script"


def paint_box(cells: list[tuple[int, int]], cw: int, ch: int) -> tuple[int, int, int, int]:
    """Cells that are not the outer border frame, as a cell box."""
    xs = [i % cw for i, (_, t) in enumerate(cells) if t not in BORDER_TILES and t]
    ys = [i // cw for i, (_, t) in enumerate(cells) if t not in BORDER_TILES and t]
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def windows(box: tuple[int, int, int, int]) -> list[tuple[int, int, int, int]]:
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    if cw <= WINDOW[0] and ch <= WINDOW[1]:
        return []                                       # the whole picture already covers it at 4x
    def starts(size: int, win: int, step: int) -> list[int]:
        if size <= win:
            return [0]
        out = []
        s = 0
        while True:
            out.append(min(s, size - win))
            if s + win >= size:
                break
            s += step
        return sorted(set(out))
    return [(x0 + x, y0 + y, x0 + min(x + WINDOW[0], cw), y0 + min(y + WINDOW[1], ch))
            for y in starts(ch, WINDOW[1], STEP[1]) for x in starts(cw, WINDOW[0], STEP[0])]


def build(rom: bytes, out: Path, only: set[int] | None, no_windows: bool) -> dict:
    dynamics = json.loads(DYNAMICS.read_text())["maps"]
    chosen, unreferenced = roots(dynamics)
    chosen.sort(key=lambda m: (first_scene(m), m["map"]))
    out.mkdir(parents=True, exist_ok=True)
    (out / "outputs").mkdir(exist_ok=True)
    (out / "style").mkdir(exist_ok=True)
    font = ImageFont.truetype("/System/Library/Fonts/Hiragino Sans GB.ttc", 22)
    manifest = {"schema": "srw64.tactical-kit.v1", "rom_sha256": ROM_SHA256, "cell": CELL, "scale": SCALE,
                "window_cells": list(WINDOW), "step_cells": list(STEP),
                "families": {}, "maps": [], "variants": {str(v): r for v, r in VARIANTS.items()}, "unreferenced": unreferenced}
    for atlas, (name, zh, anchor, _, _) in FAMILIES.items():
        manifest["families"][name] = {"atlas": atlas, "name_zh": zh, "anchor_map": anchor,
                                      "style_reference": f"style/{name}.png", "maps": []}
    sheets: dict[str, list] = {}
    for order, m in enumerate(chosen):
        index = m["map"]
        if only and index not in only:
            continue
        family, zh, anchor, features, detail = FAMILIES[m["atlas"]]
        data = load_map(rom, index)
        (w, h), cells = cell_grid(data["layout_bytes"])
        cw, ch = w // 2, h // 2
        box = paint_box(cells, cw, ch)
        source = render(data["indices"], data["palette"])
        animated, _ = masks(data, data["indices"])
        folder = out / family
        folder.mkdir(exist_ok=True)
        stem = f"map-{index:03d}"
        is_anchor = index == anchor
        style = STYLE_ANCHOR if is_anchor else STYLE_WITH_REFERENCE.format(family=zh + " / " + family)
        scene = scene_label(m)
        # Whole picture: the map without its border, hard pixels, long side about 1536.
        px = tuple(v * CELL for v in box)
        whole = source.crop(px)
        ratio = min(4, max(2, round(1536 / max(whole.size))))
        whole_input = whole.resize((whole.width * ratio, whole.height * ratio), Image.NEAREST)
        whole_input.save(folder / f"{stem}-00-whole-input.png", optimize=True)
        prompt = WHOLE_PROMPT.format(map=index, scene=scene, ratio=f"{ratio}x", style=style, features=features, detail=detail,
                                     notes=MAP_NOTES.get(index, "") + auto_notes(m), w=whole_input.width, h=whole_input.height)
        (folder / f"{stem}-00-whole-prompt.txt").write_text(prompt + "\n")
        entry = {"map": index, "layout": m["layout"], "atlas": m["atlas"], "palette": m["palette"], "family": family,
                 "order": order, "first_scene": first_scene(m), "scene": scene, "anchor": is_anchor,
                 "size": [w * 8, h * 8], "cells": [cw, ch], "paint_box_cells": list(box), "paint_box": list(px),
                 "source": f"{family}/{stem}-source.png",
                 "whole": {"input": f"{family}/{stem}-00-whole-input.png", "scale": ratio,
                           "input_sha256": sha(folder / f"{stem}-00-whole-input.png"),
                           "prompt": f"{family}/{stem}-00-whole-prompt.txt", "output": f"outputs/{stem}-00-whole-out.png"},
                 "windows": []}
        source.save(folder / f"{stem}-source.png", optimize=True)
        if not no_windows:
            for n, wb in enumerate(windows(box), 1):
                wpx = tuple(v * CELL for v in wb)
                area = (wpx[2] - wpx[0]) * (wpx[3] - wpx[1])
                static = 1 - sum(animated.crop(wpx).histogram()[128:]) / area
                if static < MIN_STATIC:
                    continue
                crop = source.crop(wpx)
                crop.resize((crop.width * SCALE, crop.height * SCALE), Image.NEAREST).save(folder / f"{stem}-{n:02d}-input.png", optimize=True)
                wstyle = STYLE_ANCHOR if is_anchor and n == 1 else STYLE_WITH_REFERENCE.format(family=zh + " / " + family)
                (folder / f"{stem}-{n:02d}-prompt.txt").write_text(WINDOW_PROMPT.format(
                    map=index, scene=scene, features=features, detail=detail, style=wstyle, w=crop.width * SCALE, h=crop.height * SCALE,
                    notes=MAP_NOTES.get(index, "") + auto_notes(m)) + "\n")
                entry["windows"].append({"id": f"{stem}-{n:02d}", "box_cells": list(wb), "box": list(wpx), "static": round(static, 2),
                                         "input": f"{family}/{stem}-{n:02d}-input.png",
                                         "input_sha256": sha(folder / f"{stem}-{n:02d}-input.png"),
                                         "prompt": f"{family}/{stem}-{n:02d}-prompt.txt", "output": f"outputs/{stem}-{n:02d}-out.png"})
        manifest["maps"].append(entry)
        manifest["families"][family]["maps"].append(index)
        # Contact sheet tile: the source with the paint box and window boxes.
        tile = source.copy()
        draw = ImageDraw.Draw(tile)
        draw.rectangle((px[0], px[1], px[2] - 1, px[3] - 1), outline=(255, 255, 0))
        for wn in entry["windows"]:
            b = wn["box"]
            draw.rectangle((b[0], b[1], b[2] - 1, b[3] - 1), outline=(0, 255, 255))
            draw.text((b[0] + 4, b[1] + 2), wn["id"][-2:], fill=(0, 255, 255), font=font)
        draw.text((4, 4), f"{stem}  {scene}  {len(entry['windows'])} windows", fill=(255, 255, 0), font=font)
        tile.thumbnail((480, 480))
        sheets.setdefault(family, []).append(tile)
    for family, tiles in sheets.items():
        columns = min(6, len(tiles))
        rows = (len(tiles) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * 490, rows * 490), (30, 30, 30))
        for i, t in enumerate(tiles):
            sheet.paste(t, ((i % columns) * 490 + 5, (i // columns) * 490 + 5))
        sheet.save(out / f"index-{family}.jpg", quality=80)
    manifest["colony"] = colony_sheet(rom, out)
    write_readme(out, manifest)
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n")
    return manifest


def colony_sheet(rom: bytes, out: Path) -> dict:
    """The 8 colony frames (6235, space palette) as a 4x2 sheet with black gutters, 4x hard pixels."""
    resources = ResourceTable(rom)
    frames = index_atlas(resources.extract(COLONY_RESOURCE)[0])
    palette_data = resources.extract(COLONY_PALETTE)[0]
    palette = [tuple(round(((v >> s) & 31) * 255 / 31) for s in (11, 6, 1)) for (v,) in struct.iter_unpack(">H", palette_data[8:])]
    palette += [(0, 0, 0)] * (256 - len(palette))
    image = Image.frombytes("P", frames.size, frames.tobytes())
    image.putpalette([c for color in palette for c in color])
    image = image.convert("RGB")
    fw, fh = COLONY_FRAME
    gutter = 16
    sheet = Image.new("RGB", (4 * fw + 3 * gutter, 2 * fh + gutter), (0, 0, 0))
    boxes = []
    for f in range(8):
        x, y = (f % 4) * (fw + gutter), (f // 4) * (fh + gutter)
        sheet.paste(image.crop((f * fw, 0, (f + 1) * fw, fh)), (x, y))
        boxes.append([x, y, x + fw, y + fh])
    folder = out / "colony"
    folder.mkdir(exist_ok=True)
    big = sheet.resize((sheet.width * SCALE, sheet.height * SCALE), Image.NEAREST)
    big.save(folder / "colony-sheet-input.png", optimize=True)
    sheet.save(folder / "colony-sheet-source.png")
    (folder / "colony-sheet-prompt.txt").write_text(COLONY_PROMPT.format(w=big.width, h=big.height) + "\n")
    return {"resource": COLONY_RESOURCE, "palette": COLONY_PALETTE, "frame": list(COLONY_FRAME), "gutter": gutter, "frames": boxes,
            "source": "colony/colony-sheet-source.png", "input": "colony/colony-sheet-input.png",
            "input_sha256": sha(folder / "colony-sheet-input.png"),
            "prompt": "colony/colony-sheet-prompt.txt", "output": "outputs/colony-sheet-out.png"}


def write_readme(out: Path, manifest: dict) -> None:
    fam = manifest["families"]
    rows = []
    total_w = total_m = 0
    for name, f in fam.items():
        maps = [m for m in manifest["maps"] if m["family"] == name]
        nw = sum(len(m["windows"]) for m in maps)
        total_w += nw
        total_m += len(maps)
        rows.append(f"| {f['name_zh']} `{name}` | {len(maps)} | {nw} | map-{f['anchor_map']:03d} | `style/{name}.png` |")
    order = [f"map-{m['map']:03d}" for m in manifest["maps"]]
    batches = [order[i:i + 10] for i in range(0, len(order), 10)]
    text = f"""# 战术地图 image_gen 生成包

{__import__('datetime').date.today().isoformat()}。战术地图的 HD 底图由 Codex 的 image_gen 来画，做法与世界地图包相同。规划见 `docs/design/tactical-map-hd-kit.md`。

每张地图先画 **1 张整图**（去掉外圈边框的整张，硬像素放大），再画若干 **局部窗口**（通常为 3:2：384×256 源像素 → 1536×1024，4 倍；map-054 的窄图窗口为 368×256 → 1472×1024，保持实际比例）。整图定整张的颜色和地貌分布，窗口补细节。殖民地另有 1 张 8 帧图。

| 家族 | 地图 | 窗口 | 样板地图 | 图 2 |
| --- | ---: | ---: | --- | --- |
{chr(10).join(rows)}
| 合计 | {total_m} | {total_w} | | |

缩略图见 `index-<家族>.jpg`：黄框是要画的范围，青框和数字是窗口编号。

## 顺序

1. **先画 8 个家族的样板**：8 张整图，加上有窗口的 6 个家族各自的 01 号窗口，共 14 张；两个云层家族不另切窗口。这 14 张的初始提示词没有图 2，只按文字描述画风。
2. 画完告诉我，我合成后给你看；你认可的那张窗口结果（无窗口的云层家族用整图）我会存成 `style/<家族>.png`，之后同家族每一张都拿它当图 2。
3. 然后按下面的批次画（按剧情出场顺序，每批 10 张地图，先整图后窗口）：

{chr(10).join(f'- 第 {i + 1} 批：' + '、'.join(b) for i, b in enumerate(batches))}

4. 殖民地帧图 `colony/colony-sheet-input.png` 在宇宙家族样板通过后画，1 张，不用图 2。
5. 3D34 变体（22 张）不在这个包里：它们要以根图的合成结果为底，根图定稿后我再出第二个包。

## 每一张怎么画

1. 图 1：该张的 `*-input.png`（原图，硬像素放大）。
2. 图 2：`style/<家族>.png`（样板阶段没有图 2）。
3. 提示词：把同名的 `*-prompt.txt` 整段贴进去。
4. 结果按 `manifest.json` 里的 `output` 存进 `outputs/`，文件名要一模一样（PNG）：整图 `map-NNN-00-whole-out.png`，窗口 `map-NNN-MM-out.png`，殖民地 `colony-sheet-out.png`。
5. 生成记录照旧写进 `outputs/generation-records.json`。

## 检查，不合格就重画

- 每条道路、河流、岸线、建筑、森林块、陨石的位置和轮廓要与图 1 一致；不能增删、挪动、镜像或合并。
- 水面、熔岩、灯光要画成平静均匀的一片，不要波纹、倒影、闪光：这些区域游戏里会自己动，合成时会换成原版色号图。
- 输入里的旋转殖民地仅供定位：静态生成图必须抹掉本体、圆环及镜面板，补成周围的暗色星空；不要留下光晕、剪影或占位符。8 帧动画另行制作。
- 画面里不能出现文字、数字、机体、光标、网格线、边框、界面。
- 画幅比例与图 1 相同，整张铺满，不留白边。

轻微的位置偏差没关系，合成时会自动配准并逐格核对。

已生成结果的只读诊断与对照页可重新构建：

```sh
.venv/bin/python -m tools.hd_ai.tactical_map_audit
.venv/bin/python -m tools.hd_ai.tactical_kit_review
```

`outputs/geometry-audit.json` 仅提供稀疏配准和逐格平均色诊断：±12 源像素搜索、动态区域排除，模糊匹配不算通过。它不替代完整配准、逐格几何验收及实机验证。对照页为 `outputs/review.html`。


## 画完以后

1. 每张配准回原图，逐格核对平均色，偏差超过半格的窗口重画。
2. 整图定大体颜色，窗口补细节，接缝处羽化。
3. 水面、边框、殖民地格换回色号图；殖民地帧图切成 8 帧。
4. 导出成运行时资产，实机对照。
"""
    (out / "README.md").write_text(text)


def compose_whole(rom: bytes, kit: Path, index: int, colour_lock: bool, maps_out: Path) -> dict:
    """Compose a map's whole-picture output into a 4x base the runtime can use (no windows yet).

    The painting is registered to the border-less paint area, placed on a 4x canvas whose
    border, palette-cycled pixels and colony cells come from the MMPX index map, and
    exported through tactical_map_hd.export as run name imagegen-whole-1.
    """
    manifest = json.loads((kit / "manifest.json").read_text())
    entry = next(m for m in manifest["maps"] if m["map"] == index)
    data = load_map(rom, index)
    width, height = data["indices"].size
    px = entry["paint_box"]
    source = render(data["indices"], data["palette"])
    crop = source.crop(px)
    target = ((px[2] - px[0]) * SCALE, (px[3] - px[1]) * SCALE)
    generated = Image.open(kit / entry["whole"]["output"]).convert("RGB")
    raw_size = generated.size
    generated = generated.resize(target, Image.LANCZOS)
    fit = fit_scale(crop, generated)
    generated = align(generated, fit)
    if colour_lock:
        generated = color_lock(generated, crop)
    folder = maps_out / f"map-{index:03d}"
    folder.mkdir(parents=True, exist_ok=True)
    index4_path = folder / "index-4x.png"
    if not index4_path.exists():
        magnify(data["indices"], data["palette"], SCALE).save(index4_path)
    index4 = Image.open(index4_path)
    flat = render(index4, data["palette"])
    canvas = flat.copy()
    canvas.paste(generated, (px[0] * SCALE, px[1] * SCALE))
    animated4, _ = masks(data, index4)
    _, border = masks(data, data["indices"])
    protected = ImageChops.lighter(animated4, border.resize(flat.size, Image.NEAREST))
    feather = protected.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5))
    base = Image.composite(flat, canvas, feather)
    name = "imagegen-whole-1"
    base_path = folder / f"base-4x--{name}.png"
    base.save(base_path)
    comparison(folder, source, base, name)
    box = animated4.getbbox()
    if box:
        pad = 96
        box = (max(0, box[0] - pad), max(0, box[1] - pad), min(base.width, box[2] + pad), min(base.height, box[3] + pad))
        longest = max(sum(c["frame_ticks"]) for c in data["channels"])
        frames = [Image.composite(render(index4, cycle_palette(data, t)), base, animated4).crop(box) for t in range(0, longest, 3)]
        frames[0].save(folder / f"cycle--{name}.gif", save_all=True, append_images=frames[1:], duration=100, loop=0, optimize=True)
    report = {"map": index, "output": entry["whole"]["output"], "output_size": list(raw_size), "input_size": [entry["whole"]["scale"] * (px[2] - px[0]), entry["whole"]["scale"] * (px[3] - px[1])],
              "scale_fit_4x": {"x": [round(fit[0], 5), round(fit[1], 2)], "y": [round(fit[2], 5), round(fit[3], 2)]},
              "colour_lock": colour_lock,
              "source_mean_rgb": [round(v, 1) for v in ImageStat.Stat(crop).mean],
              "result_mean_rgb": [round(v, 1) for v in ImageStat.Stat(generated.resize(crop.size, Image.BOX)).mean],
              "protected_fraction": round(ImageStat.Stat(protected).mean[0] / 255, 4), "base": base_path.name, "base_sha256": sha(base_path)}
    (folder / f"report--{name}.json").write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n")
    report["export"] = export(rom, index, maps_out, f"map-{index:03d}--{name}", maps_out / "runtime")
    return report


def compose_colony(kit: Path, maps_out: Path) -> dict:
    """The 8 HD colony frames from the sheet image_gen painted (docs/design/tactical-map-hd-kit.md §3.3).

    Each painted frame is registered to its source frame. Everything that is identical in all
    8 source frames (the mirror ring, the stars, the black background) is taken from ONE
    painted frame, so the ring does not jitter between frames; only the rotating body comes
    from each frame's own painting. Writes colony-frames.png (4x2, 256x192 each, no gutters),
    a preview GIF and a comparison sheet into maps_out/colony.
    """
    from PIL import ImageChops, ImageFilter
    from tools.hd_ai.tactical_map_hd import best_shift
    m = json.loads((kit / "manifest.json").read_text())["colony"]
    fw, fh = m["frame"]
    source = Image.open(kit / m["source"]).convert("RGB")
    frames = [source.crop(tuple(b)) for b in m["frames"]]
    # The sheet may carry alpha (a transparent background lets the HD map's own stars show
    # through); it is kept as straight alpha throughout.
    painted = Image.open(kit / m["output"]).convert("RGBA")
    sheet_size = (source.width * SCALE, source.height * SCALE)
    painted = painted.resize(sheet_size, Image.LANCZOS)
    black = Image.new("RGBA", sheet_size, (0, 0, 0, 255))
    # Static pixels: equal in all 8 source frames (tolerance 8) and part of the ring, stars or background.
    static = Image.new("L", (fw, fh), 255)
    for f in frames[1:]:
        diff = ImageChops.difference(frames[0], f).convert("L").point(lambda v: 255 if v <= 8 else 0)
        static = ImageChops.multiply(static, diff)
    static_hd = static.resize((fw * SCALE, fh * SCALE), Image.NEAREST)
    # Widen by one source pixel so a ring the model drew a few HD pixels off is fully replaced,
    # but never over a frame's own body (bright source pixels that are not static).
    widened = static_hd.filter(ImageFilter.MaxFilter(2 * SCALE + 1))
    hd_frames, shifts = [], []
    ref = None
    for i, (box, f) in enumerate(zip(m["frames"], frames)):
        crop = painted.crop(tuple(v * SCALE for v in box))
        flat = Image.alpha_composite(black.crop(tuple(v * SCALE for v in box)), crop).convert("RGB")
        (dx, dy), err = best_shift(f, flat.resize((fw, fh), Image.BOX), 3)
        crop = ImageChops.offset(crop, -dx * SCALE, -dy * SCALE)
        shifts.append([dx, dy, round(err, 2)])
        if ref is None:
            ref = crop
        # The margin around the static pixels applies only where both the reference frame and
        # this frame are background, so no body of either frame leaks through.
        dark = ImageChops.multiply(frames[0].convert("L").point(lambda v: 255 if v <= 60 else 0),
                                   f.convert("L").point(lambda v: 255 if v <= 60 else 0))
        dark_hd = dark.resize((fw * SCALE, fh * SCALE), Image.NEAREST)
        mask = ImageChops.lighter(static_hd, ImageChops.multiply(widened, dark_hd))
        mask = mask.filter(ImageFilter.GaussianBlur(1))
        hd_frames.append(Image.composite(ref, crop, mask))
    folder = maps_out / "colony"
    folder.mkdir(parents=True, exist_ok=True)
    out = Image.new("RGBA", (4 * fw * SCALE, 2 * fh * SCALE), (0, 0, 0, 0))
    for i, hd in enumerate(hd_frames):
        out.paste(hd, ((i % 4) * fw * SCALE, (i // 4) * fh * SCALE))
    out.save(folder / "colony-frames.png")
    over_black = lambda im: Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)), im).convert("RGB")
    hd_frames = [over_black(hd) for hd in hd_frames]
    out = over_black(out)
    big = [hd.resize((fw * SCALE * 2, fh * SCALE * 2), Image.NEAREST) for hd in hd_frames]
    big[0].save(folder / "colony-preview.gif", save_all=True, append_images=big[1:], duration=450, loop=0)
    # Comparison: source frames (nearest x4) above the composed frames.
    comp = Image.new("RGB", (out.width, out.height * 2 + 8), (40, 40, 40))
    for i, f in enumerate(frames):
        comp.paste(f.resize((fw * SCALE, fh * SCALE), Image.NEAREST), ((i % 4) * fw * SCALE, (i // 4) * fh * SCALE))
    comp.paste(out, (0, out.height + 8))
    comp.save(folder / "colony-compare.png")
    ring_jitter = max(max(ImageChops.difference(hd_frames[0], hd).convert("L").point(lambda v: v).crop((0, 0, 15 * SCALE, fh * SCALE)).getextrema())
                      for hd in hd_frames[1:])
    alpha = Image.open(folder / "colony-frames.png").split()[3].histogram()
    meta = {"schema": "srw64.hd-colony.v0", "ring_max_diff_left_15px": ring_jitter, "alpha": "straight",
            "transparent_fraction": round(alpha[0] / sum(alpha), 3), "resource": m["resource"], "palette": m["palette"], "frame": [fw * SCALE, fh * SCALE],
            "scale": SCALE, "frames": 8, "layout": "4x2", "shifts_1x": shifts, "static_fraction": round(sum(static.histogram()[128:]) / (fw * fh), 3),
            "files": {"colony-frames.png": sha(folder / "colony-frames.png")}}
    (folder / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1) + "\n")
    return meta


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=("build", "compose-whole", "audit", "compose-colony", "build-variants", "compose-variants", "pack"))
    parser.add_argument("--pack-source", type=Path, default=ROOT / "assets/hd-ai/tactical-kit-variants/runtime-all")
    parser.add_argument("--pack-output", type=Path, default=ROOT / "assets/hd-ai/tactical-maps/pack-v1")
    parser.add_argument("--bundle", type=Path, default=ROOT / "assets/hd-ai/tactical-kit/composed/runtime-integrated-v4", help="build-variants: composed root maps")
    parser.add_argument("--variants-output", type=Path, default=ROOT / "assets/hd-ai/tactical-kit-variants")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--map", type=int, help="compose-whole: the map to compose")
    parser.add_argument("--colour-lock", action="store_true", help="compose-whole: keep the source's low-frequency colour")
    parser.add_argument("--maps-output", type=Path, default=MAPS_OUT, help="compose-whole: tactical-maps work folder")
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--maps", help="comma-separated map numbers to limit the build to")
    parser.add_argument("--no-windows", action="store_true")
    args = parser.parse_args()
    if args.step == "pack":
        print(json.dumps(pack_maps(args.pack_source, args.pack_output), ensure_ascii=False))
        return
    if args.step == "compose-variants":
        only = {int(v) for v in args.maps.split(",")} if args.maps else None
        print(json.dumps(compose_variants(args.rom.read_bytes(), args.variants_output, args.bundle, args.variants_output / "runtime", only), ensure_ascii=False))
        return
    if args.step == "build-variants":
        print(json.dumps(build_variants(args.rom.read_bytes(), args.output, args.bundle, args.variants_output), ensure_ascii=False, indent=1))
        return
    if args.step == "compose-colony":
        print(json.dumps(compose_colony(args.output, args.maps_output), ensure_ascii=False, indent=1))
        return
    if args.step == "audit":
        print(json.dumps(audit(args.output, args.rom.read_bytes(), args.workers), ensure_ascii=False, indent=1))
        return
    if args.step == "compose-whole":
        if args.map is None:
            parser.error("compose-whole needs --map")
        print(json.dumps(compose_whole(args.rom.read_bytes(), args.output, args.map, args.colour_lock, args.maps_output), ensure_ascii=False, indent=1))
        return
    only = {int(v) for v in args.maps.split(",")} if args.maps else None
    manifest = build(args.rom.read_bytes(), args.output, only, args.no_windows)
    print(json.dumps({"maps": len(manifest["maps"]), "windows": sum(len(m["windows"]) for m in manifest["maps"]),
                      "families": {k: len(v["maps"]) for k, v in manifest["families"].items()}}, ensure_ascii=False))




# ---------------------------------------------------------------------------
# audit: every output against its input (size, registration, tone, per-cell colour)

def _audit_one(job: tuple) -> dict:
    from PIL import ImageChops, ImageStat
    from tools.hd_ai.tactical_map_hd import best_shift
    kit, entry_id, inp, outp, box, scale, protected = job
    protected = set(map(tuple, protected))
    result = {"id": entry_id, "output": outp}
    path = Path(kit) / outp
    if not path.exists():
        result["status"] = "missing"
        return result
    src = Image.open(Path(kit) / inp).convert("RGB")
    src1 = src.resize((src.width // scale, src.height // scale), Image.BOX)      # back to 1x source pixels
    gen = Image.open(path).convert("RGB")
    result["size"] = list(gen.size)
    aspect = (gen.width / gen.height) / (src.width / src.height)
    result["aspect_error_percent"] = round(abs(aspect - 1) * 100, 2)
    gen1 = gen.resize(src1.size, Image.BOX)
    (dx, dy), error = best_shift(src1, gen1, 4)
    result["shift_1x"] = [dx, dy]
    result["luma_error_1x"] = round(error, 2)
    s_mean, g_mean = ImageStat.Stat(src1).mean, ImageStat.Stat(gen1).mean
    luma = lambda m: 0.299 * m[0] + 0.587 * m[1] + 0.114 * m[2]
    result["luma_ratio"] = round(luma(g_mean) / max(1, luma(s_mean)), 3)
    result["source_mean"] = [round(v, 1) for v in s_mean]
    result["output_mean"] = [round(v, 1) for v in g_mean]
    # Per-cell mean colour after undoing the shift: cells whose colour moved far are candidates
    # for a changed feature (a crater turned into a hill, water turned blue).
    moved = ImageChops.offset(gen1, -dx, -dy)
    cw, ch = src1.width // CELL, src1.height // CELL
    cells = []
    for cy in range(ch):
        for cx in range(cw):
            b = (cx * CELL, cy * CELL, (cx + 1) * CELL, (cy + 1) * CELL)
            a, c = ImageStat.Stat(src1.crop(b)).mean, ImageStat.Stat(moved.crop(b)).mean
            gx, gy = cx + box[0] // CELL, cy + box[1] // CELL
            if (gx, gy) in protected:
                continue
            d = sum(abs(a[i] - c[i]) for i in range(3)) / 3
            cells.append((round(d, 1), gx, gy))
    cells.sort(reverse=True)
    result["cell_error_mean"] = round(sum(c[0] for c in cells) / max(1, len(cells)), 1)
    result["cells_over_40"] = sum(1 for c in cells if c[0] > 40)
    result["worst_cells"] = cells[:5]
    result["status"] = "ok"
    return result


def protected_cells(rom: bytes, index: int) -> list:
    """Cells more than a quarter covered by palette-cycled pixels (water, lights) or colony frames."""
    data = load_map(rom, index)
    animated, _ = masks(data, data["indices"])
    cw, ch = animated.width // CELL, animated.height // CELL
    cells = []
    for cy in range(ch):
        for cx in range(cw):
            hist = animated.crop((cx * CELL, cy * CELL, (cx + 1) * CELL, (cy + 1) * CELL)).histogram()
            if sum(hist[128:]) > CELL * CELL // 4:
                cells.append([cx, cy])
    return cells


def audit(kit: Path, rom: bytes, workers: int) -> dict:
    """Check every generated output against its input; write outputs/audit.json and audit.md."""
    from concurrent.futures import ProcessPoolExecutor
    manifest = json.loads((kit / "manifest.json").read_text())
    jobs = []
    for m in manifest["maps"]:
        protected = protected_cells(rom, m["map"])
        jobs.append((str(kit), f"map-{m['map']:03d}-00-whole", m["whole"]["input"], m["whole"]["output"], m["paint_box"], m["whole"]["scale"], protected))
        for w in m["windows"]:
            jobs.append((str(kit), w["id"], w["input"], w["output"], w["box"], SCALE, protected))
    with ProcessPoolExecutor(workers) as pool:
        rows = list(pool.map(_audit_one, jobs, chunksize=4))
    by_id = {r["id"]: r for r in rows}
    for m in manifest["maps"]:
        by_id[f"map-{m['map']:03d}-00-whole"]["family"] = m["family"]
        for w in m["windows"]:
            by_id[w["id"]]["family"] = m["family"]
    flags = {"missing": [], "aspect": [], "shift": [], "tone": [], "cells": []}
    for r in rows:
        if r["status"] == "missing":
            flags["missing"].append(r["id"]); continue
        if r["aspect_error_percent"] > 1: flags["aspect"].append(r["id"])
        if max(abs(v) for v in r["shift_1x"]) >= 3: flags["shift"].append(r["id"])
        if not 0.75 <= r["luma_ratio"] <= 1.4: flags["tone"].append(r["id"])
        if r["cells_over_40"] >= 12: flags["cells"].append(r["id"])
    (kit / "outputs" / "audit.json").write_text(json.dumps({"schema": "srw64.tactical-kit.audit.v1", "checked": len(rows), "flags": flags, "rows": rows},
                                                            ensure_ascii=False, indent=1) + "\n")
    worst = sorted((r for r in rows if r["status"] == "ok"), key=lambda r: -r["cells_over_40"])[:24]
    lines = [f"# 生成结果体检\n\n检查 {len(rows)} 张：缺失 {len(flags['missing'])}、比例偏差>1% {len(flags['aspect'])}、"
             f"配准偏移≥3 源像素 {len(flags['shift'])}、亮度比超出 0.75–1.4 {len(flags['tone'])}、色差>40 的格≥12 个 {len(flags['cells'])}。\n",
             "| 图 | 家族 | 尺寸 | 偏移 | 亮度比 | 平均格色差 | 色差>40 的格 | 最差格 (色差, x, y) |", "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for r in worst:
        lines.append(f"| {r['id']} | {r['family']} | {r['size'][0]}×{r['size'][1]} | {r['shift_1x']} | {r['luma_ratio']} | {r['cell_error_mean']} | "
                     f"{r['cells_over_40']} | {', '.join(f'({c[0]},{c[1]},{c[2]})' for c in r['worst_cells'][:3])} |")
    (kit / "outputs" / "audit.md").write_text("\n".join(lines) + "\n")
    return {"checked": len(rows), "flags": {k: len(v) for k, v in flags.items()}, "worst": [(r["id"], r["cells_over_40"]) for r in worst[:12]]}




# ---------------------------------------------------------------------------
# build-variants: the second kit, for the 22 layout variants (docs/design/tactical-map-hd-kit.md §1.2)

# What the changed cells of each variant show (docs/design/tactical-map-hd-kit.md §1.2), for the prompt.
VARIANT_CHANGES = {
    **{v: "the rocky asteroid Axis with its blue engine glow has moved one step closer to the Earth: the cells where it used to be "
          "are now empty starfield, the cells where the pixel art shows it now get the asteroid, rock texture and glow matching the "
          "painted one elsewhere" for v in (121, 123, 125, 127, 129, 131, 133, 135)},
    136: "the rocky asteroid Axis has reached the Earth and is striking its edge: paint the asteroid pressed against the planet's limb "
         "with a burst of impact debris and glow exactly where the pixel art puts them",
    **{v: "the winged fortress (a dark asteroid base with two great mechanical wings) has moved one step closer to the Earth: the cells "
          "it left are empty starfield, the cells where the pixel art shows it now get the fortress" for v in (141, 143, 145, 147, 149, 151, 153, 155)},
    156: "the winged fortress has reached the Earth's edge and its old place is empty starfield; paint it against the planet's limb "
         "exactly where the pixel art shows it",
    39: "a bulkhead in the fortress corridor has changed state (a door section of the metal wall): paint the metal panels and door "
        "frame exactly as the pixel art shows, matching the surrounding deck",
    117: "the space colony (the cylinder with its ring of mirrors) has been destroyed: the cells now show its shattered wreckage, "
         "broken hull sections, scattered mirror panels and debris drifting on the starfield, no intact colony",
    118: "the space colony (the cylinder with its ring of mirrors) has been destroyed: the cells now show its shattered wreckage, "
         "broken hull sections, scattered mirror panels and debris drifting on the starfield, no intact colony",
    137: "the space station (a long hull with radial spokes) has been destroyed: the cells now show its broken wreckage and drifting "
         "debris on the starfield, no intact station",
}

VARIANT_PROMPT = (
    "Use case: inpainting a painted game map. Image 1 is a crop of a finished hand-painted battlefield map for a 1999 tactical RPG "
    "(map {root} of the {family} family), except for a few cells that are still the old pixel art: they look blocky and pixelated. "
    "Image 2 is a mask of the SAME crop: white = the pixelated cells to repaint, black = finished painting.\n"
    "Repaint ONLY the pixelated (white in Image 2) cells so they show what the pixel art shows there ({change}), painted in exactly the "
    "style, scale, colours and lighting of the surrounding finished painting, blending seamlessly at the cell edges. Keep every feature's "
    "position, outline and size as in the pixel art. Everything outside those cells must stay pixel-identical to Image 1: do not "
    "restyle, brighten, sharpen or touch it.\n"
    "Absolutely no text, labels, units, cursors, grid lines, frame, border or UI. Same crop, same aspect ratio as Image 1 ({w}x{h}), "
    "fill the canvas.")


def build_variants(rom: bytes, kit: Path, bundle: Path, out: Path) -> dict:
    """Windows around each variant's changed cells: the root's composed HD base with those
    cells shown as hard pixels of the variant, plus a mask, for image_gen to repaint."""
    from PIL import ImageChops
    out.mkdir(parents=True, exist_ok=True)
    (out / "outputs").mkdir(exist_ok=True)
    manifest = {"schema": "srw64.tactical-kit-variants.v1", "rom_sha256": ROM_SHA256, "cell": CELL, "scale": SCALE,
                "bundle": str(bundle), "variants": []}
    dynamics = {m["map"]: m for m in json.loads(DYNAMICS.read_text())["maps"]}
    font = ImageFont.truetype("/System/Library/Fonts/Hiragino Sans GB.ttc", 22)
    thumbs = []
    for variant, root in sorted(VARIANTS.items()):
        r, v = load_map(rom, root), load_map(rom, variant)
        (w, h), cells_r = cell_grid(r["layout_bytes"])
        _, cells_v = cell_grid(v["layout_bytes"])
        cw, ch = w // 2, h // 2
        changed = [(i % cw, i // cw) for i, (a, b) in enumerate(zip(cells_r, cells_v)) if a != b]
        if not changed:
            continue
        family, zh, _, features, _ = FAMILIES[r["atlas"]]
        base_path = bundle / f"map-{root:03d}" / "base.png"
        base = Image.open(base_path).convert("RGB")
        if base.size != (w * 8 * SCALE, h * 8 * SCALE):
            raise ValueError(f"composed base for map {root} is not {SCALE}x")
        v_source = render(v["indices"], v["palette"])
        box = paint_box(cells_r, cw, ch)
        # Windows of WINDOW cells over the changed cells' bounding box, clamped to the paint area.
        xs, ys = [c[0] for c in changed], [c[1] for c in changed]
        bx0, by0, bx1, by1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
        starts_x = list(range(bx0, bx1, WINDOW[0])) if bx1 - bx0 > WINDOW[0] else [bx0 + (bx1 - bx0) // 2 - WINDOW[0] // 2]
        starts_y = list(range(by0, by1, WINDOW[1])) if by1 - by0 > WINDOW[1] else [by0 + (by1 - by0) // 2 - WINDOW[1] // 2]
        windows_c = []
        for sy in starts_y:
            for sx in starts_x:
                x0 = min(max(sx, box[0]), box[2] - WINDOW[0]); y0 = min(max(sy, box[1]), box[3] - WINDOW[1])
                wb = (x0, y0, x0 + WINDOW[0], y0 + WINDOW[1])
                if any(wb[0] <= cx < wb[2] and wb[1] <= cy < wb[3] for cx, cy in changed) and wb not in windows_c:
                    windows_c.append(wb)
        stem = f"map-{variant:03d}"
        folder = out / family
        folder.mkdir(exist_ok=True)
        entry = {"variant": variant, "root": root, "family": family, "layout": v["layout"], "root_layout": r["layout"],
                 "scene": scene_label(dynamics[variant]) if dynamics[variant]["initial_for"] else f"switched in by 3D34 from map {root}",
                 "changed_cells": changed, "bbox_cells": [bx0, by0, bx1, by1], "windows": []}
        covered = set()
        for n, wb in enumerate(windows_c, 1):
            px = tuple(c * CELL for c in wb)
            hd = tuple(c * SCALE for c in px)
            crop = base.crop(hd).copy()
            mask = Image.new("L", crop.size, 0)
            draw_mask = ImageDraw.Draw(mask)
            kinds = set()
            for cx, cy in changed:
                if not (wb[0] <= cx < wb[2] and wb[1] <= cy < wb[3]):
                    continue
                covered.add((cx, cy))
                sx, sy = (cx - wb[0]) * CELL * SCALE, (cy - wb[1]) * CELL * SCALE
                pixel = v_source.crop((cx * CELL, cy * CELL, (cx + 1) * CELL, (cy + 1) * CELL)).resize((CELL * SCALE, CELL * SCALE), Image.NEAREST)
                crop.paste(pixel, (sx, sy))
                draw_mask.rectangle((sx, sy, sx + CELL * SCALE - 1, sy + CELL * SCALE - 1), fill=255)
            crop.save(folder / f"{stem}-{n:02d}-input.png", optimize=True)
            mask.save(folder / f"{stem}-{n:02d}-mask.png", optimize=True)
            change = VARIANT_CHANGES.get(variant) or "the terrain, structures or wreckage the pixel art shows in those cells"
            (folder / f"{stem}-{n:02d}-prompt.txt").write_text(VARIANT_PROMPT.format(root=root, family=zh + " / " + family, change=change,
                                                                                     w=crop.width, h=crop.height) + "\n")
            entry["windows"].append({"id": f"{stem}-{n:02d}", "box_cells": list(wb), "box": list(px),
                                     "input": f"{family}/{stem}-{n:02d}-input.png", "input_sha256": sha(folder / f"{stem}-{n:02d}-input.png"),
                                     "mask": f"{family}/{stem}-{n:02d}-mask.png", "prompt": f"{family}/{stem}-{n:02d}-prompt.txt",
                                     "output": f"outputs/{stem}-{n:02d}-out.png"})
        entry["uncovered_cells"] = [c for c in changed if c not in covered]
        manifest["variants"].append(entry)
        # thumbnail: the variant source with the changed cells and windows outlined
        tile = v_source.copy()
        d = ImageDraw.Draw(tile)
        for cx, cy in changed:
            d.rectangle((cx * CELL, cy * CELL, (cx + 1) * CELL - 1, (cy + 1) * CELL - 1), outline=(255, 0, 0))
        for wn in entry["windows"]:
            b = wn["box"]; d.rectangle((b[0], b[1], b[2] - 1, b[3] - 1), outline=(0, 255, 255))
        d.text((4, 4), f"{stem} ← map-{root:03d}  {len(changed)} cells  {len(entry['windows'])} windows", fill=(255, 255, 0), font=font)
        tile.thumbnail((480, 480))
        thumbs.append(tile)
    columns = 6
    rows = (len(thumbs) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * 490, rows * 490), (30, 30, 30))
    for i, t in enumerate(thumbs):
        sheet.paste(t, ((i % columns) * 490 + 5, (i // columns) * 490 + 5))
    sheet.save(out / "index.jpg", quality=80)
    total = sum(len(e["windows"]) for e in manifest["variants"])
    (out / "README.md").write_text(f"""# 战术地图变体 image_gen 生成包

{__import__('datetime').date.today().isoformat()}。22 张 3D34 变体地图与各自的根图只差几十个格子（`docs/design/tactical-map-hd-kit.md` §1.2）。这个包不重画整张，只补画变化的格子，共 {total} 张。缩略图 `index.jpg`：红框是变化的格子，青框是窗口。

## 每一张怎么画

1. 图 1：`*-input.png`。这是根图已合成好的 HD 画面裁出的窗口，只有变化的格子换成了变体的原版像素（看起来是马赛克块）。
2. 图 2：同名的 `*-mask.png`，白色就是要补画的格子。
3. 提示词：同名的 `*-prompt.txt` 整段贴入。要求只重画马赛克格子，画成周围一样的画风；其余部分保持原样。
4. 结果按 `manifest.json` 里的 `output` 存进 `outputs/`（`map-NNN-MM-out.png`）。

合成时只取变化格加 1 格羽化，窗口其余部分一律丢弃，所以模型把别处稍微改了也没关系；但马赛克格子里画的内容必须与原版像素一致（位置、轮廓、类型）。

## 检查

- 白色格子里的东西与原版像素一致，没有多画或漏画。
- 画风、颜色、亮度与周围一致，接缝看不出来。
- 没有文字、单位、光标、网格、边框。
""")
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n")
    return {"variants": len(manifest["variants"]), "windows": total,
            "uncovered": {e["variant"]: e["uncovered_cells"] for e in manifest["variants"] if e["uncovered_cells"]}}




# ---------------------------------------------------------------------------
# compose-variants: variant runtime assets from the root's composed base plus the repainted cells

def variant_reference(root: Image.Image, pixels: Image.Image, mask: Image.Image, radius: int = 192) -> Image.Image:
    """Low-frequency colour target for a repainted variant window: the variant's pixel art where
    it shows something, and elsewhere in the changed cells the root's own background, filled in
    from the unchanged dark surroundings (normalised blur), so neither the old object nor the
    pixel art's flat black shows through."""
    from PIL import ImageChops, ImageFilter, ImageMath
    dark = root.convert("L").point(lambda v: 255 if v < 40 else 0)
    weight = ImageChops.multiply(dark, ImageChops.invert(mask))
    blur = lambda im: im.filter(ImageFilter.BoxBlur(radius))
    w = blur(weight)
    channels = []
    for c in ImageChops.multiply(root, Image.merge("RGB", [weight] * 3)).split():
        num = blur(c)
        channels.append(ImageMath.lambda_eval(lambda a: a["n"] * 255 / (a["w"] + 1), n=num.convert("F"), w=w.convert("F")).convert("L"))
    background = Image.merge("RGB", channels)
    shown = pixels.convert("L").point(lambda v: 255 if v >= 30 else 0)
    fill = Image.composite(pixels, background, shown)
    return Image.composite(fill, root, mask)


def compose_variants(rom: bytes, kit: Path, bundle: Path, out: Path, only: set[int] | None) -> dict:
    """For each variant: the root's composed base, with the changed cells taken from the
    repainted windows (registered, one cell of feather), or from the pixel-scaled index
    render when a window was not painted; palette-cycled pixels, the border and colony
    blocks restored exactly as the root compose does. Writes runtime folders like the
    root bundle's, with the variant's own layout number."""
    from PIL import ImageChops, ImageFilter
    from tools.hd_ai.tactical_colony_pack import colony_instances
    from tools.hd_ai.tactical_map_hd import best_shift
    manifest = json.loads((kit / "manifest.json").read_text())
    results = []
    for entry in manifest["variants"]:
        variant, root = entry["variant"], entry["root"]
        if only and variant not in only:
            continue
        data = load_map(rom, variant)
        width, height = data["indices"].size
        base = Image.open(bundle / f"map-{root:03d}" / "base.png").convert("RGB")
        if base.size != (width * SCALE, height * SCALE):
            raise ValueError(f"root base for map {root} does not match variant {variant}'s size")
        index4 = magnify(data["indices"], data["palette"], SCALE)
        instances = colony_instances(data)
        background_index = None
        if instances:
            animated_indices = {i for c in data["channels"] for i in range(c["first_index"], c["first_index"] + c["count"])}
            histogram = data["indices"].histogram()
            neutral = [i for i, c in enumerate(data["palette"]) if c == (0, 0, 0, 255) and i not in animated_indices]
            background_index = max(neutral, key=lambda i: histogram[i])
            for x, y in instances:
                index4.paste(background_index, (x * SCALE, y * SCALE, (x + 64) * SCALE, (y + 48) * SCALE))
        flat = render(index4, data["palette"]).convert("RGB")
        result = base.copy()
        painted_cells, pixel_cells, windows = set(), set(), []
        for wn in entry["windows"]:
            box = wn["box"]
            hd = tuple(v * SCALE for v in box)
            cells_here = [tuple(c) for c in entry["changed_cells"] if box[0] <= c[0] * CELL < box[2] and box[1] <= c[1] * CELL < box[3]]
            mask = Image.new("L", (hd[2] - hd[0], hd[3] - hd[1]), 0)
            d = ImageDraw.Draw(mask)
            for cx, cy in cells_here:
                sx, sy = (cx * CELL - box[0]) * SCALE, (cy * CELL - box[1]) * SCALE
                d.rectangle((sx, sy, sx + CELL * SCALE - 1, sy + CELL * SCALE - 1), fill=255)
            output = kit / wn["output"]
            report = {"id": wn["id"], "cells": len(cells_here)}
            if output.exists():
                source_in = Image.open(kit / wn["input"]).convert("RGB")
                generated = Image.open(output).convert("RGB").resize(source_in.size, Image.LANCZOS)
                (dx, dy), err = best_shift(source_in.resize((source_in.width // SCALE, source_in.height // SCALE), Image.BOX),
                                           generated.resize((source_in.width // SCALE, source_in.height // SCALE), Image.BOX), 3)
                generated = ImageChops.offset(generated, -dx * SCALE, -dy * SCALE)
                # The repaint's background differs slightly in level from the root base; keep
                # its detail but take the low-frequency colour from what is there now (root base,
                # the variant's pixel art in the changed cells), so the feathered margin leaves no box.
                reference = variant_reference(result.crop(hd), flat.crop(hd), mask)
                generated = color_lock(generated, reference, 16)
                # Half a cell of margin so the repaint's blending at the cell edges is kept, feathered.
                soft = mask.filter(ImageFilter.MaxFilter(CELL * SCALE // 2 + 1)).filter(ImageFilter.GaussianBlur(6))
                piece = Image.composite(generated, result.crop(hd), soft)
                painted_cells.update(cells_here)
                report.update(status="painted", shift_1x=[dx, dy], luma_error=round(err, 2), output_sha256=sha(output))
            else:
                piece = Image.composite(flat.crop(hd), result.crop(hd), mask)
                pixel_cells.update(cells_here)
                report["status"] = "pixel_fallback"
            result.paste(piece, hd[:2])
            windows.append(report)
        animated4, border = masks(data, index4)
        exact = ImageChops.lighter(animated4, border.resize(flat.size, Image.NEAREST))
        for x, y in instances:
            exact.paste(0, (x * SCALE, y * SCALE, (x + 64) * SCALE, (y + 48) * SCALE))
        result = Image.composite(flat, result, exact)
        folder = out / f"map-{variant:03d}"
        folder.mkdir(parents=True, exist_ok=True)
        result.save(folder / "base.png"); index4.save(folder / "index.png"); exact.save(folder / "protected.png")
        meta = {"schema": "srw64.hd-map-runtime.v0", "map": variant, "layout": data["layout"], "atlas": data["atlas"],
                "palette": data["palette_id"], "width": width, "height": height, "scale": SCALE,
                "source_run": f"tactical-kit-variants from map-{root:03d}", "rom_sha256": ROM_SHA256,
                "reference_palette": [list(c) for c in data["palette"]],
                "files": {n: sha(folder / n) for n in ("base.png", "index.png", "protected.png")},
                "acceptance_proven": False, "runtime_verified": False, "colony_animation_integrated": bool(instances)}
        if instances:
            meta.update(mode=data["mode"], colony_instances=instances, colony_background_index=background_index)
        (folder / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n")
        (folder / "composition.json").write_text(json.dumps({"variant": variant, "root": root, "root_base_sha256": sha(bundle / f"map-{root:03d}" / "base.png"),
                                                             "windows": windows, "painted_cells": len(painted_cells), "pixel_cells": len(pixel_cells)},
                                                            ensure_ascii=False, indent=2) + "\n")
        results.append({"variant": variant, "root": root, "painted_cells": len(painted_cells), "pixel_cells": len(pixel_cells)})
    return {"variants": results}




# ---------------------------------------------------------------------------
# pack: the runtime maps as one checked folder the art manifest points at

def pack_maps(source: Path, output: Path) -> dict:
    """Copy every map folder (base, index, meta) and the colony frames out of a runtime
    directory (symlinks resolved) into a new pack with an index of file hashes, for
    content/art/stage1-hd.json's "tactical_maps" (compile_art, src/srw64_native/assets.py)."""
    if output.exists():
        raise ValueError(f"{output} exists; packs are immutable, use a new folder")
    maps, colony = [], []
    staging = output.with_name(output.name + ".partial")
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    names = ("base.png", "index.png", "meta.json")
    for folder in sorted(p for p in source.iterdir() if p.name.startswith("map-")):
        meta = json.loads((folder / "meta.json").read_text())
        target = staging / folder.name
        target.mkdir()
        for name in names:
            shutil.copyfile((folder / name).resolve(), target / name)
        maps.append({"map": meta["map"], "layout": meta["layout"], "folder": folder.name,
                     "files": {name: sha(target / name) for name in names}})
    frames = source / "colony"
    if frames.exists():
        (staging / "colony").mkdir()
        for folder in sorted(p for p in frames.resolve().iterdir() if p.name.startswith("frame-")):
            target = staging / "colony" / folder.name
            target.mkdir()
            for name in names:
                shutil.copyfile(folder / name, target / name)
            colony.append({"folder": f"colony/{folder.name}", "files": {name: sha(target / name) for name in names}})
    layouts = [m["layout"] for m in maps]
    if len(set(layouts)) != len(layouts):
        raise ValueError("Two maps in the pack share a layout; the host keys assets by layout")
    index = {"schema": "srw64.tactical-maps.v1", "maps": maps, "colony_frames": colony,
             "note": "HD tactical maps (docs/design/tactical-map-hd-kit.md): per map a 4x painted base, the 4x palette-index map and the reference palette; colony overlay frames."}
    (staging / "tactical-maps.json").write_text(json.dumps(index, ensure_ascii=False, indent=1) + "\n")
    staging.rename(output)
    return {"maps": len(maps), "colony_frames": len(colony), "manifest_sha256": sha(output / "tactical-maps.json")}


if __name__ == "__main__":
    main()
