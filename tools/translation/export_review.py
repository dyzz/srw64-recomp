#!/usr/bin/env python3
"""Translations for the story reader: assets/original-data/translations/<locale>.json.

Combines the locale catalog (term-table, hand-written and merged mt entries) with the
machine-translation runs given by --tag (later tags win, as in run_mt.py collect), so
drafts can be read in context before they are merged. Lines whose draft failed a check
appear with the failure instead of a translation. Only story text (chapter titles,
dialogue, choices) is written; the reader has no other consumer.

    PYTHONPATH=src .venv/bin/python -B tools/translation/export_review.py --locale zh-Hans --tag zh-v1 --tag zh-v1-review
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_mt import RUNS, effective, final_target, load_records  # noqa: E402
from write_dialogue import MARKER  # noqa: E402
from srw64_native.catalog import source_catalog  # noqa: E402
from srw64_native.dialogue_text import compile_entry, parse  # noqa: E402
from srw64_native.original_story import display_text  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets/original-data/translations"
ORIGIN = {"terms": "词条", "mt": "机翻", None: "手写"}


def story_key(key: str) -> bool:
    if not key.startswith("base:t00_"):
        return False
    text_id = int(key[9:])
    return text_id >= 17347 or 281 <= text_id <= 423


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--locale", required=True)
    parser.add_argument("--tag", action="append", default=[], help="run tags; later ones override")
    args = parser.parse_args()
    if not (ROOT / "assets/original-data/story/index.json").exists():
        raise SystemExit("assets/original-data is missing; run tools/content/extract_original.py first")
    catalog = json.loads((ROOT / f"content/locales/{args.locale}.json").read_text(encoding="utf-8"))
    entries: dict[str, list] = {}
    for row in catalog["entries"]:
        if story_key(row["key"]):
            entries[row["key"]] = [display_text(row["target"]), ORIGIN.get(row.get("origin"), "手写"), ""]
    failed: dict[str, list] = {}
    for tag in args.tag:
        for path in sorted((RUNS / tag).rglob("*.json")):
            doc = json.loads(path.read_text())
            if doc.get("schema") != "srw64.mt-batch.v1" or doc.get("stage") in ("review", "joins"):
                continue
            for item in doc["items"]:
                if item.get("errors") and story_key(item["key"]):
                    tr = item.get("tr")
                    shown = "▸".join(p for p in tr if isinstance(p, str)) if isinstance(tr, list) else ""
                    failed[item["key"]] = [shown, "未通过", "；".join(item["errors"])]
    # Hand-maintained dialogue files (no generated-file marker) are what the game shows for their keys.
    sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
    for path in sorted((ROOT / f"content/dialogue/{args.locale}").rglob("*.txt")):
        text = path.read_text(encoding="utf-8")
        if text.startswith(MARKER):
            continue
        for entry in parse(text, str(path))[0]:
            target = compile_entry(entry, sources.get(entry.key)) if story_key(entry.key) else None
            if target:
                entries[entry.key] = [display_text(target), "手写", ""]
    hand = {k for k, row in entries.items() if row[1] == "手写"}  # hand-written lines stay authoritative
    good = effective(args.tag)
    records = load_records()
    for key, item in good.items():
        if story_key(key) and key not in hand:
            stage = item.get("base_stage") if item.get("stage") == "joins" else item.get("stage")
            label = "审校" if stage == "review" else "机翻"
            note = item.get("reason") or item.get("flag") or "；".join(item.get("warnings", []))
            entries[key] = [display_text(final_target(records[key], item, args.locale)), label, note]
    for key, row in failed.items():
        if key not in good:
            entries.setdefault(key, row)
    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / f"{args.locale}.json"
    out.write_text(json.dumps({"schema": "srw64.review-translations.v1", "locale": args.locale, "runs": args.tag,
                               "entries": entries}, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    counts = {}
    for row in entries.values():
        counts[row[1]] = counts.get(row[1], 0) + 1
    print(json.dumps({"locale": args.locale, "entries": len(entries), "by_origin": counts,
                      "output": str(out.relative_to(ROOT)), "bytes": out.stat().st_size}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
