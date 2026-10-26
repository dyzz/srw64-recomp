#!/usr/bin/env python3
"""Build the chapter title card fonts in content/fonts.

The original title cards are brush lettering (docs/native/native-title-and-story-images.md);
the host draws them natively, so each language gets a brush face:

- SRW64ChapterTitle.ttf (Japanese): the cards' own glyphs. Every card (resident table
  0x84B20) and every 第N話 scene (5106-5204) is rendered from the ROM; each line is
  cut into characters by its transcription (assets/transcriptions/chapter-titles.ja.json):
  a dynamic programme places the cuts where the column ink is lowest and each width is
  closest to the character's median width. Of a character's copies the medoid is kept,
  pieces of the neighbours are dropped, and the 24-row bitmap is upscaled and traced with
  potrace. One row is 1000/24 units, the bottom four sit under the baseline, and the
  advance is the cut width, so the original's proportional spacing comes back.
- MaShanZheng-Regular.ttf (Chinese and English): Ma Shan Zheng (SIL OFL 1.1, no Reserved
  Font Name), subset to the GB2312 level-1 hanzi, every Chinese chapter title, ASCII and
  the CJK punctuation. A title outside the subset falls back to HarmonyOS Sans.
- SRW64BrushTC.ttf (Traditional Chinese): Bakudai 1.49 (莫大毛筆, SIL OFL 1.1, Chun yu Yao,
  from Aoyagi Kouzan's brush face; github.com/max32002/bakudaifont, tw/Bakudai-Regular.ttf),
  subset the same way to the Big5 level-1 hanzi and every zh-Hant chapter title. Bakudai is
  a Reserved Font Name, so the subset, a Modified Version, is renamed; the copyright and
  licence records stay.

Needs numpy, scipy, potracer and fontTools (pip install numpy scipy potracer fonttools);
the built fonts are tracked, so this runs only when the cards or the titles change.

    python tools/content/build_title_fonts.py --mashanzheng /path/to/MaShanZheng-Regular.ttf \\
        --bakudai /path/to/Bakudai-Regular.ttf
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import re
import statistics
import sys

import numpy as np
import potrace
from PIL import Image
from scipy import ndimage
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools import subset

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_rom.resources import ResourceTable  # noqa: E402
from srw64_native.battle_graphics import parse_scene, read_triplets, render_scene  # noqa: E402
from srw64_native.original_images import decode_indexed  # noqa: E402

TRANSCRIPTION = ROOT / "assets/transcriptions/chapter-titles.ja.json"
FONTS = ROOT / "content/fonts"
NUMBER_SCENES = range(5106, 5205)   # 第N話 for N = scene - 5105, drawn from atlas 4989
NUMBER_ATLAS, PALETTE = 4989, 5105
ROWS, UPM, UPSCALE = 24, 1000, 12   # a line's glyph cell is 24 rows: tile row + 8 .. + 32
PX = UPM / ROWS
DESCENT = 4 * PX
PUNCTUATION = set("「」！、。？・…（）()!?")
SMALL_KANA = set("ァィゥェォャュョッっゃゅょぁぃぅぇぉ")


# ---- Cutting the cards into characters ----

def card_images(rom: bytes):
    """(image, glyph-cell tops, lines) for every title card and 第N話 scene."""
    table = ResourceTable(rom)
    data = lambda i: table.extract(i)[0]
    lines = {row["card"]: row["lines"] for row in json.loads(TRANSCRIPTION.read_text())["cards"]}
    jobs = [(scene, atlas, palette, lines[card])
            for card, (scene, atlas, palette) in enumerate(read_triplets(rom, "chapter_titles"))
            if scene not in NUMBER_SCENES]
    jobs += [(scene, NUMBER_ATLAS, PALETTE, [f"第{scene - NUMBER_SCENES.start + 1}話"]) for scene in NUMBER_SCENES]
    for scene, atlas, palette, text in jobs:
        parsed = parse_scene(data(scene))
        image = render_scene(parsed, decode_indexed(data(atlas), data(palette)))[0][0]
        top = parsed.bounds()[1]
        tops = []
        for row in sorted({part.y - top for part in parsed.frames[0]}):
            if not tops or row - tops[-1] > 4:   # the digits sit a pixel off their line
                tops.append(row)
        yield image, tops, [line.replace("<END>", "") for line in text]


def coverage(image: Image.Image) -> np.ndarray:
    """Ink coverage: the white letter is 1, its dark rim and the background 0."""
    pixels = np.asarray(image.convert("RGBA")).astype(float)
    light = pixels[..., :3].mean(-1) * (pixels[..., 3] > 0)
    return np.clip((light - 60) / 195, 0, 1)


def prior_width(char: str) -> float:
    if char in " 　":
        return 8
    if char in PUNCTUATION:
        return 10
    if char in SMALL_KANA:
        return 13
    if char.isdigit():
        return 12
    return 17 if "぀" <= char <= "ヿ" else 22


def cuts(ink: np.ndarray, chars: list[str], width) -> list[int]:
    """Column boundaries of `chars` across the inked span: low ink at each cut, widths near `width`."""
    inked = np.where(ink > 0.05)[0]
    start, span = inked[0], inked[-1] + 1 - inked[0]
    cost_at = [min(ink[start + i - 1], ink[start + i]) if 0 < i < span else 0 for i in range(span + 1)]
    best = np.full((len(chars) + 1, span + 1), np.inf)
    back = np.zeros((len(chars) + 1, span + 1), int)
    best[0, 0] = 0
    for k, char in enumerate(chars, 1):
        expected = width(char)
        for j in range(1, span + 1):
            for i in range(max(0, j - int(expected * 2.2) - 2), j):
                cost = best[k - 1, i] + ((j - i - expected) / expected) ** 2 * 8 + cost_at[j] * 3
                if cost < best[k, j]:
                    best[k, j], back[k, j] = cost, i
    bounds = [span]
    for k in range(len(chars), 0, -1):
        bounds.append(back[k, bounds[-1]])
    return [start + x for x in reversed(bounds)]


def glyph_bitmaps(rom: bytes) -> dict[str, np.ndarray]:
    """Each character's medoid copy as a ROWS-high coverage bitmap, as wide as its cut."""
    cards = [(coverage(image), tops, lines) for image, tops, lines in card_images(rom)]
    width = prior_width
    for _ in range(3):   # re-estimate every character's width from the previous cut
        copies, widths = collections.defaultdict(list), collections.defaultdict(list)
        for ink_map, tops, lines in cards:
            for top, text in zip(tops, lines):
                band = ink_map[top + 8:top + 8 + ROWS]
                ink = band.sum(0)
                if not text or ink.max() == 0:
                    continue
                bounds = cuts(ink, list(text), width)
                for char, x0, x1 in zip(text, bounds, bounds[1:]):
                    if char not in " 　":
                        widths[char].append(x1 - x0)
                        copies[char].append(band[:, x0:x1])
        medians = {char: statistics.median(w) for char, w in widths.items()}
        width = lambda char, medians=medians: medians.get(char, prior_width(char))
    glyphs = {}
    for char, found in copies.items():
        found = found[:40]
        wide = max(b.shape[1] for b in found)
        padded = [np.pad(b, ((0, ROWS - b.shape[0]), (0, wide - b.shape[1]))) for b in found]
        glyphs[char] = found[int(np.argmin([sum(np.abs(p - q).sum() for q in padded) for p in padded]))]
    return glyphs


