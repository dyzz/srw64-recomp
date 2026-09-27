#!/usr/bin/env python3
"""Style pass over the story and battle lines: character voice, forms of address, tone.

Plan: docs/design/dialogue-polish-plan.md. Every line goes to the model with its Japanese
pages, the current translation, the speaker's voice card (content/translation/voices.json)
and the neighbouring speakers, and comes back unchanged or revised for voice only. Meaning,
names, placeholders and page count stay; check_item rejects anything else. Output is a
srw64.mt-batch.v1 run (stage "style") that write_dialogue.py layers after the draft runs.

    PYTHONPATH=src .venv/bin/python -B tools/translation/style_review.py --locale en --tag en-v2-style \\
        --draft en-v1 --draft en-v1-review --draft en-v1-fixes --scenes 0,1,2,3
    PYTHONPATH=src .venv/bin/python -B tools/translation/style_review.py --report --tag en-v2-style
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dashscope  # noqa: E402
from run_mt import (LANG, RUNS, WORLD, Context, check_item, done, effective, load_records, load_scenes,  # noqa: E402
                    localized, request, save, source_pages, speaker_cards, usage_of)

ROOT = Path(__file__).resolve().parents[2]
PROMPT_VERSION = "srw64-style-v1"

SYSTEM = ("你是资深游戏本地化审校，负责 N64 游戏《超级机器人大战64》{name}台词的**人物口吻**。\n" + WORLD + "\n"
          "每行给出：speaker 说话人、to 对话对象（按前后行推定，可能为空）、ja 日文原文（按翻页拆成数组）、tr 现有译文（页数相同）。"
          "speakers 是本批说话人的卡片：role 身份、persona 性格、ja_speech 该角色在日文里的一人称／二人称／句尾习惯（从全部台词统计得来）、"
          "voice 对译文口吻的要求、address 对特定人物的称呼。ctx 为 true 的行只作上下文，不要返回。\n"
          "只检查、只改这些：\n"
          "1. 口吻与 voice 不符：贵族、管家、军人、老人、少年、反派各有腔调；敬语层级（对长官、长辈、主人）该有的要有，平辈之间不该有的要去掉；"
          "语气的强弱（怒吼、哀求、拖长音、结巴、笑声）要与 ja 一致。\n"
          "2. 称呼与 address 不符，或同一人对同一人的称呼前后不一致。\n"
          "3. 整条通读后发现某一页被单独理解、割裂了句意（页末无标点的地方是同一句话的中间）。\n"
          "不改的：意思、信息的增删、专名与译名、{{HeroName}} 这类占位符（个数顺序不变）、页数与分页位置、引号与标点风格。"
          "改动尽量小，能改一个词就不改整句；已经符合的行不要为了改而改，也不要把没改的行返回。\n"
          "称呼的分寸：原文有呼语（お嬢様、万丈様、中尉 等）的地方按 address 和敬语译出；原文没有呼语的句子不要自行添加 sir／my lady／大人 之类的称呼。"
          "原文带 様／さん／殿 的称呼要保留相应的敬称（Lady Simone、西蒙娜小姐），address 只规定写法，不是删除敬称的依据。\n"
          "输出 JSON 对象 {{\"items\":[{{\"id\":\"…\",\"revised\":[\"…\"],\"reason\":\"…\"}}]}}：只返回需要修改的行，revised 是改好后的完整译文数组"
          "（页数与 ja 相同，必须与 tr 不同，reason 里说的修改要真正体现在 revised 里），reason 用中文一句话。没有需要改的就返回 {{\"items\":[]}}。\n")


def batches_for(records: dict, scenes: dict, wanted: set[int] | None, size: int) -> list[dict]:
    out = []
    for index, scene in sorted(scenes.items()):
        if wanted is not None and index not in wanted:
            continue
        seen: set[str] = set()
        lines = [l for l in scene["lines"] if l.get("kind") == "dialogue" and l.get("key")
                 and not (l["key"] in seen or seen.add(l["key"]))]
        for part in range(0, len(lines), size):
            chunk = lines[part: part + size]
            before = lines[part - 2: part] if part else []
            after = lines[part + size: part + size + 2]
            out.append({"id": f"scene-{index:04d}-{part // size:02d}", "scene": index, "title": scene["title"],
                        "lines": before + chunk, "ctx_before": len(before), "after": after})
    return out


def battle_batches(records: dict, size: int) -> list[dict]:
    rows = sorted((r for r in records.values() if str(r["category"]).startswith("battle")), key=lambda r: r["id"])
    out = []
    for part in range(0, len(rows), size):
        chunk = rows[part: part + size]
        out.append({"id": f"battle-{chunk[0]['id']:05d}", "kind": "battle", "rows": chunk})
    return out


def trivial(before: list[str], after: list[str]) -> bool:
    """Only quotes, whitespace or trailing punctuation differ: not a voice change."""
    strip = lambda pages: "".join(pages).translate(str.maketrans("", "", "“”‘’\"' 　.。…!！?？,，")).strip()  # noqa: E731
    return strip(before) == strip(after)


VOCATIVES = {"en": ["my lady", "sir", "ma'am", "master ", "lord ", "lady ", "your excellency", "if i may", "milady"],
             "zh-Hans": ["大小姐", "大人", "小姐", "先生", "阁下", "长官", "少爷", "您"]}
JA_ADDRESS = ("様", "さま", "お嬢", "殿", "閣下", "さん", "君", "少尉", "中尉", "大尉", "少佐", "中佐", "大佐", "艦長", "隊長",
              "博士", "教授", "先生", "師匠", "兄", "姉", "坊っちゃま", "お坊ちゃま", "旦那", "ご主人")


def added_vocative(ja_pages: list[str], before: list[str], after: list[str], locale: str) -> str | None:
    """A form of address that the revision adds where the Japanese has none: not the model's call."""
    ja = "".join(ja_pages).replace("様子", "").replace("様々", "").replace("お兄", "兄")
    if any(mark in ja for mark in JA_ADDRESS):
        return None
    b, a = "".join(before).lower(), "".join(after).lower()
    for word in VOCATIVES[locale]:
        if a.count(word) > b.count(word):
            return word
    return None


