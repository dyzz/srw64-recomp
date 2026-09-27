#!/usr/bin/env python3
"""Speech habits of every speaker, counted from the Japanese lines, as evidence for the voice cards.

Counts first-person and second-person pronouns, sentence-final particles and copula styles,
laughs and honorific verb forms per speaker over story dialogue and battle quotes, plus a few
sample lines. Output: assets/translation-runs/voice/speaker-features.json and a markdown
table for review. Docs: docs/design/dialogue-polish-plan.md.

    PYTHONPATH=src .venv/bin/python -B tools/translation/speaker_features.py [--min-lines 20]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STORY = ROOT / "assets/original-data/story"
EXPORT = ROOT / "assets/text-export/records.jsonl"
OUT = ROOT / "assets/translation-runs/voice"

FIRST = ["わたくし", "あたし", "アタシ", "あたい", "わたし", "私", "オレ", "俺", "おれ", "ボク", "僕", "ぼく", "ワシ", "わし",
         "オイラ", "自分", "我", "余", "朕", "拙者", "小生", "あっし"]
SECOND = ["貴様", "きさま", "てめぇ", "てめえ", "テメェ", "お前", "おまえ", "オマエ", "あんた", "アンタ", "あなた", "アナタ",
          "貴方", "君", "きみ", "そなた", "貴殿", "貴公", "おぬし", "お主"]
ENDINGS = ["だぜ", "ぜ", "だぞ", "ぞ", "だわ", "わよ", "わね", "ですわ", "ますわ", "のよ", "かしら", "ですな", "ますな", "ですぞ",
           "ますぞ", "でございます", "ございます", "のじゃ", "じゃ", "なのだ", "のだ", "である", "だな", "だね", "かな",
           "ッス", "っす", "でやんす", "ざます", "のう", "わい", "かい", "だい", "ですね", "ますね", "でしょう", "ましょう",
           "なさい", "たまえ", "くれ", "だ", "です", "ます"]
LAUGHS = ["フフフ", "フフ", "ハハハ", "ハハ", "ホホホ", "オホホ", "ヒヒヒ", "へへ", "ヘヘ", "はは", "ふふ", "クックック", "ククク",
          "ワハハ", "ガハハ", "ウハハ"]
HONORIFIC = ["いたします", "なさいます", "いただき", "ございます", "申し上げ", "おります", "まいります", "くださいませ", "でしょうか"]
CASUAL = ["じゃねえ", "じゃねぇ", "ねえよ", "ねぇ", "やがる", "やがれ", "だろ", "だろう", "ったく", "ちくしょう", "ちっ"]


def base(label: str | None) -> str:
    return re.sub(r"（.*?）$", "", label or "").strip()


def visible(text: str) -> str:
    return re.sub(r"<[^>]+>", "", text)


def load_story() -> dict[str, list[str]]:
    lines: dict[str, list[str]] = defaultdict(list)
    for path in sorted(STORY.glob("0*.json")):
        scene = json.loads(path.read_text(encoding="utf-8"))
        for event in scene["events"]:
            for line in event["lines"]:
                if line.get("kind") == "dialogue" and line.get("text"):
                    lines[base((line.get("speaker") or {}).get("label"))].append(visible(line["text"]))
    return lines


def load_battle() -> dict[str, list[str]]:
    lines: dict[str, list[str]] = defaultdict(list)
    with EXPORT.open(encoding="utf-8") as file:
        for record in map(json.loads, file):
            if not str(record.get("category", "")).startswith("battle"):
                continue
            speaker = (record.get("context") or {}).get("speaker") or ""
            if speaker:
                lines[base(speaker)].append(visible(record["source"]))
    return lines


def count(patterns: list[str], texts: list[str], ending: bool = False) -> dict[str, int]:
    out: Counter = Counter()
    for text in texts:
        for sentence in re.split(r"[。!?！？…」』)]\s*", text) if ending else [text]:
            for p in patterns:
                if ending:
                    if sentence.rstrip("、。 ").endswith(p):
                        out[p] += 1
                        break
                else:
                    out[p] += sentence.count(p)
    return {k: v for k, v in out.most_common() if v}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--min-lines", type=int, default=20)
    args = parser.parse_args()
    story, battle = load_story(), load_battle()
    speakers = sorted(set(story) | set(battle), key=lambda s: -(len(story.get(s, [])) + len(battle.get(s, []))))
    features = []
    for speaker in speakers:
        if not speaker or speaker in ("???", "？？？"):
            continue
        texts = story.get(speaker, []) + battle.get(speaker, [])
        if len(texts) < args.min_lines:
            continue
        joined = "".join(texts)
        first = count(FIRST, texts)
        # 私 swallows わたし/わたくし only when written in kana separately; keep both as counted
        item = {
            "speaker": speaker, "story_lines": len(story.get(speaker, [])), "battle_lines": len(battle.get(speaker, [])),
            "chars": len(joined),
            "first_person": first, "second_person": count(SECOND, texts),
            "endings": count(ENDINGS, texts, ending=True), "laughs": count(LAUGHS, texts),
            "honorific": count(HONORIFIC, texts), "casual": count(CASUAL, texts),
            "samples": sorted(texts, key=len)[len(texts) // 3: len(texts) // 3 + 4],
        }
        features.append(item)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "speaker-features.json").write_text(json.dumps(features, ensure_ascii=False, indent=1), encoding="utf-8")
    rows = ["| 说话人 | 剧情/战斗行数 | 一人称 | 二人称 | 句尾 | 敬语/粗口 |", "| --- | --- | --- | --- | --- | --- |"]
    for f in features:
        fmt = lambda d, n=4: "、".join(f"{k}{v}" for k, v in list(d.items())[:n])  # noqa: E731
        rows.append(f"| {f['speaker']} | {f['story_lines']}/{f['battle_lines']} | {fmt(f['first_person'])} | "
                    f"{fmt(f['second_person'])} | {fmt(f['endings'], 6)} | {fmt(f['honorific'], 2)} / {fmt(f['casual'], 2)} |")
    (OUT / "speaker-features.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"{len(features)} speakers with >= {args.min_lines} lines → {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