# ---- Cleaning and tracing ----

def without_neighbours(bitmap: np.ndarray) -> np.ndarray:
    """Drop specks, and small pieces at a side of the cut that stand apart (a neighbour's stroke)."""
    bitmap = bitmap.copy()
    labels, count = ndimage.label(bitmap > 0.15, structure=np.ones((3, 3)))
    total = bitmap.sum()
    for k in range(1, count + 1):
        piece = labels == k
        ys, xs = np.where(piece)
        if bitmap[piece].sum() < 1.2:
            bitmap[piece] = 0
            continue
        if bitmap[piece].sum() >= 0.2 * total or (xs.min() > 0 and xs.max() < bitmap.shape[1] - 1):
            continue
        rest = np.where(((labels > 0) & ~piece).any(0))[0]
        if len(rest) and (xs.min() > rest.max() if xs.max() == bitmap.shape[1] - 1 else xs.max() < rest.min()):
            bitmap[piece] = 0
    # A narrow, tall run of columns past an empty column at either end: a neighbour's stroke.
    inked = np.where((bitmap > 0.15).any(0))[0]
    runs = np.split(inked, np.where(np.diff(inked) > 1)[0] + 1) if len(inked) else []
    for run in (runs[0], runs[-1]) if len(runs) > 1 else ():
        columns = bitmap[:, run[0]:run[-1] + 1]
        rows = np.where((columns > 0.15).any(1))[0]
        if len(run) <= 6 and rows[-1] - rows[0] >= 9 and columns.sum() < 0.25 * bitmap.sum():
            columns[:] = 0
    return bitmap