def label_of(line: dict) -> str | None:
    return line.get("speaker") or None


def story_payload(ctx: Context, batch: dict, records: dict, drafts: dict) -> tuple[dict, dict[str, str]]:
    lines, ids = [], {}
    all_lines = batch["lines"] + batch["after"]
    for n, line in enumerate(batch["lines"]):
        key = line["key"]
        item = {"id": f"L{n}", "speaker": label_of(line) or "（旁白／系统）"}
        speakers_around = [label_of(l) for l in all_lines[max(0, n - 2): n + 3] if label_of(l) and label_of(l) != label_of(line)]
        if speakers_around:
            item["to"] = speakers_around[0]
        if n < batch["ctx_before"] or key not in drafts or key not in records:
            item["ctx"] = True
            item["ja"] = line["display"]
            lines.append(item)
            continue
        ids[item["id"]] = key
        item["ja"] = source_pages(records[key], ctx.locale)
        item["tr"] = localized(drafts[key]["tr"], ctx.locale)
        lines.append(item)
    labels = [label_of(l) for l in batch["lines"]]
    return {"scene": batch["title"], "speakers": speaker_cards(ctx, labels), "lines": lines}, ids


def battle_payload(ctx: Context, batch: dict, drafts: dict) -> tuple[dict, dict[str, str]]:
    lines, ids = [], {}
    for n, record in enumerate(batch["rows"]):
        key = record["key"]
        speaker = (record.get("context") or {}).get("speaker") or "？"
        if key not in drafts:
            continue
        ids[f"L{n}"] = key
        lines.append({"id": f"L{n}", "speaker": speaker, "ja": source_pages(record, ctx.locale),
                      "tr": localized(drafts[key]["tr"], ctx.locale)})
    labels = [(r.get("context") or {}).get("speaker") for r in batch["rows"]]
    return {"section": "战斗台词", "speakers": speaker_cards(ctx, labels), "lines": lines}, ids


