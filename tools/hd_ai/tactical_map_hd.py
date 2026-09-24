"""Whole-map HD sample for one tactical map: prepare the request, then compose the result.

prepare  writes the 1x source, the palette-index map, protection masks, a 2x
         nearest-neighbour model input, the 4x index map (MMPX, index-only) and a
         frozen sample entry that tools/hd_ai/aliyun.py can send once.
compose  fits and undoes the model's slight enlargement, keeps the model's
         detail but the source's low-frequency colour, uses the model only outside
         the protected areas (palette-cycled pixels and the dark border), and
         renders the palette cycles from the ROM data as preview frames, the same
         way the runtime will: colour = live palette[index].
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageStat

from srw64_rom.resources import ResourceTable
from tools.content.map_dynamics import BORDER_TILES, CYCLE_NAMES, cell_grid, compose, index_atlas, table
from tools.hd_ai.pixel_scale import magnify

ROOT = Path(__file__).resolve().parents[2]
ROM_SHA256 = "ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e"
DEFAULT_OUT = ROOT / "assets/hd-ai/tactical-maps"
SCALE = 4
MAX_OUTPUT_AREA = 2048 * 2048          # qwen-image-3.0(-pro) output area limit
COLOR_LOCK_RADIUS = 6                  # HD pixels; 12 gave nearly the same result on map 20
PROMPT = (
    "对图1进行忠实的高清重绘，用作1999年二维战略游戏战术地图的高清底图。图1是俯视角的整张战场地图，每16像素为一格。"
    "严格保持每条道路、河流与岸线、建筑、森林、山地和荒地的位置、轮廓、宽度及整体构图，不可增加、删除或移动任何地物，"
    "道路、建筑和岸线的边缘要与原图位置对齐。保持原作偏暗的灰绿草地、灰色路面与城市建筑的色调和明暗，不要提高饱和度，"
    "不要改成卫星照片或三维渲染。只把像素噪点重画成细致的二维手绘地表：草地、树冠、岩石、屋顶和路面要有清晰的细节。"
    "水面画成平静、均匀的水面，不画波纹、倒影和泡沫。地图外圈的深色边框保持原样。保持原图的竖向画幅，完整保留所有边缘，"
    "不裁切、不旋转、不镜像。不添加文字、网格线、单位、光标、界面或任何新物体。")


def rgba5551(value: int) -> tuple[int, int, int, int]:
    return tuple(round(((value >> shift) & 31) * 255 / 31) for shift in (11, 6, 1)) + (255 * (value & 1),)


def load_map(rom: bytes, index: int) -> dict:
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("ROM differs from the pinned JP Rev 0 image")
    resources = ResourceTable(rom)
    spec = table("map_assets")
    layout_id, atlas_id, palette_id, aux1, aux2, mode, _ = struct.unpack_from(
        ">5H2B", rom, spec["rom_offset"] + index * spec["stride"])
    layout = resources.extract(layout_id)[0]
    palette_data = resources.extract(palette_id)[0]
    palette = [rgba5551(v) for (v,) in struct.iter_unpack(">H", palette_data[8:])]
    palette += [(0, 0, 0, 255)] * (256 - len(palette))
    indices, _, _ = compose(layout, index_atlas(resources.extract(atlas_id)[0]))
    channels = []
    for rid in (aux1, aux2):
        if not rid:
            continue
        data = resources.extract(rid)[0]
        frames, first, count = data[0], data[1], data[2]
        ticks = list(data[3:3 + frames])
        colors = [[rgba5551(struct.unpack_from(">H", data, 3 + frames + (c * frames + f) * 2)[0])
                   for c in range(count)] for f in range(frames)]
        channels.append({"resource": rid, "name": CYCLE_NAMES[rid], "first_index": first, "count": count,
                         "frame_ticks": ticks, "colors": colors})
    return {"map": index, "layout": layout_id, "atlas": atlas_id, "palette_id": palette_id, "mode": mode,
            "layout_bytes": layout, "indices": indices, "palette": palette, "channels": channels}


def render(indices: Image.Image, palette: list[tuple]) -> Image.Image:
    image = Image.frombytes("P", indices.size, indices.tobytes())
    image.putpalette([c for color in palette for c in color[:3]])
    return image.convert("RGB")


def masks(data: dict, indices: Image.Image) -> tuple[Image.Image, Image.Image]:
    """Animated-pixel mask (from indices at any scale) and border-cell mask at 1x."""
    animated = set()
    for channel in data["channels"]:
        animated.update(range(channel["first_index"], channel["first_index"] + channel["count"]))
    lut = [255 if i in animated else 0 for i in range(256)]
    animated_mask = indices.point(lut)
    (width, height), cells = cell_grid(data["layout_bytes"])
    border = Image.new("L", (width * 8, height * 8))
    draw = ImageDraw.Draw(border)
    columns = width // 2
    for i, (_, tile) in enumerate(cells):
        if tile in BORDER_TILES:
            x, y = (i % columns) * 16, (i // columns) * 16
            draw.rectangle((x, y, x + 15, y + 15), fill=255)
    return animated_mask, border


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(rom: bytes, index: int, out: Path) -> dict:
    data = load_map(rom, index)
    folder = out / f"map-{index:03d}"
    folder.mkdir(parents=True, exist_ok=True)
    width, height = data["indices"].size
    if width * height * SCALE * SCALE > MAX_OUTPUT_AREA:
        raise ValueError(f"map {index} at {SCALE}x exceeds the single-request output area; needs windows")
    source = render(data["indices"], data["palette"])
    source.save(folder / "source.png")
    data["indices"].save(folder / "source-index.png")
    animated, border = masks(data, data["indices"])
    animated.save(folder / "mask-animated.png")
    border.save(folder / "mask-border.png")
    source.resize((width * 2, height * 2), Image.NEAREST).save(folder / "input.png")
    index4 = magnify(data["indices"], data["palette"], SCALE)
    index4.save(folder / "index-4x.png")
    meta = {"schema": "srw64.tactical-map-hd.v0", "rom_sha256": ROM_SHA256,
            **{k: data[k] for k in ("map", "layout", "atlas", "palette_id", "mode")},
            "size": [width, height], "scale": SCALE,
            "channels": [{k: v for k, v in c.items() if k != "colors"} for c in data["channels"]],
            "files": {name: sha(folder / name) for name in
                      ("source.png", "source-index.png", "mask-animated.png", "mask-border.png", "input.png", "index-4x.png")}}
    (folder / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1) + "\n")
    samples_path = out / "samples.json"
    samples = json.loads(samples_path.read_text()) if samples_path.exists() else {
        "schema": "srw64.hd-ai-samples.v1", "samples": []}
    sample = {"id": f"map-{index:03d}", "input": f"map-{index:03d}/input.png", "prompt": PROMPT,
              "output_size": f"{width * SCALE}*{height * SCALE}", "input_sha256": meta["files"]["input.png"]}
    existing = next((s for s in samples["samples"] if s["id"] == sample["id"]), None)
    if existing and existing != sample:
        raise ValueError(f"{sample['id']} is already frozen with different inputs; use a new output folder")
    if not existing:
        samples["samples"].append(sample)
        samples_path.write_text(json.dumps(samples, ensure_ascii=False, indent=1) + "\n")
    return meta


def best_shift(reference: Image.Image, candidate: Image.Image, radius: int = 3) -> tuple[tuple[int, int], float]:
    ref = reference.convert("L").filter(ImageFilter.GaussianBlur(1))
    cand = candidate.convert("L").filter(ImageFilter.GaussianBlur(1))
    width, height = ref.size
    inner = (radius, radius, width - radius, height - radius)
    base = ref.crop(inner)
    scores = {}
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            moved = cand.crop((inner[0] + dx, inner[1] + dy, inner[2] + dx, inner[3] + dy))
            scores[(dx, dy)] = ImageStat.Stat(ImageChops.difference(base, moved)).mean[0]
    shift = min(scores, key=scores.get)
    return shift, scores[shift]


def fit_scale(source: Image.Image, generated: Image.Image, grid: int = 6, radius: int = 8) -> tuple[float, ...]:
    """Fit per-axis scale + offset between the source and a model output.

    The models return the requested size but draw the content slightly enlarged
    (map 20: 0 px drift at the top-left, 2-3 source px at the far edges). Window
    shifts are measured at 2x, then x' = ax*x + bx and y' = ay*y + by are fitted
    by least squares, in 4x output pixels.
    """
    ref = source.resize((source.width * 2, source.height * 2), Image.NEAREST)
    cand = generated.resize(ref.size, Image.BOX)
    samples = []
    width, height = ref.size
    step_x, step_y = width // grid, height // grid
    for gy in range(grid):
        for gx in range(grid):
            box = (gx * step_x, gy * step_y, (gx + 1) * step_x, (gy + 1) * step_y)
            window = (box[0] - radius, box[1] - radius, box[2] + radius, box[3] + radius)
            if window[0] < 0 or window[1] < 0 or window[2] > width or window[3] > height:
                continue
            (dx, dy), _ = best_shift(ref.crop(window), cand.crop(window), radius)
            samples.append(((box[0] + box[2]) / 2, (box[1] + box[3]) / 2, dx, dy))

    def line(points: list[tuple[float, float]]) -> tuple[float, float]:
        n = len(points)
        mean_x = sum(p for p, _ in points) / n
        mean_y = sum(q for _, q in points) / n
        slope = sum((p - mean_x) * (q - mean_y) for p, q in points) / sum((p - mean_x) ** 2 for p, _ in points)
        return slope, mean_y - slope * mean_x
    slope_x, offset_x = line([(cx, dx) for cx, _, dx, _ in samples])
    slope_y, offset_y = line([(cy, dy) for _, cy, _, dy in samples])
    # 2x shift d(p2) = s*p2 + o  ->  4x shift = 2*d(p4/2) = s*p4 + 2o
    return 1 + slope_x, 2 * offset_x, 1 + slope_y, 2 * offset_y


def align(generated: Image.Image, fit: tuple[float, ...]) -> Image.Image:
    ax, bx, ay, by = fit
    return generated.transform(generated.size, Image.AFFINE, (ax, 0, bx, 0, ay, by), resample=Image.BICUBIC)


def color_lock(generated: Image.Image, source: Image.Image, radius: float = COLOR_LOCK_RADIUS) -> Image.Image:
    """Keep the model's detail but the source's low-frequency colour: out = gen - blur(gen) + blur(src).

    Map 20's yellow-green crater came back grey-white from both models, and both
    brightened the map slightly; this restores hue and brightness per area.
    """
    smooth = source.resize(generated.size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(radius))
    detail = ImageChops.subtract(generated, generated.filter(ImageFilter.GaussianBlur(radius)), 1, 128)
    return ImageChops.add(detail, smooth, 1, -128)


def cycle_palette(data: dict, tick: int) -> list[tuple]:
    palette = list(data["palette"])
    for channel in data["channels"]:
        ticks = channel["frame_ticks"]
        position, frame = tick % sum(ticks), 0
        while position >= ticks[frame]:
            position -= ticks[frame]
            frame += 1
        for c, color in enumerate(channel["colors"][frame]):
            palette[channel["first_index"] + c] = color
    return palette


def compose_result(rom: bytes, index: int, out: Path, run: Path) -> dict:
    data = load_map(rom, index)
    folder = out / f"map-{index:03d}"
    request = json.loads((run / "request.json").read_text())
    if request.get("status") != "completed":
        raise ValueError(f"run {run.name} did not complete: {request.get('status')}")
    width, height = data["indices"].size
    target = (width * SCALE, height * SCALE)
    generated = Image.open(out / request["output"]).convert("RGB")
    if generated.size != target:
        generated = generated.resize(target, Image.LANCZOS)
    source = render(data["indices"], data["palette"])
    raw_shift, raw_error = best_shift(source, generated.resize(source.size, Image.BOX))
    fit = fit_scale(source, generated)
    generated = align(generated, fit)
    shift, error = best_shift(source, generated.resize(source.size, Image.BOX))
    generated = color_lock(generated, source)
    index4 = Image.open(folder / "index-4x.png")
    animated4, _ = masks(data, index4)
    _, border = masks(data, data["indices"])
    protected = ImageChops.lighter(animated4, border.resize(target, Image.NEAREST))
    # Feather two HD pixels so the model's land meets the palette-driven water without a hard seam.
    feather = protected.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5))
    flat = render(index4, data["palette"])
    base = Image.composite(flat, generated, feather)
    name = run.name.split("--", 1)[1]
    base_path = folder / f"base-4x--{name}.png"
    base.save(base_path)
    # Preview: one frame per 3 ticks over the longest cycle, cropped to the animated area.
    box = animated4.getbbox()
    frames = []
    if box:
        pad = 96
        box = (max(0, box[0] - pad), max(0, box[1] - pad), min(target[0], box[2] + pad), min(target[1], box[3] + pad))
        longest = max(sum(c["frame_ticks"]) for c in data["channels"])
        for tick in range(0, longest, 3):
            live = render(index4, cycle_palette(data, tick))
            frames.append(Image.composite(live, base, animated4).crop(box))
        frames[0].save(folder / f"cycle--{name}.gif", save_all=True, append_images=frames[1:],
                       duration=100, loop=0, optimize=True)
    report = {"run": run.name, "model_output_sha256": request.get("output_sha256"),
              "raw_shift_1x": raw_shift, "raw_mean_abs_luma_error_1x": round(raw_error, 2),
              "scale_fit_4x": {"x": [round(fit[0], 5), round(fit[1], 2)], "y": [round(fit[2], 5), round(fit[3], 2)]},
              "registration_shift_1x": shift, "mean_abs_luma_error_1x": round(error, 2),
              "color_lock_radius_4x": COLOR_LOCK_RADIUS,
              "source_mean_rgb": [round(v, 1) for v in ImageStat.Stat(source).mean],
              "result_mean_rgb": [round(v, 1) for v in ImageStat.Stat(base.resize(source.size, Image.BOX)).mean],
              "protected_fraction": round(ImageStat.Stat(protected).mean[0] / 255, 4),
              "base": base_path.name, "base_sha256": sha(base_path), "preview_frames": len(frames)}
    (folder / f"report--{name}.json").write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n")
    comparison(folder, source, base, name)
    return report


def comparison(folder: Path, source: Image.Image, base: Image.Image, name: str) -> None:
    """Side-by-side crops at 4x: original nearest-neighbour vs composed HD base."""
    font = ImageFont.truetype("/System/Library/Fonts/Hiragino Sans GB.ttc", 28)
    width, height = source.size
    crops = [(0, 0, width, height)]
    panels = []
    for box in crops:
        a = source.crop(box).resize(((box[2] - box[0]) * SCALE, (box[3] - box[1]) * SCALE), Image.NEAREST)
        b = base.crop(tuple(v * SCALE for v in box))
        panels.append((a, b))
    a, b = panels[0]
    sheet = Image.new("RGB", (a.width * 2 + 20, a.height + 50), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, (image, label) in enumerate([(a, "原图 最近邻 ×4"), (b, f"HD 底图（{name}）")]):
        sheet.paste(image, (i * (a.width + 20), 50))
        draw.text((i * (a.width + 20) + 8, 10), label, fill=(255, 255, 0), font=font)
    sheet.save(folder / f"compare--{name}.png")


def export(rom: bytes, index: int, out: Path, run: str, target: Path) -> dict:
    """Write the runtime files the host reads: base RGB, 4x index map, reference palette."""
    data = load_map(rom, index)
    folder = out / f"map-{index:03d}"
    name = run.split("--", 1)[1]
    base = Image.open(folder / f"base-4x--{name}.png").convert("RGB")
    index4 = Image.open(folder / "index-4x.png")
    width, height = data["indices"].size
    if base.size != (width * SCALE, height * SCALE) or index4.size != base.size:
        raise ValueError("composed base and index map must both be 4x the map")
    destination = target / f"map-{index:03d}"
    destination.mkdir(parents=True, exist_ok=True)
    base.save(destination / "base.png")
    index4.save(destination / "index.png")
    meta = {"schema": "srw64.hd-map-runtime.v0", "map": index, "layout": data["layout"], "atlas": data["atlas"],
            "palette": data["palette_id"], "width": width, "height": height, "scale": SCALE, "source_run": run,
            # Colours the base was composed with; the shader moves each pixel by live[idx] - reference[idx].
            "reference_palette": [list(color) for color in data["palette"]],
            "files": {"base.png": sha(destination / "base.png"), "index.png": sha(destination / "index.png")}}
    (destination / "meta.json").write_text(json.dumps(meta, ensure_ascii=False) + "\n")
    return {k: meta[k] for k in ("map", "layout", "width", "height", "scale", "source_run", "files")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=("prepare", "compose", "export"))
    parser.add_argument("--map", type=int, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--run", help="run folder name under OUTPUT/runs (compose, export)")
    parser.add_argument("--runtime", type=Path, default=DEFAULT_OUT / "runtime", help="export destination")
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if args.step == "prepare":
        print(json.dumps(prepare(rom, args.map, args.output), ensure_ascii=False, indent=1))
    elif args.step == "export":
        if not args.run:
            parser.error("export needs --run")
        print(json.dumps(export(rom, args.map, args.output, args.run, args.runtime), ensure_ascii=False, indent=1))
    else:
        if not args.run:
            parser.error("compose needs --run")
        print(json.dumps(compose_result(rom, args.map, args.output, args.output / "runs" / args.run),
                         ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