def repaired(char: str, bitmap: np.ndarray) -> np.ndarray:
    """Cuts the medoid copy still gets wrong."""
    if char == "ブ":   # ル's first stroke follows it on its only cards
        bitmap = bitmap[:, :20].copy()
        bitmap[:, 19:] = 0
        bitmap[17:, 15:] = 0
    elif char == "利":   # always followed by a touching 、
        bitmap = bitmap[:, :22].copy()
    elif char == "…":   # the cards draw …… as six even dots; one dot, three times, 19 px like theirs
        dot = bitmap[13:18, 3:7]
        bitmap = np.zeros((ROWS, 19))
        for x in (2, 8, 14):
            bitmap[13:18, x:x + 4] = np.maximum(bitmap[13:18, x:x + 4], dot)
    return bitmap


def trace(bitmap: np.ndarray, pen) -> None:
    """Upscale, smooth and trace one bitmap into `pen` (font units, baseline at y = 0)."""
    height, width = bitmap.shape
    image = Image.fromarray((np.pad(bitmap, 2) * 255).astype("uint8"))
    image = image.resize(((width + 4) * UPSCALE, (height + 4) * UPSCALE), Image.BICUBIC)
    ink = ndimage.gaussian_filter(np.asarray(image) / 255.0, UPSCALE * 0.35) > 0.42
    # potracer traces the False pixels; the image's y grows downwards.
    scale = PX / UPSCALE
    out = TransformPen(Cu2QuPen(pen, max_err=1.0, reverse_direction=True),
                       (scale, 0, 0, -scale, -2 * PX, (ROWS + 2) * PX - DESCENT))
    point = lambda p: (p.x, p.y)
    for curve in potrace.Bitmap(~ink).trace(turdsize=UPSCALE * UPSCALE // 4, alphamax=1.1,
                                            opticurve=True, opttolerance=0.25):
        out.moveTo(point(curve.start_point))
        for segment in curve.segments:
            if segment.is_corner:
                out.lineTo(point(segment.c))
                out.lineTo(point(segment.end_point))
            else:
                out.curveTo(point(segment.c1), point(segment.c2), point(segment.end_point))
        out.closePath()


