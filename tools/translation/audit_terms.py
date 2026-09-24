#!/usr/bin/env python3
"""Cross-check dialogue terms (story-terms.json) against the project term table.

A dialogue term whose Japanese occurs inside a term-table string (ゾルバドス in
ムゲ・ゾルバドス城, アル＝イー＝クイス in a victory condition) should be rendered the
same way there, in both languages. The audit lists every such pair whose rendering
disagrees, plus term-table sections that render one Japanese root differently. It
changes nothing; decisions go into story-terms.json or the term table by hand.

    PYTHONPATH=src .venv/bin/python -B tools/translation/audit_terms.py
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STORY_TERMS = ROOT / "content/translation/story-terms.json"
OUT = ROOT / "assets/translation-runs/term-audit.json"


def main() -> int:
    story = json.loads(STORY_TERMS.read_text())["terms"]
    tables = {loc: json.loads((ROOT / f"content/locales/terms/{loc}.json").read_text())["sections"]
              for loc in ("zh-Hans", "en")}
    field = {"zh-Hans": "zh", "en": "en"}
    conflicts = []
    for ja, entry in story.items():
        if entry.get("kind") != "proper" or len(ja) < 2:
            continue
        for loc, sections in tables.items():
            mine = entry.get(field[loc]) or ""
            for section, table in sections.items():
                for source, target in table.items():
                    if ja in source and mine and mine.casefold() not in target.casefold():
                        conflicts.append({"ja": ja, "locale": loc, "story_term": mine, "section": section,
                                          "term_source": source, "term_target": target})
    # One katakana root rendered two ways inside the term table itself (ムゲ → 穆格 / 姆格).
    split_roots = []
    for ja, entry in story.items():
        if entry.get("kind") != "proper":
            continue
        renderings = defaultdict(set)
        for section, table in tables["zh-Hans"].items():
            for source, target in table.items():
                if source.startswith(ja) and len(ja) >= 2:
                    renderings[target[:len(entry.get("zh") or "") or 2]].add(f"{section}:{source}→{target}")
        if len(renderings) > 1:
            split_roots.append({"ja": ja, "renderings": {k: sorted(v)[:4] for k, v in renderings.items()}})
    OUT.write_text(json.dumps({"conflicts": conflicts, "split_roots": split_roots}, ensure_ascii=False, indent=1) + "\n",
                   encoding="utf-8")
    by_term = defaultdict(list)
    for c in conflicts:
        by_term[(c["ja"], c["locale"])].append(c)
    print(json.dumps({"conflicting_terms": len(by_term), "pairs": len(conflicts), "split_roots": len(split_roots),
                      "output": str(OUT.relative_to(ROOT))}, ensure_ascii=False))
    for (ja, loc), rows in sorted(by_term.items()):
        r = rows[0]
        print(f"{loc:7s} {ja} = {r['story_term']}  ≠  {r['section']}:{r['term_source']} = {r['term_target']}"
              + (f"  (+{len(rows) - 1})" if len(rows) > 1 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
