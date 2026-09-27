#!/usr/bin/env python3
"""Mechanical voice consistency over the shipped dialogue files (docs/design/dialogue-polish-plan.md §2).

Reports, per speaker:
- zh-Hans: which first-person pronouns the character uses (我 / 老夫 / 本人 / 在下 / 老身 / 俺 …) — one is expected,
  two is a finding unless the card allows it;
- both locales: how a speaker addresses each named character (万丈大人 / 万丈先生 / 万丈; Master Banjo / Mr. Banjo /
  Lord Banjo / Banjo) — more than one form for the same pair is a finding.

    PYTHONPATH=src .venv/bin/python -B tools/translation/check_voice.py [--locale en|zh-Hans|all] [--min 2]
Output: a markdown report to stdout; also written to assets/translation-runs/voice/consistency-<locale>.md.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from srw64_native.dialogue_text import parse  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXPORT = ROOT / "assets/text-export/records.jsonl"
OUT = ROOT / "assets/translation-runs/voice"

ZH_FIRST = ["老夫", "老身", "本人", "在下", "本大爷", "本小姐", "人家", "老娘", "老子", "朕", "俺", "咱", "我"]
ZH_TITLES = "大人|先生|小姐|少爷|舰长|队长|博士|教授|阁下|殿下|中尉|少尉|大尉|少校|中校|上校|司令|将军|老师|师父|队长|同志|君"
EN_TITLES = r"(?:Master|Mr\.|Mrs\.|Ms\.|Miss|Lord|Lady|Captain|Lieutenant|Colonel|Major|Commander|Professor|Prof\.|Dr\.|Doctor|General|Admiral|Sir)"


def speaker_of(record: dict) -> str:
    ctx = record.get("context") or {}
    label = ctx.get("speaker") or ((ctx.get("occurrences") or [{}])[0].get("speaker")) or ""
    return re.sub(r"（.*?）$", "", label).strip()


def targets(locale: str) -> dict[str, str]:
    out = {}
    for path in sorted((ROOT / "content/dialogue" / locale).rglob("*.txt")):
        for entry in parse(path.read_text(encoding="utf-8"), str(path))[0]:
            pages = [" ".join(p.target) for p in entry.pages if p.target]
            if pages:
                out[entry.key] = " ".join(pages)
    return out


def names(locale: str) -> dict[str, str]:
    """Japanese short name -> translated short name, for characters that get addressed."""
    sections = json.loads((ROOT / f"content/locales/terms/{locale}.json").read_text(encoding="utf-8"))["sections"]
    table = {ja: tr for ja, tr in sections.get("pilots", {}).items() if len(tr) >= 2 and not ja.endswith("兵")}
    return {ja: tr for ja, tr in table.items() if tr not in ("NT Soldier", "NT士兵")}


def address_forms(text: str, name: str, locale: str) -> list[str]:
    if locale == "en":
        pattern = rf"(?:{EN_TITLES}\s+)?{re.escape(name)}(?![a-z])"
    else:
        pattern = rf"{re.escape(name)}(?:{ZH_TITLES})?"
    return [m.group(0) for m in re.finditer(pattern, text)]


def run(locale: str, minimum: int) -> list[str]:
    records = {}
    with EXPORT.open(encoding="utf-8") as file:
        for record in map(json.loads, file):
            if record.get("id", -1) >= 5799 and record["key"].startswith("base:"):
                records[record["key"]] = record
    text = targets(locale)
    table = names(locale)
    lines_by_speaker: dict[str, list[str]] = defaultdict(list)
    for key, tr in text.items():
        record = records.get(key)
        if record:
            lines_by_speaker[speaker_of(record)].append(tr)
    report = [f"# 口吻一致性：{locale}", ""]
    if locale == "zh-Hans":
        report += ["## 一人称不唯一的说话人", "", "| 说话人 | 用法 |", "| --- | --- |"]
        for speaker, lines in sorted(lines_by_speaker.items(), key=lambda kv: -len(kv[1])):
            joined = "\n".join(lines)
            counts = Counter()
            for pron in ZH_FIRST:
                n = len(re.findall(pron, joined))
                if pron == "我":
                    n -= sum(counts.values()) * 0  # 我 is counted plainly; compound forms above were removed first
                if n:
                    counts[pron] = n
                joined = joined.replace(pron, "＿" * len(pron))
            forms = {p: n for p, n in counts.items() if n >= minimum}
            if len(forms) > 1 and speaker:
                report.append(f"| {speaker} | " + "、".join(f"{p}{n}" for p, n in forms.items()) + " |")
        report.append("")
    report += ["## 同一人对同一人称呼不一致", "", "| 说话人 | 对象 | 用法 |", "| --- | --- | --- |"]
    for speaker, lines in sorted(lines_by_speaker.items(), key=lambda kv: -len(kv[1])):
        joined = "\n".join(lines)
        for ja, tr in table.items():
            if ja == speaker or tr not in joined:
                continue
            forms = Counter(address_forms(joined, tr, locale))
            forms = {f: n for f, n in forms.items() if n >= minimum}
            if len(forms) > 1 and speaker:
                report.append(f"| {speaker} | {tr} | " + "、".join(f"{f}×{n}" for f, n in sorted(forms.items(), key=lambda kv: -kv[1])) + " |")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--locale", choices=("en", "zh-Hans", "all"), default="all")
    parser.add_argument("--min", type=int, default=2, help="ignore forms used fewer times than this")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for locale in (("en", "zh-Hans") if args.locale == "all" else (args.locale,)):
        report = run(locale, args.min)
        (OUT / f"consistency-{locale}.md").write_text("\n".join(report) + "\n", encoding="utf-8")
        print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
