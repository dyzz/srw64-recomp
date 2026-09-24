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


def header(entries: list[dict]) -> str:
    rows = ",\n".join(f"    {{{e['scene']}, {e['text']}}}" for e in entries)
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
    entries = cards(rom, json.loads(CARD_TRANSCRIPTION.read_text()), sources)
    if args.report:
        args.report.write_text(json.dumps(entries, ensure_ascii=False, indent=1) + "\n")
    text = header(entries)
    if args.write:
        HEADER.write_text(text)
    elif HEADER.read_text() != text:
        print(f"{HEADER.relative_to(ROOT)} is out of date; rerun with --write", file=sys.stderr)
        return 1
    print({"cards": len(entries), "titled": sum(1 for e in entries if e["text"]),
           "notes": {e["card"]: e["note"] for e in entries if "note" in e}})
    return 0


if __name__ == "__main__":
    sys.exit(main())
