#!/usr/bin/env python3
"""Map unit icons in HD-2D style through image_gen: the kit to hand over, and the collector.

The 16x16 CI4 icon (docs/design/unit-icon-hd.md), a close-up of the machine's head (a ship:
its hull from above), is redrawn as a hand-pixelled sprite on a 128x128 grid (the icon's own
16 pixels times eight; spec v1 in that document), in the machine's real colours taken from
its HD standing pose. Each item hands image_gen two pictures: the original icon enlarged
(pose, facing, framing, silhouette) and the HD pose (colours, parts, style), plus a prompt.
Outputs come back as PNGs on flat grey; `collect --raw` measures the model's own pixel pitch,
so the raw picture (1254 or 1024) is read without any resizing in between.

    python3 tools/hd_ai/unit_icon_imagegen.py prepare --output assets/hd-ai/unit-icon-imagegen/hd2d-2
    python3 tools/hd_ai/unit_icon_imagegen.py collect --output assets/hd-ai/unit-icon-imagegen/hd2d-2 --raw --colors 40

`collect` keys the grey out, reads one colour per block, writes results/<rid>.png (128x128
RGBA) with a x4 copy and results/report.json (`grid_error` says how far a drawing was from
its grid, `pitch` what the block size was), and review.jpg (original, current HD, new, raw
output). Faction marking is a later step; these are the ally masters.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ICONS = ROOT / "assets/hd-ai/unit-icons/v2"
POSES = ROOT / "assets/hd-ai/unit-poses/all-1"
GREY = (100, 100, 112)
# Spec v1 (docs/design/unit-icon-hd.md, HD-2D): the master is a 128x128 pixel-art grid, the
# most the game can show 1:1 (resolution scale 8); at the default scale 4 it shows at 2:1.
GRID = 128           # sprite pixels per side
BLOCK = 8            # output pixels per sprite pixel
OUT_SIDE = GRID * BLOCK   # 1024
ICON_SCALE = OUT_SIDE // 16   # one original pixel = 64 output pixels = an 8x8 sprite-pixel area

# The nine samples unit-icon-hd.md used, (icon resource, a unit that carries it).
SAMPLES = [(737, 55), (944, 262), (853, 171), (929, 247), (779, 97), (733, 51), (878, 196), (838, 156), (692, 5)]

PROMPT = """Use case: pixel-art sprite for an HD-2D remaster (the look of Octopath Traveler: hand-drawn pixel sprites in a detailed world). Asset: the map unit icon of one machine from a 1999 Japanese tactical RPG, redrawn as a crisp high-resolution pixel-art sprite on a 128x128 pixel grid.
Image 1 is the original 16x16 icon, enlarged 64 times: every block in it is one old pixel and corresponds to an 8x8 area of your 128x128 grid. These icons are close-ups: usually the machine's head and face filling the frame (a warship is its whole hull seen from above). Keep EXACTLY Image 1's framing and crop, facing direction, silhouette, and the position and size of every part (eyes, antenna, horns, vents, jaw); do not zoom out to a full body. Image 1 is tinted blue for the player's side: do NOT copy its colours.
Image 2 is the same machine's full standing illustration: find the part Image 1 shows (normally the head) and take its real colours, markings and details from there, and nothing else; do not copy Image 2's framing or pose.
Draw the sprite as clean pixel art: every sprite pixel is a uniform square block aligned to one 128x128 grid over the whole canvas (output 1024x1024 makes each block exactly 8x8), hard block edges, no anti-aliasing, blur, gradients or sub-block detail, and the same block size everywhere. Use a deliberate palette for this sprite (about 24 to 40 colours): a dark 1-pixel outline, flat colours with two to four shading tones per part, light from the upper left, a few bright highlights, dithering only where Image 1 has it. Use the finer grid for crisp detail (panel lines, vents, eye highlights), not for softness. The silhouette should read clearly at a glance and fill about the same area of the canvas as in Image 1.
Background: flat solid grey RGB(100,100,112) everywhere outside the sprite, no ground shadow, no base, no glow.
Critical exclusions: no text, logos, effects, beams, extra weapons or parts, extra characters, frame or border. Not a smooth illustration, not a 3D render, not a photo: pixel art only."""


LIBRARY = ROOT / "web/.data/library.json"


def names() -> dict[str, list[str]]:
    return json.loads((ICONS / "names.json").read_text(encoding="utf-8"))


def library() -> tuple[dict[int, dict], dict[int, dict]]:
    """The library's units by id and series by index (web/.data/library.json), or empty."""
    if not LIBRARY.is_file():
        return {}, {}
    data = json.loads(LIBRARY.read_text(encoding="utf-8"))
    return {u["id"]: u for u in data["units"]}, {i: s for i, s in enumerate(data["series"])}


