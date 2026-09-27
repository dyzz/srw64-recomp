#!/usr/bin/env python3
"""Hand-triage of a review/style run: list its accepted changes compactly, then keep only the
ones a reader approved as a new run that write_dialogue.py can layer on.

    PYTHONPATH=src .venv/bin/python -B tools/translation/curate.py list --tag en-v2-style [--scene 12] [--offset 0 --limit 200]
        → one block per changed line: key, speaker, ja, before, after, reason
    PYTHONPATH=src .venv/bin/python -B tools/translation/curate.py keep --tag en-v2-style --out en-v2-style-ok \\
        --decisions decisions.json
        decisions.json: {"base:t00_17380": "ok", "base:t00_17471": "no", "base:t00_17385": ["page 1", "page 2"]}
        "ok" keeps the model's revision, "no" drops it, a list of pages replaces it with the reader's own text
        (checked like any other item); keys absent from the file are dropped, so every kept line is a decision.

The output run holds one batch file per source batch, stage "style", model "<model>+curated".
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_mt import RUNS, Context, check_item, load_records, localized, renames, save  # noqa: E402


def changed_items(tag: str):
    for path in sorted((RUNS / tag).glob("*.json")):
        doc = json.loads(path.read_text())
        if doc.get("schema") != "srw64.mt-batch.v1":
            continue
        for item in doc["items"]:
            if not item.get("errors"):
                yield doc, item


def after_renames(pages: list[str], record: dict, locale: str) -> str:
    return "▸".join(renames(locale).apply(page, record["source"]) for page in pages)


def redundant(doc: dict, item: dict, record: dict) -> bool:
    """The revision equals the draft once the rename table is applied to both: a no-op at write time."""
    if not isinstance(item.get("draft"), list):
        return False
    locale = doc["locale"]
    return after_renames(localized(item["draft"], locale), record, locale) == after_renames(item["tr"], record, locale)


def junk(item: dict) -> bool:
    """Model output that leaked JSON punctuation into a page ("…”], ")."""
    return any(("]," in page) or ('["' in page) or ('"]' in page) for page in item["tr"])


def speaker_of(record: dict) -> str:
    ctx = record.get("context") or {}
    return ctx.get("speaker") or ((ctx.get("occurrences") or [{}])[0].get("speaker")) or ""


def cmd_list(args) -> None:
    records = load_records()
    decided = json.loads(Path(args.undecided).read_text(encoding="utf-8")) if args.undecided else {}
    rows, skipped = [], 0
    for doc, item in changed_items(args.tag):
        if args.scene is not None and not doc["id"].startswith(f"scene-{args.scene:04d}-"):
            continue
        if item["key"] in decided:
            continue
        if junk(item) or (item["key"] in records and redundant(doc, item, records[item["key"]])):
            skipped += 1
            continue
        rows.append((doc, item))
    rows = rows[args.offset: args.offset + args.limit] if args.limit else rows[args.offset:]
    for doc, item in rows:
        record = records[item["key"]]
        before = "▸".join(localized(item["draft"], doc["locale"])) if isinstance(item["draft"], list) else item["draft"]
        print(f"{item['key']} [{doc['id']}] {speaker_of(record)}")
        print("  J", record["display"].replace("\n", " "))
        print("  <", before)
        print("  >", "▸".join(item["tr"]))
        print("  ?", item.get("reason", ""))
    print(f"-- {len(rows)} shown, {skipped} redundant after renames skipped", file=sys.stderr)


def cmd_keep(args) -> None:
    records = load_records()
    decisions = json.loads(Path(args.decisions).read_text(encoding="utf-8"))
    kept = replaced = dropped = 0
    ctx: dict[str, Context] = {}
    per_batch: dict[str, tuple[dict, list]] = {}
    for doc, item in changed_items(args.tag):
        verdict = decisions.get(item["key"])
        if verdict == "ok" and (junk(item) or redundant(doc, item, records[item["key"]])):
            verdict = None
        if verdict in (None, "no"):
            dropped += 1
            continue
        locale = doc["locale"]
        if isinstance(verdict, list):
            context = ctx.setdefault(locale, Context(locale))
            checked = check_item(records[item["key"]], verdict, context.terms, locale)
            if checked.get("errors"):
                print(f"{item['key']}: reader text rejected: {checked['errors']}", file=sys.stderr)
                dropped += 1
                continue
            item = {**item, **checked, "reason": item.get("reason", "") + "（人工改写）"}
            replaced += 1
        else:
            kept += 1
        per_batch.setdefault(doc["id"], (doc, []))[1].append(item)
    out = RUNS / args.out
    for batch_id, (doc, items) in per_batch.items():
        result = {**{k: v for k, v in doc.items() if k != "items"}, "model": doc["model"] + "+curated",
                  "curated_from": args.tag, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "items": items}
        save(out / f"{batch_id}.json", result)
    print(f"kept {kept}, replaced {replaced}, dropped {dropped} → {out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list")
    p.add_argument("--tag", required=True)
    p.add_argument("--scene", type=int)
    p.add_argument("--offset", type=int, default=0)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--undecided", help="decisions.json; list only changes without a decision")
    p = sub.add_parser("keep")
    p.add_argument("--tag", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--decisions", required=True)
    args = parser.parse_args()
    {"list": cmd_list, "keep": cmd_keep}[args.command](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
