#!/usr/bin/env python3
"""Draft Chinese and English names for proper nouns that only dialogue uses.

Reads assets/translation-runs/term-candidates.json (term_candidates.py), asks the model to
separate proper nouns from ordinary words and to name each proper noun in both languages,
and writes content/translation/story-terms.json. Candidates are sorted so related forms
sit together (ムゲ帝国, ムゲゾルバドス, ムゲゾルバドス帝国) and batches run in order,
each seeing the decisions already made for overlapping strings. Existing entries are
kept unless --redo is given, so hand edits survive a rerun.

    PYTHONPATH=src .venv/bin/python -B tools/translation/draft_terms.py --env-file ~/.../.env
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dashscope  # noqa: E402
from references import STORY_TERMS, project_terms, srwz_terms  # noqa: E402
from srw64_native.translation import relevant_terms  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CANDIDATES = ROOT / "assets/translation-runs/term-candidates.json"
EXPORT = ROOT / "assets/text-export/records.jsonl"
MODEL = "deepseek-v4-pro-0813"

SYSTEM = """你是《超级机器人大战》系列的中英文本地化专家，熟悉各参战作品的中国大陆通行译名和官方英文名。
下面是 1999 年 N64 游戏《超级机器人大战64》台词里出现的片假名词和带“帝国／财团／基地／部队”等后缀的词，每个附有出现次数和例句。
参战作品：高达系列（0079、第08MS小队、0083、Z、ZZ、逆袭的夏亚、F91、G高达、高达W）、魔神Z、大魔神、UFO机器人古连泰沙、盖塔机器人／G、孔巴特拉V、赞博特3、泰坦3、豪将军、圣战士丹拜因、超兽机神断空我、苍之流星SPT雷兹纳、六神合体战神、巨型机器人，以及原创剧情（外宇宙的ムゲゾルバドス帝国占领地球圈，A.C.195 年的反帝国运动）。