def symmetry(rid: int) -> float:
    """How much of the icon's silhouette differs from its mirror image (0 = symmetric). The
    silhouette, not the colours: shading from the upper left makes every frontal face differ
    in colour from its mirror, but a frontal face has a symmetric outline and a turned one
    does not. The best horizontal shift within one pixel is taken, so an off-centre icon counts."""
    alpha = np.asarray(Image.open(ICONS / "idx" / f"{rid}.png").convert("L")) > 0
    best = 1.0
    for shift in (-1, 0, 1):
        mirrored = np.roll(alpha[:, ::-1], shift, axis=1)
        either = alpha | mirrored
        if either.any():
            best = min(best, float((alpha != mirrored)[either].mean()))
    return best


def item_hint(rid: int, uids: list[int], units: dict[int, dict], series: dict[int, dict]) -> str:
    """The per-item lines after the common prompt: what the machine is and what Image 1 shows."""
    lines = []
    named = [units[u] for u in uids if u in units]
    if named:
        u = named[0]
        name = u["name"]
        work = series.get(u.get("series"), {}).get("name", {})
        lines.append(f"This machine: {name.get('en') or name.get('ja')} ({name.get('ja')}" + (f", model {u['model']}" if u.get("model") else "") + ")"
                     + (f" from {work.get('en')}" if work.get("en") else "") + f"; size class {u.get('size', '?')}.")
        if len(named) > 1:
            lines.append("The same icon is also used for: " + ", ".join(x["name"].get("en") or x["name"].get("ja") for x in named[1:4]) + ".")
        if u.get("size") == "LL":
            lines.append("Size LL: this may be a warship or a very large machine. If Image 1 shows a hull seen from above rather than a face, draw the whole craft from above exactly as Image 1 frames it.")
    asym = symmetry(rid)
    if asym < 0.08:
        lines.append("Image 1's outline is left-right symmetric: a frontal view. Keep it symmetric (the shading may differ side to side).")
    elif asym < 0.2:
        lines.append(f"Image 1's outline is roughly symmetric ({round(asym * 100)}% of the silhouette differs from its mirror): most likely a frontal view, possibly turned a little. Follow Image 1 exactly either way.")
    else:
        lines.append(f"Image 1's outline is NOT symmetric (about {round(asym * 100)}% of the silhouette differs from its mirror): the head is turned or seen from the side. Keep that exact orientation; do not make it frontal or mirror it.")
    return "\n".join(lines)


def unit_name(rid: int, uid: int) -> str:
    for entry in names()[str(rid)]:
        number, _, label = entry.partition(":")
        if int(number) == uid:
            return label
    return names()[str(rid)][0].partition(":")[2]


def pose_file(uid: int) -> Path:
    samples = json.loads((POSES / "samples.json").read_text(encoding="utf-8"))["samples"]
    for row in samples:
        if uid in row["units"]:
            return POSES / "hd" / f"{row['id']}.png"
    raise FileNotFoundError(f"no HD pose for unit {uid}")


