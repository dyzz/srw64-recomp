#!/usr/bin/env python3
"""Scene-by-scene close reading (docs/design/dialogue-polish-plan.md, stage 2).

    PYTHONPATH=src .venv/bin/python -B tools/translation/read_scene.py dump --scene 4 [--zh-runs ... --en-runs ...]
        prints every line of the scene in script order: id, speaker, Japanese pages (|), then the
        current English and Chinese pages (▸ between pages) as the shipped runs render them.
    PYTHONPATH=src .venv/bin/python -B tools/translation/read_scene.py apply --fixes fixes.json --tag v3-read
        fixes.json: {"base:t00_17715": {"en": ["page", ...], "zh": ["页", ...], "reason": "..."}, ...}
        each locale's pages are checked (page count, placeholders, punctuation) and appended to
        assets/translation-runs/{en,zh}-<tag>/<scene>.json, which write_dialogue.py layers last.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

APPLY_STAMP = time.strftime("%y%m%d%H%M%S")
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_mt import RUNS, Context, check_item, effective, load_records, load_scenes, localized, save, source_pages  # noqa: E402

ZH_RUNS = "zh-v1,zh-v1-review,zh-v1-joins,zh-v1-final,zh-v1-final2,zh-v1-names,zh-v1-fixes,zh-v1-names2,zh-v1-junk,zh-v2-style-ok,zh-v1-consist"
EN_RUNS = "en-v1,en-v1-review,en-v1-fixes,en-v1-junk,en-v2-style-ok,en-v1-consist"


def cmd_dump(args) -> None:
    records, scenes = load_records(), load_scenes()
    scene = scenes[args.scene]
    en = effective(args.en_runs.split(",") + ([f"en-{args.tag}"] if args.tag else []))
    zh = effective(args.zh_runs.split(",") + ([f"zh-{args.tag}"] if args.tag else []))
    print(f"### scene {args.scene} {scene['title']} ({len(scene['lines'])} lines)")
    seen = set()
    for line in scene["lines"]:
        if line.get("kind") != "dialogue" or line["key"] in seen:
            continue
        seen.add(line["key"])
        key = line["key"]
        record = records.get(key)
        if not record:
            continue
        ja = "|".join(source_pages(record, "en"))
        e = "▸".join(localized(en[key]["tr"], "en")) if key in en else "(none)"
        z = "▸".join(localized(zh[key]["tr"], "zh-Hans")) if key in zh else "(none)"
        print(f"{key.split('_')[1]} {line.get('speaker') or '-'}\n J {ja}\n E {e}\n Z {z}")


def cmd_apply(args) -> None:
    records = load_records()
    fixes = json.loads(Path(args.fixes).read_text(encoding="utf-8"))
    ctx = {"en": Context("en"), "zh-Hans": Context("zh-Hans")}
    drafts = {"en": effective(args.en_runs.split(",")), "zh-Hans": effective(args.zh_runs.split(","))}
    docs = {}
    counts = {"en": 0, "zh-Hans": 0}
    for key, fix in fixes.items():
        record = records[key]
        for locale, short in (("en", "en"), ("zh-Hans", "zh")):
            pages = fix.get(short)
            if not pages:
                continue
            checked = check_item(record, pages, ctx[locale].terms, locale)
            if checked.get("errors"):
                print(f"{key} {short}: REJECTED {checked['errors']}", file=sys.stderr)
                continue
            draft = drafts[locale].get(key, {})
            tag = f"{short}-{args.tag}"
            # One file per apply, named so it sorts after every earlier file of the run: effective()
            # takes the last file (by path) that holds a key, so later applies must sort later.
            # (An earlier version wrote every --batch into misc.json, which sorted before scene-*.json.)
            base = args.batch or (f"scene-{args.scene:04d}" if args.scene is not None else "misc")
            name = f"z{APPLY_STAMP}-{base}"
            path = RUNS / tag / f"{name}.json"
            if path not in docs:
                docs[path] = json.loads(path.read_text()) if path.exists() else {
                    "schema": "srw64.mt-batch.v1", "status": "done", "id": name, "kind": "story", "locale": locale,
                    "stage": "read", "model": "claude", "prompt_version": "close-reading-v1",
                    "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "usage": [], "items": []}
            doc = docs[path]
            doc["items"] = [i for i in doc["items"] if i["key"] != key]
            doc["items"].append({"id": key, **checked, "draft": draft.get("tr"), "base_stage": draft.get("stage"),
                                 "reason": fix.get("reason", "精读修正")})
            counts[locale] += 1
    for path, doc in docs.items():
        save(path, doc)
    print(f"applied en {counts['en']}, zh {counts['zh-Hans']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("dump", "apply"):
        p = sub.add_parser(name)
        p.add_argument("--scene", type=int)
        p.add_argument("--zh-runs", default=ZH_RUNS)
        p.add_argument("--en-runs", default=EN_RUNS)
        p.add_argument("--tag", default="v3-read" if name == "apply" else None)
        if name == "apply":
            p.add_argument("--fixes", required=True)
            p.add_argument("--batch", help="batch file name inside the run (default scene-NNNN)")
    args = parser.parse_args()
    if args.command == "dump":
        if args.scene is None:
            raise SystemExit("--scene required")
        cmd_dump(args)
    else:
        cmd_apply(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
