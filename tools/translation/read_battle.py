#!/usr/bin/env python3
"""Close reading of battle quotes by voice, with the decoded trigger of every line.

    PYTHONPATH=src .venv/bin/python -B tools/translation/read_battle.py list
        every voice: number, actors, generic and special line counts (to split the work).
    PYTHONPATH=src .venv/bin/python -B tools/translation/read_battle.py dump --voice 1 [--voice 2 ...] [--tag v3-read]
    PYTHONPATH=src .venv/bin/python -B tools/translation/read_battle.py dump --all
        per voice: the generic block situation by situation, then the special lines grouped by
        condition (multi-line exchanges kept together), each with the Japanese source and the
        current English and Chinese pages; lines no table reaches come last under "未引用".
    Fixes are applied with read_scene.py apply --batch NAME --fixes f.json (same fix format).

Trigger data comes from assets/text-export/records.jsonl (context.triggers, written by
tools/content/export_text.py from src/srw64_native/battle_quotes.py).
"""
from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from read_scene import EN_RUNS, ZH_RUNS  # noqa: E402
from run_mt import effective, load_records, localized, source_pages  # noqa: E402
from srw64_native.battle_quotes import SITUATIONS  # noqa: E402


def battle_records(records: dict) -> list[dict]:
    return sorted((r for r in records.values() if r["category"] in ("battle.quote", "battle.special")),
                  key=lambda r: r["id"])


def by_voice(rows: list[dict]) -> dict[int | None, list[dict]]:
    voices: dict[int | None, list[dict]] = defaultdict(list)
    for r in rows:
        voices[r["context"].get("voice")].append(r)
    return voices


def voice_label(rows: list[dict], records: dict) -> str:
    """The actors that own the voice (D_800CA9C4), by their short names; speakers of the
    exchange lines inside the list (partners, co-pilots) are not part of it."""
    names = []
    for actor in rows[0]["context"].get("voice_actors", []):
        name = (records.get(f"base:t00_{4382 + actor:05d}") or {}).get("display") or str(actor)
        if name not in names:
            names.append(name)
    if not names:
        names = [r["context"].get("speaker") or "-" for r in rows[:1]]
    return "／".join(names)


def cmd_list(args) -> None:
    records = load_records()
    rows = battle_records(records)
    for voice, lines in sorted(by_voice(rows).items(), key=lambda kv: (kv[0] is None, kv[0] or 0)):
        generic = sum(1 for r in lines if any(t["kind"] == "generic" for t in r["context"].get("triggers", [])))
        special = len(lines) - generic
        owner = lines[0]["context"].get("voice_actors", [])
        print(f"voice {voice if voice is not None else '-':>3}  actors {owner}  {voice_label(lines, records):<20} "
              f"generic {generic:3d}  special {special:3d}  ids {lines[0]['id']}–{lines[-1]['id']}")


def print_line(r: dict, en: dict, zh: dict, trigger: dict | None) -> None:
    key = r["key"]
    ja = "|".join(source_pages(r, "en"))
    e = "▸".join(localized(en[key]["tr"], "en")) if key in en else "(none)"
    z = "▸".join(localized(zh[key]["tr"], "zh-Hans")) if key in zh else "(none)"
    desc = trigger["desc"] if trigger else "未被任何表引用（原版不显示）"
    print(f"{r['id']} {r['context'].get('speaker') or '-'} | {desc}\n J {ja}\n E {e}\n Z {z}")


def cmd_dump(args) -> None:
    records = load_records()
    rows = battle_records(records)
    en = effective(args.en_runs.split(",") + ([f"en-{args.tag}"] if args.tag else []))
    zh = effective(args.zh_runs.split(",") + ([f"zh-{args.tag}"] if args.tag else []))
    voices = by_voice(rows)
    wanted = sorted(v for v in voices if v is not None) if args.all else args.voice
    for voice in wanted:
        lines = voices.get(voice, [])
        if not lines:
            print(f"### voice {voice}: no lines", file=sys.stderr)
            continue
        print(f"### voice {voice} {voice_label(lines, records)} actors {lines[0]['context'].get('voice_actors')} ({len(lines)} lines)")
        generic = defaultdict(list)
        special: list[tuple[tuple, dict, dict]] = []
        for r in lines:
            for t in r["context"].get("triggers", []):
                if t["kind"] == "generic":
                    generic[t["situation"]].append((r, t))
                else:
                    first = t.get("sequence", [r["id"]])[0]
                    special.append(((t.get("code", 0), first, t.get("position", 0)), r, t))
        for situation in sorted(generic):
            print(f"## {SITUATIONS[situation][0]}")
            for r, t in generic[situation]:
                print_line(r, en, zh, t)
        current = None
        for (code, first, _), r, t in sorted(special, key=lambda x: (x[0][1], x[0][2])):
            head = t["desc"].split("（对话第")[0]
            if head != current:  # consecutive single lines under one condition share a heading
                print(f"## {head}")
                current = head
            print_line(r, en, zh, t)
    if args.all:
        orphans = [r for r in rows if not r["context"].get("triggers")]
        if orphans:
            print("### 未引用")
            for r in orphans:
                print_line(r, en, zh, None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    p = sub.add_parser("dump")
    p.add_argument("--voice", type=int, action="append", default=[])
    p.add_argument("--all", action="store_true")
    p.add_argument("--zh-runs", default=ZH_RUNS)
    p.add_argument("--en-runs", default=EN_RUNS)
    p.add_argument("--tag", default="v3-read")
    args = parser.parse_args()
    if args.command == "list":
        cmd_list(args)
    else:
        if not args.voice and not args.all:
            raise SystemExit("--voice N or --all required")
        cmd_dump(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