def on_grey(image: Image.Image, side: int) -> Image.Image:
    """The picture's opaque content centred on a grey square of `side`, as large as fits with a margin."""
    image = image.convert("RGBA")
    bbox = image.getchannel("A").getbbox() or (0, 0, image.width, image.height)
    content = image.crop(bbox)
    fit = int(side * 0.88)
    k = min(fit / content.width, fit / content.height)
    content = content.resize((max(1, round(content.width * k)), max(1, round(content.height * k))), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (side, side), GREY + (255,))
    canvas.alpha_composite(content, ((side - content.width) // 2, (side - content.height) // 2))
    return canvas.convert("RGB")


def icon_input(rid: int) -> Image.Image:
    icon = Image.open(ICONS / "rgb" / f"{rid}-1010.png").convert("RGBA")
    big = icon.resize((16 * ICON_SCALE, 16 * ICON_SCALE), Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", big.size, GREY + (255,))
    canvas.alpha_composite(big)
    return canvas.convert("RGB")


def nearest(image: Image.Image, k: int) -> Image.Image:
    return image.resize((image.width * k, image.height * k), Image.Resampling.NEAREST)


def label_font(size: int):
    for path in ("/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def all_items() -> list[tuple[int, int]]:
    """Every icon with the first of its units that has an HD pose, in icon order."""
    samples = json.loads((POSES / "samples.json").read_text(encoding="utf-8"))["samples"]
    posed = {u for row in samples for u in row["units"]}
    items = []
    for rid, labels in sorted(names().items(), key=lambda kv: int(kv[0])):
        uids = [int(label.partition(":")[0]) for label in labels]
        uid = next((u for u in uids if u in posed), None)
        if uid is None:
            print(f"warning: icon {rid} ({labels[0]}) has no HD pose; skipped", file=sys.stderr)
            continue
        items.append((int(rid), uid))
    return items


def index_sheet(path: Path, rows: list[tuple[str, str, int, int]], title: str) -> None:
    """An overview: the original x16, the current HD 64 x4, the pose, and the labels."""
    cell, pad = 256, 8
    font = label_font(20)
    sheet = Image.new("RGB", (pad + 3 * (cell + pad) + 420, pad + len(rows) * (cell + pad) + 20), (40, 40, 48))
    draw = ImageDraw.Draw(sheet)
    for r, (key, name, rid, uid) in enumerate(rows):
        y = pad + 20 + r * (cell + pad)
        original = nearest(Image.open(ICONS / "rgb" / f"{rid}-1010.png").convert("RGBA"), 16)
        hd = nearest(Image.open(ICONS / "hd" / f"{rid}-1010.png").convert("RGBA"), 4)
        pose = on_grey(Image.open(pose_file(uid)), cell)
        for c, image in enumerate((original, hd, pose)):
            tile = Image.new("RGBA", (cell, cell), GREY + (255,))
            tile.alpha_composite(image.convert("RGBA"), ((cell - image.width) // 2, (cell - image.height) // 2))
            sheet.paste(tile.convert("RGB"), (pad + c * (cell + pad), y))
        draw.text((pad + 3 * (cell + pad) + 8, y + 8), f"{key}\n{name}\nunit {uid}", fill=(235, 235, 240), font=font)
    draw.text((pad, 2), f"{title}   original x16   |   current HD 64 x4   |   HD pose (Image 2)", fill=(180, 180, 190), font=label_font(14))
    sheet.save(path, quality=88)


def prepare(args: argparse.Namespace) -> None:
    out = args.output
    (out / "items").mkdir(parents=True, exist_ok=True)
    (out / "outputs").mkdir(exist_ok=True)
    (out / "raw-generated").mkdir(exist_ok=True)
    items = all_items() if args.all else SAMPLES
    units, series = library()
    width = 3 if len(items) > 99 else 2
    manifest = {"schema": "srw64.unit-icon-imagegen.v1", "grid": GRID, "block": BLOCK, "output_side": OUT_SIDE, "grey": GREY,
                "batch_size": args.batch_size, "items": []}
    rows = []
    for n, (rid, uid) in enumerate(items, 1):
        key = f"{n:0{width}d}-{rid}"
        icon_input(rid).save(out / "items" / f"{key}-icon.png")
        on_grey(Image.open(pose_file(uid)), OUT_SIDE).save(out / "items" / f"{key}-pose.png")
        uids = [int(label.partition(":")[0]) for label in names()[str(rid)]]
        hint = item_hint(rid, uids, units, series)
        (out / "items" / f"{key}-prompt.txt").write_text(PROMPT + "\n" + hint + "\n", encoding="utf-8")
        name = unit_name(rid, uid)
        manifest["items"].append({"key": key, "rid": rid, "unit": uid, "units": uids, "name": name, "batch": (n - 1) // args.batch_size + 1,
                                  "icon": f"items/{key}-icon.png", "pose": f"items/{key}-pose.png", "prompt": f"items/{key}-prompt.txt",
                                  "output": f"outputs/{key}-out.png", "raw": f"raw-generated/{key}.png", "pose_source": str(pose_file(uid).relative_to(ROOT))})
        rows.append((key, name, rid, uid))
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    batches = (len(rows) + args.batch_size - 1) // args.batch_size
    if batches == 1:
        index_sheet(out / "index.jpg", rows, "")
    else:
        for b in range(batches):
            chunk = rows[b * args.batch_size:(b + 1) * args.batch_size]
            index_sheet(out / f"index-{b + 1:02d}.jpg", chunk, f"batch {b + 1}/{batches}")
    (out / "README.md").write_text(README.replace("{BATCHES}", str(batches)).replace("{COUNT}", str(len(rows))), encoding="utf-8")
    print(f"kit: {out} ({len(rows)} items, {batches} batch(es))")


README = """# 地图机体图标 HD-2D：image_gen 入口包

共 {COUNT} 张，分 {BATCHES} 批（每批一张总览 `index-NN.jpg`；只有一批时是 `index.jpg`），`manifest.json` 的 `batch` 字段写了每张属于哪一批。目标是「HD-2D 风格的像素画图标」，规格见 docs/design/unit-icon-hd.md 的 HD-2D 一节：128×128 像素网格上手点出来的精灵，机体用真实配色（颜色取自 HD 立绘），构图、朝向、轮廓照原版 16×16 图标。注意原版图标是**头部特写**（战舰是俯视整舰），不是全身像，重画时保持这个取景，别画成全身。`index.jpg` 是总览：左边原版图标，中间现在的 HD 64 版，右边 HD 立绘。

## 每一张怎么画

1. 图1：`items/NN-资源号-icon.png`（原版 16×16 图标放大 64 倍，每格就是一个旧像素，对应你 128 格里的 8×8）。它带蓝色的我方阵营色，**颜色不要照它**。
2. 图2：`items/NN-资源号-pose.png`（同一台机体的 HD 全身立绘，在里面找到图1画的那个部位，通常是头，只取颜色和细节，不要照它的取景）。
3. 提示词：`items/NN-资源号-prompt.txt` 整段贴进去。末尾几行是这一台的特征提示（名字、作品、尺寸、图 1 是否左右对称、LL 尺寸可能是战舰），是程序算的，和图 1 不符时以图 1 为准；你可以像上一轮那样再补一两句具体描述（朝向、哪些部位不许出现）。
4. 结果存为 `outputs/NN-资源号-out.png`，PNG。**模型给多大就存多大，不要再缩放**（最近邻缩放会把色块网格弄乱；上一轮 1254→1024 就是这样丢了精度），同一张原始输出也放一份在 `raw-generated/NN-资源号.png`。我回收时按图里的色块间距读取。

## 检查，不合格就重画

- 取景、朝向、轮廓和各部件位置大小与图1一致（头部特写就还是头部特写）。
- 真实配色（高达白蓝红黄、扎古绿），不是图1的蓝色调。
- 色块对齐同一张 128×128 网格，全图色块一样大，硬边，没有抗锯齿、渐变和比色块更细的细节；一张图大约 24 到 40 色，有一像素深色描边，两到四阶平涂阴影，光从左上来。
- 细网格用来画清楚的细节（面板线、散热口、眼睛高光），不是用来画柔和过渡。
- 背景纯灰 RGB(100,100,112)，没有影子、底座、光效。
- 没有文字、额外武器、额外人物、边框。

被版权过滤拒绝的记下编号告诉我，可以把提示词第一句改得更泛再试。

## 画完以后

告诉我一声，我跑 `collect --raw`：抠掉灰底、按测出的色块间距读成 128×128 母版、出 `results/` 和 `review.jpg`（原版、现 HD、新图、原始输出并排），再看阵营标记怎么加、要不要进游戏实机看。
"""


def key_grey(image: Image.Image, tolerance: int = 28) -> np.ndarray:
    """RGBA array with the flat grey made transparent."""
    rgb = np.asarray(image.convert("RGB"), dtype=np.int16)
    distance = np.abs(rgb - np.array(GREY, dtype=np.int16)).max(axis=2)
    alpha = np.where(distance <= tolerance, 0, 255).astype(np.uint8)
    return np.dstack([rgb.astype(np.uint8), alpha])


def border_background(cells: np.ndarray, tolerance: int = 28) -> np.ndarray:
    """Cells of the flat grey that reach the canvas edge through other grey cells: the
    background. Grey armour inside the sprite, or a gap a horn closes off, stays opaque."""
    near = np.abs(cells[:, :, :3].astype(np.int16) - np.array(GREY, dtype=np.int16)).max(axis=2) <= tolerance
    near |= cells[:, :, 3] == 0  # cells the canvas edge left empty join the background
    bg = np.zeros_like(near)
    bg[0], bg[-1], bg[:, 0], bg[:, -1] = near[0], near[-1], near[:, 0], near[:, -1]
    while True:
        grown = bg.copy()
        grown[1:] |= bg[:-1]
        grown[:-1] |= bg[1:]
        grown[:, 1:] |= bg[:, :-1]
        grown[:, :-1] |= bg[:, 1:]
        grown &= near
        if (grown == bg).all():
            return bg
        bg = grown


def to_grid(rgba: np.ndarray) -> tuple[np.ndarray, float]:
    """One colour per BLOCK x BLOCK cell (the median of its opaque pixels; opaque when most are),
    and how far the picture was from being on the grid (mean colour spread inside cells, 0 = exact)."""
    side = rgba.shape[0]
    block = side // GRID
    cells = rgba.reshape(GRID, block, GRID, block, 4).transpose(0, 2, 1, 3, 4).reshape(GRID, GRID, block * block, 4)
    out = np.zeros((GRID, GRID, 4), dtype=np.uint8)
    spread = []
    for y in range(GRID):
        for x in range(GRID):
            cell = cells[y, x]
            opaque = cell[cell[:, 3] > 0]
            if len(opaque) * 2 < len(cell):
                continue
            colour = np.median(opaque[:, :3], axis=0)
            out[y, x, :3] = colour.astype(np.uint8)
            out[y, x, 3] = 255
            spread.append(float(np.abs(opaque[:, :3] - colour).mean()))
    return out, (sum(spread) / len(spread) if spread else 0.0)


def edge_profile(rgb: np.ndarray, axis: int) -> np.ndarray:
    """How much the picture changes between neighbouring columns (axis 0) or rows (axis 1)."""
    gray = rgb.astype(np.float32).mean(axis=2)
    diff = np.abs(np.diff(gray, axis=1 - axis)) if axis == 0 else np.abs(np.diff(gray, axis=0))
    return diff.sum(axis=1 - axis) if axis == 0 else diff.sum(axis=1)


def best_offset(profile: np.ndarray, pitch: float) -> tuple[float, float]:
    """The phase whose predicted boundaries collect the most edge energy per boundary, and that energy."""
    n = len(profile)
    best = (0.0, 0.0)
    for offset in np.arange(0.0, pitch, 0.5):
        idx = np.clip(np.round(np.arange(offset, n, pitch)).astype(int), 0, n - 1)
        score = float(profile[idx].mean())
        if score > best[0]:
            best = (score, float(offset))
    return best[1], best[0]


def find_pitch(profile_x: np.ndarray, profile_y: np.ndarray, nominal: float) -> tuple[float, float, float]:
    """One square pitch for both axes (the model's pseudo-pixels are square even when a
    drawing's edges along one axis are weaker) and the phase on each: (pitch, offset x, offset y).
    The search runs from 0.6 to 3 times the nominal pitch (side / GRID): a model that drew a
    coarser grid (often half the cells, sometimes 22 to 26 px blocks) is read at its own
    pitch and scaled up afterwards. The nominal pitch, or twice it, wins whenever it scores
    within 8% of the best, since a picture nearly on an intended grid is more often that."""
    candidates = {}
    for pitch in np.arange(nominal * 0.6, nominal * 3.0, 0.02):
        ox, sx = best_offset(profile_x, pitch)
        oy, sy = best_offset(profile_y, pitch)
        candidates[round(float(pitch), 2)] = (sx + sy, ox, oy)
    best_pitch = max(candidates, key=lambda k: candidates[k][0])
    for intended in (nominal, nominal * 2):
        near = [k for k in candidates if abs(k - intended) <= 0.03]
        if near:
            n = min(near, key=lambda k: abs(k - intended))
            if candidates[n][0] >= 0.92 * candidates[best_pitch][0]:
                best_pitch = n
                break
    return best_pitch, candidates[best_pitch][1], candidates[best_pitch][2]


def grid_from_raw(image: Image.Image) -> tuple[np.ndarray, dict]:
    """The sprite read off the model's own pixel grid: the block pitch and phase are measured
    from the picture's edges along each axis (the model rarely lands exactly on 16 px), each
    cell takes the median of its inner half, the grey that reaches the edge becomes transparent
    (border_background), and the cells are centred on the master canvas."""
    rgb = np.asarray(image.convert("RGB"), dtype=np.uint8)
    rgba = np.dstack([rgb, np.full(rgb.shape[:2], 255, np.uint8)])
    px, ox, oy = find_pitch(edge_profile(rgb, 0), edge_profile(rgb, 1), rgba.shape[1] / GRID)
    py = px
    # Boundaries along each axis; the first boundary is pulled back to the picture's edge.
    xs = np.arange(ox - px * np.ceil(ox / px), rgba.shape[1] + px, px)
    ys = np.arange(oy - py * np.ceil(oy / py), rgba.shape[0] + py, py)
    cells = np.zeros((len(ys) - 1, len(xs) - 1, 4), dtype=np.uint8)
    spread = []
    for j in range(len(ys) - 1):
        for i in range(len(xs) - 1):
            x0, x1 = int(round(xs[i] + px * 0.25)), int(round(xs[i + 1] - px * 0.25))
            y0, y1 = int(round(ys[j] + py * 0.25)), int(round(ys[j + 1] - py * 0.25))
            x0, x1 = max(x0, 0), min(x1, rgba.shape[1])
            y0, y1 = max(y0, 0), min(y1, rgba.shape[0])
            if x1 <= x0 or y1 <= y0:
                continue
            cell = rgba[y0:y1, x0:x1].reshape(-1, 4)
            opaque = cell[cell[:, 3] > 0]
            if len(opaque) * 2 < len(cell):
                continue
            colour = np.median(opaque[:, :3], axis=0)
            cells[j, i, :3] = colour.astype(np.uint8)
            cells[j, i, 3] = 255
            spread.append(float(np.abs(opaque[:, :3] - colour).mean()))
    cells[border_background(cells)] = 0
    # Each master pixel takes the cell under its centre, so the sprite keeps the canvas's
    # framing (the original icon's) whatever pitch the model drew at; at 50-64 cells a
    # model block becomes 2-2.6 master pixels.
    centres_x = (np.arange(GRID) + 0.5) * rgba.shape[1] / GRID
    centres_y = (np.arange(GRID) + 0.5) * rgba.shape[0] / GRID
    ix = np.clip(np.floor((centres_x - xs[0]) / px).astype(int), 0, cells.shape[1] - 1)
    iy = np.clip(np.floor((centres_y - ys[0]) / py).astype(int), 0, cells.shape[0] - 1)
    out = cells[iy][:, ix]
    info = {"pitch": [round(px, 2), round(py, 2)], "cells": [cells.shape[1], cells.shape[0]], "grid_error": round(sum(spread) / len(spread), 2) if spread else 0.0}
    mask = out[:, :, 3] > 0
    if mask.any():
        ys_, xs_ = np.where(mask)
        info["sprite"] = [int(xs_.max() - xs_.min() + 1), int(ys_.max() - ys_.min() + 1)]
    return out, info


def quantize(rgba: np.ndarray, colors: int) -> np.ndarray:
    image = Image.fromarray(rgba[:, :, :3], "RGB")
    alpha = rgba[:, :, 3]
    quantized = image.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    out = np.dstack([np.asarray(quantized), alpha])
    out[alpha == 0] = 0
    return out


def collect(args: argparse.Namespace) -> None:
    kit = args.output
    manifest = json.loads((kit / "manifest.json").read_text(encoding="utf-8"))
    # A round of outputs may sit in a subfolder (v2/outputs); its results go beside it.
    out = kit / args.round if args.round else kit
    (out / "results").mkdir(parents=True, exist_ok=True)
    report, rows = [], []
    for item in manifest["items"]:
        path = out / item["output"]
        if not path.is_file():
            report.append({"key": item["key"], "status": "missing"})
            continue
        raw = out / "raw-generated" / f"{item['key']}.png"
        info = {}
        if args.raw and raw.is_file():
            grid, info = grid_from_raw(Image.open(raw).convert("RGB"))
            error = info["grid_error"]
            image = Image.open(raw).convert("RGB")
        else:
            image = Image.open(path).convert("RGB")
            if image.size != (OUT_SIDE, OUT_SIDE):
                image = image.resize((OUT_SIDE, OUT_SIDE), Image.Resampling.LANCZOS)
            grid, error = to_grid(key_grey(image))
        if args.colors:
            grid = quantize(grid, args.colors)
        result = Image.fromarray(grid, "RGBA")
        result.save(out / "results" / f"{item['rid']}.png")
        nearest(result, 4).save(out / "results" / f"{item['rid']}-x4.png")
        used = len({tuple(p) for p in grid[grid[:, :, 3] > 0][:, :3]})
        report.append({"key": item["key"], "status": "ok", "grid_error": round(error, 2), "colors": used,
                       "opaque": int((grid[:, :, 3] > 0).sum()), **{k: v for k, v in info.items() if k != "grid_error"}})
        rows.append((item, result, image))
    (out / "results" / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    # One review sheet per 40 sprites (a JPEG stops at 65535 px): review.jpg, or review-NN.jpg.
    pages = [rows[i:i + 40] for i in range(0, len(rows), 40)]
    for number, page in enumerate(pages, 1):
        cell, pad = 256, 8
        font = label_font(18)
        sheet = Image.new("RGB", (pad + 4 * (cell + pad) + 300, pad + len(page) * (cell + pad)), (40, 40, 48))
        draw = ImageDraw.Draw(sheet)
        for r, (item, result, raw) in enumerate(page):
            y = pad + r * (cell + pad)
            original = nearest(Image.open(ICONS / "rgb" / f"{item['rid']}-1010.png").convert("RGBA"), 16)
            hd = nearest(Image.open(ICONS / "hd" / f"{item['rid']}-1010.png").convert("RGBA"), 4)
            new = nearest(result, max(1, 256 // GRID))
            for c, image in enumerate((original, hd, new, raw.resize((cell, cell), Image.Resampling.BOX))):
                tile = Image.new("RGBA", (cell, cell), GREY + (255,))
                tile.alpha_composite(image.convert("RGBA"), ((cell - image.width) // 2, (cell - image.height) // 2))
                sheet.paste(tile.convert("RGB"), (pad + c * (cell + pad), y))
            entry = next(e for e in report if e["key"] == item["key"])
            draw.text((pad + 4 * (cell + pad) + 8, y + 8), f"{item['key']}\n{item['name']}\ngrid error {entry['grid_error']}\n{entry['colors']} colours", fill=(235, 235, 240), font=font)
        draw.text((pad, 2), f"original x16   |   current HD 64 x4   |   new {GRID} x{max(1, 256 // GRID)}   |   raw output", fill=(180, 180, 190), font=label_font(14))
        sheet.save(out / ("review.jpg" if len(pages) == 1 else f"review-{number:02d}.jpg"), quality=90)
    for entry in report:
        print(entry)

# Faction tones (bright, dark) from the icon palettes 1011-1013, entries 2 and 4. The
# player's side (1010) carries no mark: an unmarked icon is the player's.
FACTION_TONES = {1011: ((222, 123, 123), (156, 25, 25)), 1012: ((222, 206, 25), (140, 132, 8)),
                 1013: ((132, 123, 132), (74, 66, 74))}


def outside_ring(alpha: np.ndarray) -> np.ndarray:
    """Transparent pixels touching the sprite (4-neighbourhood)."""
    solid = alpha > 0
    near = np.zeros_like(solid)
    near[1:] |= solid[:-1]
    near[:-1] |= solid[1:]
    near[:, 1:] |= solid[:, :-1]
    near[:, :-1] |= solid[:, 1:]
    return near & ~solid


def faction_mark(master: Image.Image, palette: int) -> Image.Image:
    """The sprite in its own colours inside a two-pixel ring of its faction's tones,
    bright then dark (about one screen pixel at the default scale 4)."""
    if palette not in FACTION_TONES:
        return master
    rgba = np.asarray(master.convert("RGBA")).copy()
    for tone in FACTION_TONES[palette]:
        rgba[outside_ring(rgba[:, :, 3])] = tone + (255,)
    return Image.fromarray(rgba, "RGBA")


def pack(args: argparse.Namespace) -> None:
    """The collected masters into a copy of the RT64 pack, replacing its icon textures.

    Each icon is keyed four times, once per faction palette (1010 + faction, the parameter
    the map passes; docs/data/hd-asset-inventory.md), since the hash covers the palette.
    The sprites keep their real colours; the other sides get faction_mark (2026-10-25 user
    decision, after no mark, a thicker ring, a tint and a base were compared)."""
    from srw64_native.catalog import sha
    from srw64_rom.resources import ResourceTable
    from tools.hd_ai.esrgan_pose import fill_transparent
    from tools.hd_ai.rt64_hash import hasher
    from tools.hd_ai.unit_icon_hd import PALETTES
    from tools.hd_ai.worldmap_space import ci4_hash

    results, target = args.output / "results", args.pack_output
    resources = ResourceTable(args.rom.read_bytes())
    palettes = {p: resources.extract(p)[0][8:40] for p in PALETTES}
    xxh = hasher()
    shutil.copytree(args.base_pack, target)
    database = json.loads((target / "rt64.json").read_text())
    entries = {}
    for e in database["textures"]:
        if e["path"].startswith("icon-"):
            (target / e["path"]).unlink()
        else:
            entries[e["hashes"]["rt64"]] = e
    added, keys, shared = {}, [], []
    for item in sorted(json.loads((args.output / "manifest.json").read_text(encoding="utf-8"))["items"], key=lambda i: i["rid"]):
        rid = item["rid"]
        master = Image.open(results / f"{rid}.png").convert("RGBA")
        pixels = resources.extract(rid)[0][8:]
        for p in PALETTES:
            digest = ci4_hash(pixels, palettes[p], (16, 16), xxh)
            if digest in added:
                # Six pairs of icons have the same pixels in the ROM (738/770 Mk-II among them), so
                # one hash; the lower resource, the base form, keeps the picture.
                shared.append({"icon": rid, "palette": p, "hash": digest})
                continue
            if digest in entries:
                raise ValueError(f"{digest} already names {entries[digest]['path']}")
            image = faction_mark(master, p)
            filled = fill_transparent(image)
            filled.putalpha(image.getchannel("A"))
            name = f"icon-{digest}.png"
            filled.save(target / name)
            entries[digest] = {"hashes": {"rt64": digest}, "path": name}
            added[digest] = sha((target / name).read_bytes())
            keys.append({"icon": rid, "palette": p, "hash": digest})
    database["textures"] = sorted(entries.values(), key=lambda e: e["hashes"]["rt64"])
    (target / "rt64.json").write_text(json.dumps(database, indent=2) + "\n")
    (args.output / "pack-keys.json").write_text(json.dumps({"schema": "srw64.unit-icon-keys.v1", "pack": str(target), "keys": keys, "shared": shared}, indent=1))
    print(f"{len(added)} icon textures -> {target}, {len(shared) // len(PALETTES)} icons share another's picture")
    if args.bind:
        path = ROOT / "content/art/stage1-hd.json"
        manifest = json.loads(path.read_text())
        rows = [r for r in manifest["textures"] if r["kind"] != "icon"]
        rows += [{"hash": h, "kind": "icon", "sha256": v} for h, v in sorted(added.items())]
        manifest["textures"] = rows
        manifest["source"] = {"path": str(target.resolve().relative_to(ROOT)), "manifest_sha256": sha((target / "rt64.json").read_bytes())}
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print("bound", path.relative_to(ROOT), len(rows), "textures")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare", help="write the kit")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--all", action="store_true", help="every icon (320) instead of the nine samples")
    p.add_argument("--batch-size", type=int, default=40, help="items per batch and per index sheet (default 40)")
    p.set_defaults(run=prepare)
    c = sub.add_parser("collect", help="read the outputs back")
    c.add_argument("--output", type=Path, required=True)
    c.add_argument("--colors", type=int, default=0, help="quantize each sprite to this many colours (0 = keep)")
    c.add_argument("--round", default="", help="a subfolder holding this round's outputs/ (e.g. v2); results and review.jpg go there")
    c.add_argument("--raw", action="store_true", help="read raw-generated/<key>.png off the model's own pixel grid (measured) instead of the resized 1024 output")
    c.set_defaults(run=collect)
    k = sub.add_parser("pack", help="key the collected masters by RT64 hash into a copy of the pack")
    k.add_argument("--output", type=Path, required=True, help="the kit, with results/ from collect")
    k.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    k.add_argument("--base-pack", type=Path, required=True, help="the RT64 pack the art manifest names now")
    k.add_argument("--pack-output", type=Path, required=True, help="the new pack (must not exist)")
    k.add_argument("--bind", action="store_true", help="list the icons in content/art/stage1-hd.json and point it at the new pack")
    k.set_defaults(run=pack)
    args = parser.parse_args(argv)
    args.run(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
