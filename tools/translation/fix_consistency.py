#!/usr/bin/env python3
"""Mechanical consistency fixes decided by the user (docs/design/dialogue-polish-plan.md):

- The Specials' own ranks stay distinct: 特尉 / 特佐 / 特士 → zh 特尉 / 特佐 / 特士,
  en "Special Lieutenant" / "Special Colonel" / "Special Officer". Only lines whose Japanese
  carries exactly that rank and no other rank word are rewritten; the rest are listed for a reader.
- Lady Une addresses Treize as 阁下: in lines whose Japanese has トレーズ様／閣下, zh 特列斯大人 →
  特列斯阁下 (every speaker); en, for Lady's lines, vocative "Lord Treize" → "Your Excellency",
  other mentions → "His Excellency".

Writes a fixes run per locale that write_dialogue.py layers last:
    PYTHONPATH=src .venv/bin/python -B tools/translation/fix_consistency.py --locale en --draft ... --out en-v1-consist
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_mt import RUNS, Context, check_item, effective, load_records, localized, save  # noqa: E402

OTHER_RANKS = re.compile(r"大尉|中尉|少尉|大佐|中佐|少佐|准将|少将|中将|大将|艦長|隊長|司令|将軍|元帥|曹長|軍曹|伍長")
EN_RANK = re.compile(r"\b(?:Special )?(?:First |Petty |Lieutenant )?(?:Lieutenant Colonel|Lieutenant|Ensign|Captain|Colonel|Officer|Major)\b")
EN_FOR = {"特尉": "Special Lieutenant", "特佐": "Special Colonel", "特士": "Special Officer"}
ZH_RANK = re.compile(r"特尉|特佐|特士|特校|上尉|中尉|少尉|上校|中校|少校")


def speaker_of(record: dict) -> str:
    ctx = record.get("context") or {}
    return ctx.get("speaker") or ((ctx.get("occurrences") or [{}])[0].get("speaker")) or ""


def fix_ranks(pages: list[str], source: str, locale: str) -> tuple[list[str], str] | None:
    present = [r for r in ("特尉", "特佐", "特士") if r in source]
    if len(present) != 1 or OTHER_RANKS.search(source):
        return None
    rank = present[0]
    if locale == "en":
        new = [EN_RANK.sub(EN_FOR[rank], p).replace("Specials Special ", "Special ") for p in pages]
    else:
        new = [ZH_RANK.sub(rank, p) for p in pages]
    if new == pages:
        return None
    return new, f"机械统一：{rank} 保留为特殊部队军衔（用户 2026-09-26 定）"


def fix_treize(pages: list[str], source: str, locale: str, speaker: str) -> tuple[list[str], str] | None:
    if not re.search(r"トレーズ(?:様|さま|閣下)", source):
        return None
    if locale == "zh-Hans":
        new = [p.replace("特列斯大人", "特列斯阁下") for p in pages]
    else:
        if not speaker.startswith("レディ"):
            return None
        new = []
        for p in pages:
            p = re.sub(r"Lord Treize's", "His Excellency's", p)
            p = re.sub(r"(^|[“(\s])Lord Treize([,.!?…]|$)", lambda m: f"{m.group(1)}Your Excellency{m.group(2)}", p)
            p = p.replace("Lord Treize", "His Excellency")
            new.append(p)
    if new == pages:
        return None
    return new, "机械统一：蕾蒂称特列斯为阁下／Your Excellency（用户 2026-09-26 定）"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--locale", required=True)
    parser.add_argument("--draft", action="append", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    records = load_records()
    ctx = Context(args.locale)
    items, skipped = [], []
    for key, item in effective(args.draft).items():
        record = records.get(key)
        pages = item.get("tr")
        if not record or not isinstance(pages, list):
            continue
        pages = localized(pages, args.locale)
        source = record["source"]
        changed, reasons = list(pages), []
        for fixer in (lambda p: fix_ranks(p, source, args.locale),
                      lambda p: fix_treize(p, source, args.locale, speaker_of(record))):
            result = fixer(changed)
            if result:
                changed, reason = result
                reasons.append(reason)
        if not reasons:
            if any(r in source for r in ("特尉", "特佐", "特士")) and OTHER_RANKS.search(source):
                skipped.append(key)
            continue
        checked = check_item(record, changed, ctx.terms, args.locale)
        if checked.get("errors"):
            print(f"{key}: rejected {checked['errors']}", file=sys.stderr)
            continue
        items.append({"id": key, **checked, "draft": item["tr"], "base_stage": item.get("stage"), "reason": "；".join(reasons)})
    doc = {"schema": "srw64.mt-batch.v1", "status": "done", "id": "consistency", "kind": "story", "locale": args.locale,
           "stage": "fixes", "model": "manual", "prompt_version": "manual",
           "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "usage": [], "items": items}
    save(RUNS / args.out / "consistency.json", doc)
    print(f"{len(items)} lines fixed → {RUNS / args.out}; {len(skipped)} rank lines mixed with other ranks left for a reader: "
          + " ".join(k.split('_')[1] for k in skipped[:20]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
