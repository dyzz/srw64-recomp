#!/usr/bin/env python3
"""Polish the story translations against a human reference (docs/design/translation-plan.md).

Input: assets/translation-runs/reference-en/aligned/scene-NNNN.jsonl from align_reference.py.
For every reference-matched line the model gets the Japanese pages, our English and Chinese
pages and the reference sentence, and reports whether ours misreads the Japanese (meaning,
omission, who-speaks-to-whom, tone). The Japanese is the judge and the reference a second
opinion; fixes are rephrased, never copied from the reference; names, terms and placeholders
stay ours. The same finding is checked in Chinese too.

Output: two srw64.mt-batch.v1 runs (stage "polish"), one per locale, layered on top of the
draft runs like a review run:

    PYTHONPATH=src .venv/bin/python -B tools/translation/polish_reference.py --scenes 1,4,5 \\
        --tag ref-v1 --en en-v1,en-v1-review,en-v1-fixes --zh zh-v1,...,zh-v1-names2 --model deepseek-v4-pro-0813
    → assets/translation-runs/en-ref-v1/ and zh-ref-v1/

    PYTHONPATH=src .venv/bin/python -B tools/translation/polish_reference.py --report --tag ref-v1
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
from run_mt import (LANG, RUNS, WORLD, Context, check_item, done, effective, load_records, localized,  # noqa: E402
                    request, save, source_pages, speaker_cards, usage_of)

ROOT = Path(__file__).resolve().parents[2]
ALIGNED = ROOT / "assets/translation-runs/reference-en/aligned"
PROMPT_VERSION = "srw64-polish-ref-v1"
LOCALES = ("en", "zh-Hans")

SYSTEM = ("你是资深游戏本地化审校，精通日语、英语和简体中文，审校 N64 游戏《超级机器人大战64》的剧情对白译文。\n" + WORLD + "\n"
          "每行给出：speaker 说话人；ja 日文原文（按游戏翻页拆成数组）；en、zh 是我们现有的英文和中文译文（与 ja 页数相同）；"
          "ref 是一位英语玩家的人工翻译（整条，不分页），作为第二意见。ctx 为 true 的行只作上下文，不要返回。\n"
          "规则：\n"
          "1. 日文原文是唯一裁判。ref 用来提示可能的漏译、误译、指代（谁对谁说、人称、性别）错误、语气不符；ref 本身也会出错，"
          "与 ja 不符时不采信。\n"
          "2. 只在确有问题时修改：意思错、漏译、增译、指代错、明显不符合人物身份或语气、与前后文接不上。"
          "措辞风格的细微差别、同义改写一律不改。\n"
          "3. 修改必须用你自己的表达，不得照抄或近似照抄 ref 的句子（ref 只参考不抄）。\n"
          "4. 人名、机体、招式、组织等专名以我们现有译文为准，不要换成 ref 的写法；{HeroName} 这类占位符原样保留，个数和顺序不变。\n"
          "5. 页数与 ja 相同，逐页对应，页内不换行；引号、标点风格跟随现有译文（英文弯引号 “ ”，只用 ASCII 标点；中文“”、全角标点）。\n"
          "6. 英文里发现的问题也要核对中文是否同样存在，有就一并修正；中文本身的问题也可指出。\n"
          "输出 JSON 对象 {\"items\":[{\"id\":\"…\",\"verdict\":\"ok|en|zh|both\",\"en\":[\"…\"],\"en_reason\":\"…\","
          "\"zh\":[\"…\"],\"zh_reason\":\"…\"}]}：每个非 ctx 的 id 一条。verdict 为 ok 时只给 id 和 verdict，不要附带译文。"
          "verdict 为 en、zh 或 both 时，给出对应语言**改好后**的完整译文数组和中文 reason（先说原文的意思，再说改了什么）；"
          "译文数组必须与现有译文不同，reason 里说的修改必须真正体现在译文里，不能只写意见而原样返回。\n")


def scene_batches(rows: list[dict], size: int = 24) -> list[list[dict]]:
    """Consecutive rows in script order, each batch holding `size` reference-matched lines plus
    the unmatched lines between them as context."""
    batches, current, matched = [], [], 0
    for row in rows:
        current.append(row)
        if row.get("ref"):
            matched += 1
        if matched >= size:
            batches.append(current)
            current, matched = [], 0
    if matched:
        batches.append(current)
    elif current and batches:
        batches[-1].extend(current)
    return batches


def payload_for(ctx: dict[str, Context], rows: list[dict], records: dict, drafts: dict[str, dict],
                title: str) -> tuple[dict, dict[str, str]]:
    lines, ids = [], {}
    for n, row in enumerate(rows):
        key = row["key"]
        record = records.get(key)
        line: dict = {"id": f"L{n}", "speaker": row.get("speaker") or "（旁白／系统）"}
        if not record or not row.get("ref") or any(key not in drafts[l] for l in LOCALES):
            line["ctx"] = True
            line["ja"] = row["ja"]
            lines.append(line)
            continue
        ids[line["id"]] = key
        line["ja"] = source_pages(record, "en")
        for locale in LOCALES:
            line[locale.split("-")[0]] = localized(drafts[locale][key]["tr"], locale)
        line["ref"] = row["ref"]
        lines.append(line)
    labels = [row.get("speaker_ja") for row in rows]
    payload = {"scene": title, "speakers": speaker_cards(ctx["en"], labels), "lines": lines}
    return payload, ids


def polish_batch(ctx: dict[str, Context], args, scene_no: int, part: int, rows: list[dict], records: dict,
                 drafts: dict[str, dict], title: str) -> None:
    batch_id = f"scene-{scene_no:04d}-{part:02d}"
    outs = {locale: RUNS / f"{locale.split('-')[0]}-{args.tag}" / f"{batch_id}.json" for locale in LOCALES}
    if all(done(o) for o in outs.values()):
        return
    payload, ids = payload_for(ctx, rows, records, drafts, title)
    if not ids:
        return
    call, doc = request(args, SYSTEM, payload)
    items = doc.get("items", [])
    if isinstance(items, dict):
        items = [{"id": k, **v} for k, v in items.items() if isinstance(v, dict)]
    verdicts = {}
    per_locale: dict[str, list] = {locale: [] for locale in LOCALES}
    for item in items:
        key = ids.get(item.get("id"))
        if not key:
            continue
        verdicts[key] = item.get("verdict", "ok")
        for locale in LOCALES:
            short = locale.split("-")[0]
            pages = item.get(short)
            if not isinstance(pages, list):
                continue
            checked = check_item(records[key], pages, ctx[locale].terms, locale)
            checked["reason"] = item.get(f"{short}_reason") or ""
            checked["draft"] = drafts[locale][key]["tr"]
            checked["verdict"] = verdicts[key]
            if not checked.get("errors") and checked["target"] == drafts[locale][key]["target"]:
                checked["errors"] = ["与初稿相同"]
            per_locale[locale].append({"id": item["id"], **checked})
    for locale, out in outs.items():
        result = {"schema": "srw64.mt-batch.v1", "status": "done", "id": batch_id, "kind": "story",
                  "locale": locale, "stage": "polish", "model": args.model, "prompt_version": PROMPT_VERSION,
                  "reference": "serenesforest-balcerzak-lp", "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                  "usage": [usage_of(call)], "reviewed": len(ids),
                  "verdicts": {key: verdicts[key] for key in ids.values() if key in verdicts},
                  "items": per_locale[locale]}
        save(out, result)
    n_en = sum(not i.get("errors") for i in per_locale["en"])
    n_zh = sum(not i.get("errors") for i in per_locale["zh-Hans"])
    bad = sum(bool(i.get("errors")) for l in per_locale.values() for i in l)
    print(f"{batch_id}: {len(ids)} lines, en changed {n_en}, zh changed {n_zh}, rejected {bad}, "
          f"{call.prompt_tokens}+{call.completion_tokens} tokens, {call.elapsed:.0f}s", flush=True)


def cmd_run(args) -> None:
    args.credentials = dashscope.load_env(args.env_file)
    records = load_records()
    ctx = {locale: Context(locale) for locale in LOCALES}
    drafts = {"en": effective(args.en.split(",")), "zh-Hans": effective(args.zh.split(","))}
    index = {s["scene"]: s for s in json.loads((ROOT / "assets/original-data/story/index.json").read_text())["scenes"]}
    wanted = {int(s) for s in args.scenes.split(",")} if args.scenes else None
    jobs = []
    for path in sorted(ALIGNED.glob("scene-*.jsonl")):
        scene_no = int(path.stem.split("-")[1])
        if wanted is not None and scene_no not in wanted:
            continue
        rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
        for part, batch in enumerate(scene_batches(rows)):
            jobs.append((scene_no, part, batch, index[scene_no]["title"]))
    print(f"{len(jobs)} batches, model {args.model}, tag {args.tag}", flush=True)

    def guarded(job):
        scene_no, part, batch, title = job
        try:
            polish_batch(ctx, args, scene_no, part, batch, records, drafts, title)
        except Exception as exc:  # keep the other batches going
            print(f"scene-{scene_no:04d}-{part:02d}: FAILED {type(exc).__name__}: {exc}", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(guarded, jobs))


def cmd_report(args) -> None:
    records = load_records()
    lines = [f"# 参考对照润色：{args.tag}", ""]
    totals = {}
    for locale in LOCALES:
        run = RUNS / f"{locale.split('-')[0]}-{args.tag}"
        reviewed = changed = rejected = 0
        rows = []
        for path in sorted(run.glob("*.json")):
            doc = json.loads(path.read_text())
            reviewed += doc.get("reviewed", 0) if locale == "en" else 0
            for item in doc["items"]:
                if item.get("errors"):
                    rejected += 1
                    continue
                changed += 1
                key = item["key"]
                before = "▸".join(localized(item["draft"], locale)) if isinstance(item["draft"], list) else str(item["draft"])
                after = "▸".join(item["tr"])
                rows.append((key, records[key]["display"].replace("\n", " "), before, after, item.get("reason", "")))
        totals[locale] = (reviewed, changed, rejected)
        lines += [f"## {LANG[locale]['name']}：改 {changed} 条，检查未通过 {rejected} 条", "",
                  "| 记录 | 原文 | 改前 | 改后 | 理由 |", "| --- | --- | --- | --- | --- |"]
        lines += ["| `%s` | %s | %s | %s | %s |" % tuple(c.replace("|", "\\|") for c in r) for r in rows]
        lines.append("")
    reviewed = totals["en"][0]
    lines.insert(2, f"对照行数 {reviewed}；英文改 {totals['en'][1]}（{totals['en'][1] / max(1, reviewed):.0%}），"
                    f"中文改 {totals['zh-Hans'][1]}（{totals['zh-Hans'][1] / max(1, reviewed):.0%}）。")
    out = RUNS / f"en-{args.tag}" / "report.md"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:3]))
    print(f"→ {out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", required=True, help="run tag; outputs en-<tag> and zh-<tag>")
    parser.add_argument("--report", action="store_true", help="summarize an existing run instead of calling the model")
    parser.add_argument("--scenes", help="comma-separated scene indices; default every aligned scene")
    parser.add_argument("--en", default="en-v1,en-v1-review,en-v1-fixes", help="English draft runs, later win")
    parser.add_argument("--zh", default="zh-v1,zh-v1-review,zh-v1-joins,zh-v1-final,zh-v1-final2,zh-v1-names,"
                                        "zh-v1-fixes,zh-v1-names2", help="Chinese draft runs, later win")
    parser.add_argument("--model", default="deepseek-v4-pro-0813")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--env-file", type=Path)
    args = parser.parse_args()
    if args.report:
        cmd_report(args)
    else:
        cmd_run(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