对每个词判断 kind：
- proper：专有名词（组织、国家、地名、殖民卫星、舰船、人名／外号、作品内的特定机体、兵器、技术、种族、事件）；
- common：普通外来语或普通名词（パイロット、エネルギー、データ），以及口语自称／称呼（アタシ、アンタ、オイラ）、拟声词；
- fragment：只是更长专名的一部分、截断的词或例句里看不出独立意义的片段。
proper 需给出：category（organization/place/person/ship/unit/weapon/technology/species/event/other）、zh（中国大陆通行译名；有官方或社区通行译名时必须采用，没有就音译，人名用间隔号·）、en（官方英文名；没有就用通行罗马字或英文圈惯例）。
common 和 fragment 的 zh、en 留空字符串。
decided 是已经定好的相关译名（本项目词条表或前面批次），与之有关的词必须保持一致（例如已定 ムゲゾルバドス帝国，则 ムゲ帝国 的译法要与之呼应）。
note 用中文简短说明依据或疑点（出自哪部作品、是否不确定），没有就留空。
只输出 JSON：{"items":[{"ja":"…","kind":"proper","category":"…","zh":"…","en":"…","note":"…"}]}，每个输入词一条。"""


def examples(records: dict, keys: list[str], limit: int = 3) -> list[str]:
    out = []
    for key in keys[:limit]:
        text = records[key]["display"].replace("<BR>", "").replace("<STOP>", " ").replace("\n", "")
        out.append(text[:90])
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--env-file", type=Path)
    parser.add_argument("--model", default=MODEL)
    parser.add_argument("--batch", type=int, default=40)
    parser.add_argument("--min-records", type=int, default=2)
    parser.add_argument("--redo", action="store_true", help="re-draft entries that already exist")
    args = parser.parse_args()
    credentials = dashscope.load_env(args.env_file)
    records = {r["key"]: r for r in map(json.loads, EXPORT.open(encoding="utf-8"))}
    candidates = [c for c in json.loads(CANDIDATES.read_text())["candidates"] if c["records"] >= args.min_records]
    existing = json.loads(STORY_TERMS.read_text()) if STORY_TERMS.exists() else {"terms": {}}
    terms = existing["terms"]
    todo = sorted((c for c in candidates if args.redo or c["ja"] not in terms), key=lambda c: c["ja"])
    zh_terms, en_terms = project_terms("zh-Hans"), project_terms("en")
    en_by_ja = {t.ja: t.zh for t in en_terms}
    hints = {t.ja: t.zh for t in srwz_terms()}
    usage = existing.get("usage", {"prompt_tokens": 0, "completion_tokens": 0, "requests": 0})
    print(f"{len(todo)} candidates to draft in batches of {args.batch}, model {args.model}", flush=True)
    for start in range(0, len(todo), args.batch):
        chunk = todo[start:start + args.batch]
        words = [c["ja"] for c in chunk]
        texts = [e for c in chunk for e in examples(records, c["examples"])]
        decided = [{"ja": t.ja, "zh": t.zh, "en": en_by_ja.get(t.ja, ""), "from": t.source}
                   for t in relevant_terms(texts + words, zh_terms)]
        decided += [{"ja": ja, "zh": e.get("zh", ""), "en": e.get("en", ""), "from": "前面批次"}
                    for ja, e in terms.items() if e.get("kind") == "proper"
                    and any(ja in w or w in ja for w in words) and ja not in words]
        payload = {"decided": decided, "words": [
            {"ja": c["ja"], "occurrences": c["occurrences"], "examples": examples(records, c["examples"]),
             **({"srwz_zh_hint": hints[c["ja"]]} if c["ja"] in hints else {})} for c in chunk]}
        call = dashscope.chat([{"role": "system", "content": SYSTEM},
                               {"role": "user", "content": json.dumps(payload, ensure_ascii=False)}],
                              credentials=credentials, model=args.model)
        doc = dashscope.parse_json(call.text)
        usage["prompt_tokens"] += call.prompt_tokens
        usage["completion_tokens"] += call.completion_tokens
        usage["requests"] += 1
        by_ja = {c["ja"]: c for c in chunk}
        got = 0
        for item in doc.get("items", []):
            if not isinstance(item, dict) or item.get("ja") not in by_ja:
                continue
            c = by_ja[item["ja"]]
            kind = item.get("kind") if item.get("kind") in ("proper", "common", "fragment") else "fragment"
            terms[item["ja"]] = {"kind": kind, "category": item.get("category", "") if kind == "proper" else "",
                                 "zh": (item.get("zh") or "") if kind == "proper" else "",
                                 "en": (item.get("en") or "") if kind == "proper" else "",
                                 "note": item.get("note") or "", "status": "draft",
                                 "occurrences": c["occurrences"], "records": c["records"], "model": args.model}
            got += 1
        missing = [w for w in words if w not in terms]
        print(f"batch {start // args.batch + 1}: {got}/{len(chunk)} drafted, {call.prompt_tokens}+"
              f"{call.completion_tokens} tokens, {call.elapsed:.0f}s" + (f", missing {missing}" if missing else ""),
              flush=True)
        STORY_TERMS.parent.mkdir(parents=True, exist_ok=True)
        STORY_TERMS.write_text(json.dumps({
            "schema": "srw64.story-terms.v1",
            "note": "只在台词中出现的专有名词（组织、地名、舰船等），数据区的名字不在这里而在 content/locales/terms/。"
                    "由 tools/translation/draft_terms.py 起草，kind=proper 的条目进入机翻提示词的“必用”（3 字以上）或“参考”；"
                    "status 可改为 approved 或 rejected，改动后重跑受影响的场景。",
            "updated": time.strftime("%Y-%m-%d"), "usage": usage,
            "terms": dict(sorted(terms.items()))}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    counts = {k: sum(e["kind"] == k for e in terms.values()) for k in ("proper", "common", "fragment")}
    print(json.dumps({"terms": len(terms), **counts, "usage": usage}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
