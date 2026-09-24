#!/usr/bin/env python3
"""Proper-noun candidates in dialogue that the data-range term table does not name.

Names of units, pilots and weapons are decided in content/locales/terms/ (one section per
data range). Dialogue also mentions organizations, places, eras and ships that never
appear as a data record (ロームフェラ財団, カラバ, ミケーネ). This lists katakana words and
katakana/kanji compounds with an organization or place suffix, counted over story,
battle and intro text, minus anything a data-range name record already spells. The
result is a worklist for deciding translations before the full run, not a glossary.

    PYTHONPATH=src .venv/bin/python -B tools/translation/term_candidates.py
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from references import project_terms, srwz_terms  # noqa: E402
from srw64_native.catalog import TOKEN  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXPORT = ROOT / "assets/text-export"
OUT = ROOT / "assets/translation-runs/term-candidates.json"
DIALOGUE = {"story.dialogue", "story.choice", "story.intro", "battle.quote", "battle.special"}
KATAKANA = re.compile(r"[ァ-ヺー・＝]{3,}")
SUFFIXED = re.compile(r"[ァ-ヺー・＝一-龯]{1,8}?(?:帝国|財団|機構|連邦|公国|基地|戦線|研究所|艦隊|要塞|王国|一族|軍団|解放軍|部隊|"
                      r"星人|拳流|流派|号|城|島|市|流|艦(?!長)|隊(?!長|員)|軍(?!人|曹|師|団|医))")


def noise(word: str) -> bool:
    """Laughter and drawn-out shouts (フフフ, ハハハハッ, パーンチッ) are not names."""
    kana = set(word) - set("ッーィ")
    return word.endswith("ッ") or (len(kana) <= 2 and len(word) >= 3 and KATAKANA.fullmatch(word) is not None)


def main() -> int:
    rows = [json.loads(line) for line in (EXPORT / "records.jsonl").open(encoding="utf-8")]
    named = {TOKEN.sub("", r["source"]).replace("<END>", "") for r in rows if r["category"].startswith("names.")}
    named |= {t.ja for t in project_terms()}
    counts: Counter[str] = Counter()
    records: dict[str, set] = defaultdict(set)
    for row in rows:
        if row["category"] not in DIALOGUE:
            continue
        text = TOKEN.sub("", row["source"])
        for word in set(KATAKANA.findall(text)) | set(SUFFIXED.findall(text)):
            word = word.strip("・＝")
            if len(word) < 3 or word in named or noise(word):
                continue
            counts[word] += text.count(word)
            records[word].add(row["key"])
    hints = {t.ja: t.zh for t in srwz_terms()}
    candidates = [{"ja": w, "occurrences": n, "records": len(records[w]),
                   **({"srwz_hint": hints[w]} if w in hints else {}),
                   "examples": sorted(records[w])[:3]}
                  for w, n in counts.most_common() if len(records[w]) >= 2]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"schema": "srw64.term-candidates.v1", "rule": __doc__.split("\n\n")[1],
                               "candidates": candidates}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(json.dumps({"candidates": len(candidates), "with_srwz_hint": sum("srwz_hint" in c for c in candidates),
                      "top": [f"{c['ja']}×{c['occurrences']}" for c in candidates[:40]]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