def build_japanese(rom: bytes, output: Path) -> int:
    glyphs = glyph_bitmaps(rom)
    order, outlines, advances, cmap = [".notdef", "space", "uni3000"], {}, {}, {0x20: "space", 0x3000: "uni3000"}
    for name in order:
        outlines[name] = TTGlyphPen(None).glyph()
    advances.update({".notdef": 500, "space": round(8 * PX), "uni3000": UPM})
    for char in sorted(glyphs):
        bitmap = repaired(char, without_neighbours(glyphs[char]))
        name = f"uni{ord(char):04X}"
        pen = TTGlyphPen(None)
        trace(bitmap, pen)
        order.append(name)
        outlines[name], advances[name], cmap[ord(char)] = pen.glyph(), round(bitmap.shape[1] * PX), name
    # The transcription mixes full-width and ASCII digits and marks; each form serves both.
    pairs = {chr(0xFF10 + i): str(i) for i in range(10)} | {"！": "!", "？": "?", "Ｆ": "F", "Ｏ": "O", "Ｚ": "Z"}
    for wide, narrow in pairs.items():
        for a, b in ((wide, narrow), (narrow, wide)):
            if ord(a) not in cmap and ord(b) in cmap:
                cmap[ord(a)] = cmap[ord(b)]
    builder = FontBuilder(UPM, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(outlines)
    glyf = builder.font["glyf"]
    metrics = {}
    for name in order:
        glyf[name].recalcBounds(glyf)
        metrics[name] = (advances[name], glyf[name].xMin if glyf[name].numberOfContours else 0)
    builder.setupHorizontalMetrics(metrics)
    ascent, descent = round(UPM - DESCENT), round(DESCENT)
    builder.setupHorizontalHeader(ascent=ascent, descent=-descent)
    builder.setupNameTable({"familyName": "SRW64 Chapter Title", "styleName": "Regular",
                            "copyright": "Glyphs traced from the Super Robot Taisen 64 chapter title cards"})
    builder.setupOS2(sTypoAscender=ascent, sTypoDescender=-descent, usWinAscent=ascent, usWinDescent=descent)
    builder.setupPost()
    builder.save(output)
    return len(glyphs)


def chapter_titles(locale: str) -> str:
    document = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
    return "".join(row["target"].replace("<END>", "") for row in document["entries"]
                   if re.fullmatch(r"base:t00_00(2[89]\d|3\d\d|4[01]\d|42[0-3])", row["key"]))


def subset_font(source: Path, output: Path, text: str, rename: str | None = None) -> int:
    options = subset.Options()
    options.name_IDs = ["*"]
    options.layout_features = ["*"]
    font = subset.load_font(str(source), options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    if rename:
        # Every name a menu or a CSS family could show (family, full, PostScript, typographic
        # family and subfamily); the copyright, licence and designer records stay as they are.
        name = font["name"]
        for record in list(name.names):
            if record.nameID in (3, 4, 6, 16, 17, 18, 21, 22):
                name.removeNames(nameID=record.nameID)
        name.removeNames(nameID=1)
        name.setName(rename, 1, 3, 1, 0x409)
        name.setName(rename + " Regular", 4, 3, 1, 0x409)
        name.setName(rename.replace(" ", "") + "-Regular", 6, 3, 1, 0x409)
        name.setName(rename.replace(" ", "") + "-Regular; subset of Bakudai 1.49", 3, 3, 1, 0x409)
        name.setName("Regular", 2, 3, 1, 0x409)
    subset.save_font(font, str(output), options)
    return len(set(text))


def build_chinese(source: Path, output: Path) -> int:
    level1 = "".join(c for c in map(chr, range(0x4E00, 0x9FA6))
                     if len(c.encode("gb2312", "ignore")) == 2 and 0xB0A1 <= int.from_bytes(c.encode("gb2312"), "big") <= 0xD7F9)
    text = level1 + chapter_titles("zh-Hans") + "".join(map(chr, range(0x20, 0x7F))) + "第话終终！？，、。…（）「」『』·—：；“”‘’《》"
    return subset_font(source, output, text)


def build_traditional(source: Path, output: Path) -> int:
    # Big5 level 1, the frequently used hanzi: A440-C67E.
    level1 = "".join(c for c in map(chr, range(0x4E00, 0x9FA6))
                     if len(c.encode("big5", "ignore")) == 2 and 0xA440 <= int.from_bytes(c.encode("big5"), "big") <= 0xC67E)
    text = level1 + chapter_titles("zh-Hant") + "".join(map(chr, range(0x20, 0x7F))) + "第話終！？，、。…（）「」『』·—：；"
    return subset_font(source, output, text, rename="SRW64 Brush TC")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--mashanzheng", type=Path, help="MaShanZheng-Regular.ttf from github.com/googlefonts/mashanzheng")
    parser.add_argument("--bakudai", type=Path, help="Bakudai-Regular.ttf, tag 1.49 of github.com/max32002/bakudaifont (tw/)")
    parser.add_argument("--skip-japanese", action="store_true", help="leave SRW64ChapterTitle.ttf as it is")
    args = parser.parse_args()
    if not args.skip_japanese:
        count = build_japanese(args.rom.read_bytes(), FONTS / "SRW64ChapterTitle.ttf")
        print(f"SRW64ChapterTitle.ttf: {count} characters")
    if args.mashanzheng:
        print(f"MaShanZheng-Regular.ttf: {build_chinese(args.mashanzheng, FONTS / 'MaShanZheng-Regular.ttf')} characters")
    if args.bakudai:
        print(f"SRW64BrushTC.ttf: {build_traditional(args.bakudai, FONTS / 'SRW64BrushTC.ttf')} characters")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
