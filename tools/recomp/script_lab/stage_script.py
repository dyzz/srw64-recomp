#!/usr/bin/env python3
"""Print one scene's events as readable script, and index how each opcode is used.

The catalog already decodes every event; this joins the events of one scene back
into reading order and, with --usage, collects every original call site of an
opcode across all 1,812 events. Reading the real call sites is what tells you
which operand values a command is actually given and what it sits next to —
which is where a probe stage's parameters should come from, rather than a guess.

Nothing here executes or modifies anything; it is a view over the extracted
records.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
RECORDS = ROOT / "assets/original-data/records"


def events(scene: int | None = None) -> list[dict]:
    rows = [json.loads(l) for l in (RECORDS / "stage_events.jsonl").read_text().splitlines() if l.strip()]
    if scene is None:
        return rows
    return sorted((r for r in rows if scene in (r.get("script") or {}).get("scenes", [])),
                  key=lambda r: r["rom_offset"])


def render(instruction: dict, width: int) -> str:
    kind = instruction["kind"]
    code = f"{instruction['opcode']:04X}"
    args = instruction.get("operands") or []
    head = f"  {instruction['offset']:4}  {code}" + (" " + ",".join(str(a) for a in args) if args else "")
    head = head.ljust(width)
    if kind == "dialogue":
        who = instruction.get("speaker_label") or f"#{instruction.get('speaker_id')}"
        text = (instruction.get("text") or "").replace("<BR>", " ").replace("<STOP>", " / ").replace("<END>", "")
        return f"{head}  {who}「{text.strip()[:70]}」"
    return f"{head}  {instruction['name']}"


def show(scene: int) -> int:
    rows = events(scene)
    if not rows:
        print(f"scene {scene} has no events", file=sys.stderr)
        return 1
    print(f"# 场景 {scene} · {len(rows)} 个事件\n")
    for row in rows:
        script = row["script"]
        header = script["header_words"]
        print(f"## {row['key'].split(':')[-1]} · 类型 {header[0]} · {script['trigger']['name']}")
        print(f"   事件头参数 {header[1:]} · {row['summary']}")
        width = max((len(f"  {i['offset']:4}  {i['opcode']:04X}" +
                         (" " + ",".join(str(a) for a in (i.get('operands') or [])) if i.get('operands') else ""))
                    for i in script["instructions"]), default=20)
        for instruction in script["instructions"]:
            print(render(instruction, width))
        print()
    return 0


def usage(opcode: int, limit: int) -> int:
    """Every original call site of one opcode, with the instructions around it."""
    found = 0
    for row in events():
        script = row.get("script") or {}
        items = script.get("instructions") or []
        for index, instruction in enumerate(items):
            if instruction["opcode"] != opcode:
                continue
            found += 1
            if found > limit:
                continue
            scenes = script.get("scenes", [])
            print(f"— {row['key'].split(':')[-1]} · 场景 {scenes} · 类型 {script['header_words'][0]} · {script['trigger']['name']}")
            for near in items[max(0, index - 3):index + 4]:
                mark = " <<<" if near is instruction else ""
                args = ",".join(str(a) for a in (near.get("operands") or []))
                label = near.get("speaker_label") or near["name"]
                if near["kind"] == "dialogue":
                    label = f"{label}「{(near.get('text') or '')[:34]}…」"
                print(f"     {near['opcode']:04X}{(' ' + args) if args else '':<12} {label}{mark}")
            print()
    print(f"{opcode:04X}: {found} 处调用" + (f"（显示前 {limit} 处）" if found > limit else ""))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    s = sub.add_parser("show", help="print one scene's events in reading order")
    s.add_argument("scene", type=int)
    u = sub.add_parser("usage", help="every original call site of one opcode")
    u.add_argument("opcode")
    u.add_argument("--limit", type=int, default=12)
    args = parser.parse_args()
    if args.command == "show":
        return show(args.scene)
    return usage(int(args.opcode, 16), args.limit)


if __name__ == "__main__":
    sys.exit(main())
