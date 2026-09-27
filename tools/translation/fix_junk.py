#!/usr/bin/env python3
"""Strip JSON punctuation that leaked into translated pages ("…”], ") from the effective drafts.

Some review-stage answers were repaired from malformed JSON and kept a stray "], " inside the
last page; 45 such lines shipped. This writes a run with the cleaned pages so write_dialogue.py
can layer it last:

    PYTHONPATH=src .venv/bin/python -B tools/translation/fix_junk.py --locale zh-Hans --draft zh-v1 ... --out zh-v1-junk
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_mt import RUNS, Context, check_item, effective, load_records, save  # noqa: E402

JUNK = re.compile(r'\s*\]\s*,?\s*"?\s*$')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--locale", required=True)
    parser.add_argument("--draft", action="append", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    records = load_records()
    ctx = Context(args.locale)
    items = []
    for key, item in effective(args.draft).items():
        pages = item.get("tr")
        if not isinstance(pages, list) or not any("]" in p for p in pages):
            continue
        cleaned = [JUNK.sub("", p) if "]" in p else p for p in pages]
        if cleaned == pages:
            continue
        checked = check_item(records[key], cleaned, ctx.terms, args.locale)
        if checked.get("errors"):
            print(f"{key}: still rejected {checked['errors']}: {pages}", file=sys.stderr)
            continue
        items.append({"id": key, **checked, "draft": pages, "base_stage": item.get("stage"),
                      "reason": "机械修正：去掉泄漏进译文的 JSON 标点“],”"})
    doc = {"schema": "srw64.mt-batch.v1", "status": "done", "id": "junk", "kind": "story", "locale": args.locale,
           "stage": "fixes", "model": "manual", "prompt_version": "manual",
           "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "usage": [], "items": items}
    save(RUNS / args.out / "junk.json", doc)
    print(f"{len(items)} lines cleaned → {RUNS / args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