def style_batch(ctx: Context, args, batch: dict, records: dict, drafts: dict) -> None:
    out = RUNS / args.tag / f"{batch['id']}.json"
    if done(out):
        return
    if batch.get("kind") == "battle":
        payload, ids = battle_payload(ctx, batch, drafts)
    else:
        payload, ids = story_payload(ctx, batch, records, drafts)
    if not ids:
        return
    system = SYSTEM.format(name=LANG[ctx.locale]["name"]) + LANG[ctx.locale]["style"]
    call, doc = request(args, system, payload)
    items = doc.get("items", [])
    if isinstance(items, dict):
        items = [{"id": k, **v} for k, v in items.items() if isinstance(v, dict)]
    results = []
    for item in items:
        key = ids.get(item.get("id"))
        if not key or not isinstance(item.get("revised"), list):
            continue
        checked = check_item(records[key], item["revised"], ctx.terms, ctx.locale)
        checked["reason"] = item.get("reason") or ""
        checked["draft"] = drafts[key]["tr"]
        if not checked.get("errors") and checked["target"] == drafts[key]["target"]:
            checked["errors"] = ["与初稿相同"]
        elif not checked.get("errors") and trivial(localized(drafts[key]["tr"], ctx.locale), item["revised"]):
            checked["errors"] = ["只改了引号、空格或句末标点"]
        elif not checked.get("errors"):
            word = added_vocative(source_pages(records[key], ctx.locale), localized(drafts[key]["tr"], ctx.locale),
                                  item["revised"], ctx.locale)
            if word:
                checked["errors"] = [f"添加了原文没有的呼语：{word.strip()}"]
        results.append({"id": item["id"], **checked})
    result = {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch["id"], "kind": batch.get("kind", "story"),
              "locale": ctx.locale, "stage": "style", "model": args.model, "prompt_version": PROMPT_VERSION,
              "draft_runs": args.draft, "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "usage": [usage_of(call)], "reviewed": len(ids), "items": results}
    save(out, result)
    changed = sum(not i.get("errors") for i in results)
    rejected = len(results) - changed
    print(f"{batch['id']}: {changed}/{len(ids)} changed, {rejected} rejected, "
          f"{call.prompt_tokens}+{call.completion_tokens} tokens, {call.elapsed:.0f}s", flush=True)


def cmd_run(args) -> None:
    args.credentials = dashscope.load_env(args.env_file)
    records, scenes = load_records(), load_scenes()
    ctx = Context(args.locale)
    drafts = effective(args.draft)
    wanted = {int(s) for s in args.scenes.split(",")} if args.scenes else None
    batches = []
    if args.kind in ("story", "all"):
        batches += batches_for(records, scenes, wanted, args.size)
    if args.kind in ("battle", "all"):
        batches += battle_batches(records, args.size * 2)
    print(f"{len(batches)} batches over {len(drafts)} drafts, {args.locale}, model {args.model}, tag {args.tag}", flush=True)

    def guarded(batch):
        try:
            style_batch(ctx, args, batch, records, drafts)
        except Exception as exc:
            print(f"{batch['id']}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(guarded, batches))


def cmd_report(args) -> None:
    records = load_records()
    run = RUNS / args.tag
    reviewed = changed = rejected = 0
    rows, reasons = [], []
    for path in sorted(run.glob("*.json")):
        doc = json.loads(path.read_text())
        if doc.get("schema") != "srw64.mt-batch.v1":
            continue
        locale = doc["locale"]
        reviewed += doc.get("reviewed", 0)
        for item in doc["items"]:
            if item.get("errors"):
                rejected += 1
                reasons.append(item["errors"][0])
                continue
            changed += 1
            key = item["key"]
            speaker = ((records[key].get("context") or {}).get("speaker")
                       or ((records[key].get("context") or {}).get("occurrences") or [{}])[0].get("speaker") or "")
            before = "▸".join(localized(item["draft"], locale)) if isinstance(item["draft"], list) else str(item["draft"])
            rows.append((key.split("_")[1], speaker, records[key]["display"].replace("\n", " "), before,
                         "▸".join(item["tr"]), item.get("reason", "")))
    from collections import Counter
    lines = [f"# 风格审校：{args.tag}", "",
             f"检查 {reviewed} 行，改 {changed} 行（{changed / max(1, reviewed):.1%}），检查未通过 {rejected} 行。", ""]
    if reasons:
        lines += ["未通过原因：" + "；".join(f"{r[:40]}×{n}" for r, n in Counter(reasons).most_common(6)), ""]
    lines += ["| 记录 | 说话人 | 原文 | 改前 | 改后 | 理由 |", "| --- | --- | --- | --- | --- | --- |"]
    lines += ["| " + " | ".join(c.replace("|", "\\|") for c in r) + " |" for r in rows]
    out = run / "report.md"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:5]))
    print(f"→ {out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--locale", choices=sorted(LANG), default="zh-Hans")
    parser.add_argument("--draft", action="append", default=[], help="draft run tag(s), later ones win")
    parser.add_argument("--kind", choices=("story", "battle", "all"), default="story")
    parser.add_argument("--scenes", help="comma-separated scene indices")
    parser.add_argument("--size", type=int, default=30)
    parser.add_argument("--model", default="deepseek-v4-pro-0813")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--env-file", type=Path)
    args = parser.parse_args()
    if args.report:
        cmd_report(args)
    else:
        if not args.draft:
            raise SystemExit("--draft is required")
        cmd_run(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
