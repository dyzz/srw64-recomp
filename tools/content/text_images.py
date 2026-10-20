#!/usr/bin/env python3
"""Story text drawn as images: chapter title cards, opening pages and ending pages.

The host draws these natively in the reading language instead of the original
images (docs/native/native-title-and-story-images.md). This tool ties the
images to their words:

- Chapter title cards: the map overlay's scene -> card table and the card
  triplets (resident 0x84B20) come from the ROM; the card transcription
  (assets/transcriptions/chapter-titles.ja.json, read from the images) is
  matched to the chapter title records 281 + scene. `header` writes the card
  -> record table the host uses (src/host/story_cards.hpp).
- Ending pages: the ending overlay's page table; `render` writes the pages as
  PNG for transcription and review. Their words and translations live in
  content/dialogue/<locale>/ending.txt as @intro:<resource> entries.

    .venv/bin/python tools/content/text_images.py render      # -> build/content/text-images/
    .venv/bin/python tools/content/text_images.py header --write
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import unicodedata

from PIL import Image

from srw64_rom.resources import ResourceTable
from srw64_native.battle_graphics import parse_scene, read_triplets, render_scene
from srw64_native.catalog import source_catalog
from srw64_native.original_images import decode_indexed

ROOT = Path(__file__).resolve().parents[2]
CARD_TRANSCRIPTION = ROOT / "assets/transcriptions/chapter-titles.ja.json"
HEADER = ROOT / "src/host/story_cards.hpp"
OUTPUT = ROOT / "build/content/text-images"
SCENE_CARDS = 0x102110      # load_000AB160 D_802195B0: per stage scene (u8, card); 801C72C8 reads the card
SCENES = 143                # chapter title records 281..423, one per stage scene
FIRST_TITLE_RECORD = 281
ENDING_TABLE = 0x1160F0     # load_001156A0 D_801C3050: (scene, atlas, frames) until 0xFFFF
ENDING_PALETTE = 5564
ENDING_PAGES = range(5570, 5577)   # epilogue pages 5570-5575 and the closing 5576; the rest is the staff roll
NUMBER_SCENES = (5106, 5204)        # "第 N 話" for N = scene - 5106 + 1 (801C72C8 clamps N to 99)


def normalized(text: str) -> str:
    text = unicodedata.normalize("NFKC", text.replace("<END>", ""))
    for mark in ("「", "」", " ", "　"):
        text = text.replace(mark, "")
    for part, short in (("前編", "(前)"), ("中編", "(中)"), ("後編", "(後)")):
        text = text.replace(part, short)
    return text.replace("二つ", "2つ")


def scene_cards(rom: bytes) -> list[int]:
    return [rom[SCENE_CARDS + 2 * scene + 1] for scene in range(SCENES)]


def ending_pages(rom: bytes) -> list[tuple[int, int, int]]:
    rows, offset = [], ENDING_TABLE
    while True:
        scene, atlas, frames = struct.unpack_from(">3H", rom, offset)
        if scene == 0xFFFF:
            return rows
        rows.append((scene, atlas, frames))
        offset += 6


def cards(rom: bytes, transcription: dict, sources: dict) -> list[dict]:
    """Card -> the chapter title record its words match; every card any scene shows must match one."""
    triplets = read_triplets(rom, "chapter_titles")
    shown = scene_cards(rom)
    read = {row["card"]: row for row in transcription["cards"]}
    if sorted(read) != list(range(len(triplets))):
        raise ValueError("the transcription must cover every card")
    result = []
    for card, (scene, atlas, palette) in enumerate(triplets):
        row = read[card]
        if (row["scene"], row["atlas"], row["palette"]) != (scene, atlas, palette):
            raise ValueError(f"card {card}: transcription is of another triplet")
        words = normalized("".join(row["lines"]))
        users = [s for s, c in enumerate(shown) if c == card]
        records = [FIRST_TITLE_RECORD + s for s in users]
        exact = [r for r in records if normalized(sources[f"base:t00_{r:05d}"]) == words]
        entry = {"card": card, "scene": scene, "atlas": atlas, "stage_scenes": users}
        if NUMBER_SCENES[0] <= scene <= NUMBER_SCENES[1]:
            entry.update(text=0, note="placeholder: the card shows only 第１話")
        elif exact:
            entry.update(text=exact[0])
        elif records:
            # Two cards spell a word differently from the record (時/とき, へ/に); same title.
            close = [r for r in records if sum(a != b for a, b in zip(normalized(sources[f"base:t00_{r:05d}"]), words)) <= 2]
            if not close:
                raise ValueError(f"card {card} matches no chapter title record of its scenes")
            entry.update(text=close[0], note="the card and the record differ in one kana")
        else:
            entry.update(text=0, note="no scene shows this card")
        result.append(entry)
    # Cards 127 and 128 are the same triplet (合流, shown by two scenes); the host finds the first.
    for e in result:
        twin = next(o for o in result if o["scene"] == e["scene"])
        if e["text"] and normalized(sources[f"base:t00_{e['text']:05d}"]) != normalized(sources[f"base:t00_{twin['text']:05d}"]):
            raise ValueError(f"cards {twin['card']} and {e['card']} share a title scene but not a title")
    return result


SUFFIXES = {"前編": "前", "中編": "中", "後編": "後"}
CELL = 8   # a line's glyph cell starts 8 rows into its 32-row tile row and is 24 rows high


def lettering(image) -> "Image.Image":
    """The white letters of a card (its dark rim and the background dropped), as a mask."""
    rgba = image.convert("RGBA")
    light = rgba.convert("L").point(lambda v: 255 if v > 70 else 0)
    return Image.composite(light, Image.new("L", rgba.size), rgba.getchannel("A"))


def line_tops(scene) -> list[int]:
    """Each line's tile row (scene units from the top); the digits of 第N話 sit a row lower."""
    top = scene.bounds()[1]
    tops = []
    for row in sorted({part.y - top for part in scene.frames[0]}):
        if not tops or row - tops[-1] > 4:
            tops.append(row)
    return tops


def layout(rom: bytes, entry: dict, lines: list[str], sources: dict) -> dict | None:
    """Where the card puts each line, for drawing its record in the cards' own lettering.

    The record holds the words (戦え!熱き血のファイターたち, OZ分裂 （前）); the card adds 「」,
    breaks after the first `split` characters and gives the part its own line (前編).
    """
    if not entry["text"] or "note" in entry:
        return None
    table = ResourceTable(rom)
    data = lambda i: table.extract(i)[0]
    scene = parse_scene(data(entry["scene"]))
    image = render_scene(scene, decode_indexed(data(entry["atlas"]), data(5105)))[0][0]
    lines = [line.replace("<END>", "") for line in lines]
    suffix = lines[-1] if lines[-1] in SUFFIXES else ""
    words = lines[:-1] if suffix else lines
    record = unicodedata.normalize("NFKC", sources[f"base:t00_{entry['text']:05d}"].replace("<END>", ""))
    title = record.split("(")[0].rstrip()   # NFKC made （前） (前)
    # NFKC would turn the card's ideographic space into a plain one: keep it apart.
    flat = lambda text: unicodedata.normalize("NFKC", text.replace("\u3000", "\0")).replace("「", "").replace("」", "").replace("二つ", "2つ")
    card = flat("".join(words))
    spaces = [i for i, c in enumerate(card) if c in " \0"]
    if (card.replace(" ", "").replace("\0", "") != title.replace(" ", "") or len(words) > 2 or len(spaces) > 1
            or (suffix and not record.endswith(f"({SUFFIXES[suffix]})"))):
        return None
    mask = lettering(image)
    xs, ys, gap_x = [], [], 0
    split = len(flat(words[0]).replace(" ", "").replace("\0", "")) if len(words) > 1 else 0
    for n, top in enumerate(line_tops(scene)[:len(lines)]):
        band = mask.crop((0, top + CELL, mask.width, top + CELL + 24))
        xs.append(band.getbbox()[0])
        ys.append(top + CELL)
        if spaces and n == (1 if split and spaces[0] >= split else 0):
            # The words after the card's space start after the widest empty run in the line.
            inked = [x for x in range(band.width) if band.crop((x, 0, x + 1, band.height)).getbbox()]
            gap_x = max(zip(inked, inked[1:]), key=lambda pair: pair[1] - pair[0])[1]
    return {"width": image.width, "height": image.height, "split": split,
            "gap": spaces[0] if spaces else 0, "gap_x": gap_x,
            "lines": len(lines), "x": xs + [0] * (3 - len(xs)), "y": ys + [0] * (3 - len(ys))}


def number_layout(rom: bytes) -> dict:
    """第N話: 第 and 話 never move and the digits are centred between them (scene 5115, 第10話)."""
    table = ResourceTable(rom)
    data = lambda i: table.extract(i)[0]
    scene = parse_scene(data(NUMBER_SCENES[0] + 9))
    image = render_scene(scene, decode_indexed(data(4989), data(5105)))[0][0]
    mask = lettering(image)
    columns = [x for x in range(mask.width) if mask.crop((x, 0, x + 1, mask.height)).getbbox()]
    runs = [[columns[0]]]
    for x in columns[1:]:
        (runs[-1].append(x) if x == runs[-1][-1] + 1 else runs.append([x]))
    left, top = scene.bounds()[:2]
    row = lambda x: min(p.y - top for p in scene.frames[0] if p.x - left <= x < p.x - left + p.w)
    return {"width": image.width, "height": image.height, "dai": runs[0][0], "wa": runs[-1][0],
            "digits": runs[1][0] + runs[-2][-1] + 1, "y": row(runs[0][0]) + CELL, "digit_y": row(runs[1][0]) + CELL}


def header(entries: list[dict], layouts: list[dict | None], number: dict) -> str:
    rows = ",\n".join(f"    {{{e['scene']}, {e['text']}}}" for e in entries)
    layout_rows = ",\n".join(
        "    Layout{" + (", ".join(str(v) for v in (l["width"], l["height"], l["split"], l["gap"], l["gap_x"], l["lines"], *l["x"], *l["y"]))
                         if l else "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0") + "}" for l in layouts)
    return f"""// Generated by tools/content/text_images.py from the pinned ROM and the card
// transcription (assets/transcriptions/chapter-titles.ja.json). Do not edit.
#pragma once
#include <cstdint>
namespace srw64::story_cards {{
// Chapter title cards (resident table 0x84B20): the title scene each card draws and
// the chapter title record its words match; 0 for the two 第１話 placeholder cards.
struct Card {{ uint16_t scene, text; }};
inline constexpr Card cards[{len(entries)}] = {{
{rows}
}};
// Scenes 5106..5204 draw 第 N 話 with N = scene - 5106 + 1.
inline constexpr uint16_t number_first = {NUMBER_SCENES[0]}, number_last = {NUMBER_SCENES[1]};
// Every chapter atlas shares palette 5105.
inline constexpr uint16_t palette = 5105;
// Japanese, in the cards' own lettering: each card's size and where it puts each line
// (left of the first letter, top of the 24-row glyph cell; card units). The record's
// words, spaces left out, are: the first `split` of them the first line (0: one line),
// the card's one space after the first `gap` (gap_x: where the words after it start; 0:
// no space), and a record ending in （前）（中）（後） adds a 前編 中編 後編 line.
// lines == 0: draw the record as other languages do.
struct Layout {{ uint16_t width, height; uint8_t split, gap; uint16_t gap_x; uint8_t lines, x0, x1, x2, y0, y1, y2; }};
inline constexpr Layout layouts[{len(layouts)}] = {{
{layout_rows}
}};
// 第N話 in the cards' lettering: 第 and 話 at fixed x, the digits centred on digits_x2 / 2.
inline constexpr uint16_t number_width = {number["width"]}, number_height = {number["height"]};
inline constexpr uint8_t number_dai = {number["dai"]}, number_wa = {number["wa"]}, number_digits_x2 = {number["digits"]};
inline constexpr uint8_t number_y = {number["y"]}, number_digit_y = {number["digit_y"]};
}}
"""


def render(rom: bytes, out: Path) -> None:
    table = ResourceTable(rom)
    data = lambda i: table.extract(i)[0]
    (out / "ending").mkdir(parents=True, exist_ok=True)
    rows = []
    for scene, atlas, frames in ending_pages(rom):
        if atlas not in ENDING_PAGES:
            continue
        image, _ = render_scene(parse_scene(data(scene)), decode_indexed(data(atlas), data(ENDING_PALETTE)))
        path = out / "ending" / f"{atlas}.png"
        image[0].save(path)
        rows.append({"resource": atlas, "scene": scene, "frames": frames, "image": str(path.relative_to(ROOT)),
                     "image_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    (out / "ending" / "pages.json").write_text(json.dumps(rows, indent=1) + "\n")
    print(json.dumps(rows, indent=1))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("render").add_argument("--output", type=Path, default=OUTPUT)
    command = commands.add_parser("header")
    command.add_argument("--write", action="store_true", help="rewrite src/host/story_cards.hpp")
    command.add_argument("--report", type=Path, help="also write the card table as JSON")
    args = parser.parse_args()
    rom = (ROOT / "rom.z64").read_bytes()
    if args.command == "render":
        render(rom, args.output)
        return 0
    sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
    transcription = json.loads(CARD_TRANSCRIPTION.read_text())
    entries = cards(rom, transcription, sources)
    lines = {row["card"]: row["lines"] for row in transcription["cards"]}
    layouts = [layout(rom, entry, lines[entry["card"]], sources) for entry in entries]
    if args.report:
        args.report.write_text(json.dumps(entries, ensure_ascii=False, indent=1) + "\n")
    text = header(entries, layouts, number_layout(rom))
    if args.write:
        HEADER.write_text(text)
    elif HEADER.read_text() != text:
        print(f"{HEADER.relative_to(ROOT)} is out of date; rerun with --write", file=sys.stderr)
        return 1
    print({"cards": len(entries), "titled": sum(1 for e in entries if e["text"]), "laid out": sum(map(bool, layouts)),
           "notes": {e["card"]: e["note"] for e in entries if "note" in e}})
    return 0


if __name__ == "__main__":
    sys.exit(main())
