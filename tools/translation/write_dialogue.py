#!/usr/bin/env python3
"""Write the machine translations as dialogue text files: content/dialogue/<locale>/.

Format and loader: docs/guide/dialogue-text.md, src/srw64_native/dialogue_text.py. Both
locales are written together because the bundled catalogs must share one key set: a line
gets its translation only when every locale has a checked one; otherwise it is written as
an empty template in every locale, with any surviving draft kept as a comment for the
reviewer. Files without the generated-file marker are hand-maintained: they are never
rewritten, and their keys are left out of the generated files. The new tree is checked
with dialogue_text.load before anything is replaced.

Layout: story/scene-NNNN.txt (lines in script order, each key in the first scene that
uses it), battle/speaker-NNN.txt (per-character situation blocks, ids below 14227),
battle/moves-NNNNN.txt (weapon lines and combination attacks, by hundred), battle/special.txt
and intro.txt.

    PYTHONPATH=src .venv/bin/python -B tools/translation/write_dialogue.py \\
        --runs zh-Hans=zh-v1,zh-v1-review --runs en=en-v1,en-v1-review
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from references import Characters  # noqa: E402
from run_mt import RUNS, effective, final_target, load_records, load_scenes, paragraphs  # noqa: E402
from srw64_native.catalog import source_catalog  # noqa: E402
from srw64_native.dialogue_text import format_entry, load  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "content/dialogue"
MARKER = "# 自动生成：tools/translation/write_dialogue.py。重新生成时会整体覆盖；要修改请把条目复制到用户目录（见 docs/guide/dialogue-text.md）。"
GENERIC_BATTLE_END = 14227  # speaker-ordered situation blocks end with the generic soldiers at 14226


def failed_drafts(tags: list[str]) -> dict[str, dict]:
    result = {}
    for tag in tags:
        for path in sorted((RUNS / tag).rglob("*.json")):
            doc = json.loads(path.read_text())
            if doc.get("schema") == "srw64.mt-batch.v1" and doc.get("stage", "draft") == "draft":
                for item in doc["items"]:
                    if item.get("errors"):
                        result[item["key"]] = item
    return result


def hand_keys(locale_dir: Path) -> tuple[set[str], set[Path]]:
    keys, files = set(), set()
    for path in sorted(locale_dir.rglob("*.txt")) if locale_dir.is_dir() else []:
        text = path.read_text(encoding="utf-8")
        if text.startswith(MARKER):
            continue
        files.add(path.relative_to(locale_dir))
        for line in text.splitlines():
            if line.startswith("@"):
                head = line[1:].split()[0]
                keys.add(f"intro:{head[6:]}" if head.startswith("intro:") else f"base:t00_{int(head):05d}")
    return keys, files


def file_for(record: dict, owner: dict[str, int]) -> str:
    if record["category"] == "story.intro":
        return "intro.txt"
    if record["category"] in ("story.dialogue", "story.choice"):
        return f"story/scene-{owner[record['key']]:04d}.txt"
    if record["category"] == "battle.special":
        return "battle/special.txt"
    if record["id"] < GENERIC_BATTLE_END:
        return f"battle/speaker-{record['context']['speaker_id']:03d}.txt"
    return f"battle/moves-{record['id'] // 100 * 100:05d}.txt"


def speaker_of(record: dict) -> str | None:
    context = record.get("context") or {}
    if "occurrences" in context:
        return (context["occurrences"] or [{}])[0].get("speaker")
    return context.get("speaker")


def intro_entry(record: dict, target: str | None, note: str, comments: list[str]) -> str:
    lines = [f"@intro:{record['context']['resource']} {note}", *comments]
    lines += [f"> {p}" for p in paragraphs(record["source"])]
    lines += [p for p in target.split("\n\n")] if target else []
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", action="append", required=True, help="locale=tag[,tag...]; later tags win")
    args = parser.parse_args()
    runs = dict(spec.split("=", 1) for spec in args.runs)
    runs = {locale: tags.split(",") for locale, tags in runs.items()}
    records, scenes = load_records(), load_scenes()
    sources, _, _ = source_catalog(ROOT, ROOT / "rom.z64")
    owner: dict[str, int] = {}
    order: dict[str, int] = {}
    for scene_id in sorted(scenes):
        for line in scenes[scene_id]["lines"]:
            owner.setdefault(line["key"], scene_id)
            order.setdefault(line["key"], len(order))
    wanted = [r for r in records.values() if r["category"] in
              ("story.dialogue", "story.choice", "story.intro", "battle.quote", "battle.special")]
    wanted.sort(key=lambda r: (file_for(r, owner), order.get(r["key"], r.get("id", 0)), r["key"]))
    good = {locale: effective(tags) for locale, tags in runs.items()}
    bad = {locale: failed_drafts(tags) for locale, tags in runs.items()}
    hand, hand_files = set(), {}
    for locale in runs:
        keys, files = hand_keys(OUT / locale)
        hand |= keys
        hand_files[locale] = files
    shared = set.intersection(*(set(g) for g in good.values()))
    stats = {"translated": 0, "templates": 0, "hand_skipped": 0}
    staging = Path(tempfile.mkdtemp(prefix=".dialogue-", dir=OUT.parent))
    try:
        for locale in runs:
            names = Characters(locale).names
            files: dict[str, list[str]] = defaultdict(list)
            for record in wanted:
                key = record["key"]
                if key in hand:
                    stats["hand_skipped"] += locale == next(iter(runs))
                    continue
                label = speaker_of(record) or ""
                base = Characters.base_name(label) or ""
                note = f"{names[base]} · {label}" if base in names else label
                comments, target = [], None
                if key in shared:
                    item = good[locale][key]
                    target = final_target(record, item, locale)
                    for field, title in (("reason", "审校"), ("flag", "机翻疑问")):
                        if item.get(field):
                            comments.append(f"# {title}：{item[field]}")
                    if item.get("warnings"):
                        comments.append("# 检查提示：" + "；".join(item["warnings"]))
                    stats["translated"] += locale == next(iter(runs))
                else:
                    stats["templates"] += locale == next(iter(runs))
                    mine = good[locale].get(key) or bad[locale].get(key)
                    why = "；".join(bad[locale][key]["errors"]) if key in bad[locale] else "另一种语言未通过检查"
                    comments.append(f"# 待译：机翻未采用（{why}）")
                    if mine and isinstance(mine.get("tr"), list):
                        comments += [f"# 草稿第 {n + 1} 页：{page}" for n, page in enumerate(mine["tr"]) if isinstance(page, str)]
                if record["category"] == "story.intro":
                    files["intro.txt"].append(intro_entry(record, target, note, comments))
                    continue
                entry = format_entry(key, sources[key], target, note, options=record["category"] == "story.choice")
                head, _, rest = entry.partition("\n")
                files[file_for(record, owner)].append("\n".join([head, *comments, rest]).rstrip("\n") + "\n")
            root = staging / locale
            for name, entries in files.items():
                path = root / name
                if Path(name) in hand_files[locale]:
                    raise SystemExit(f"{locale}/{name} is hand-maintained but would receive generated entries")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(MARKER + "\n\n" + "\n".join(entries), encoding="utf-8")
            for name in hand_files[locale]:
                target_path = root / name
                target_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(OUT / locale / name, target_path)
        loaded = {}
        for locale in runs:
            targets, intro, problems = load([staging / locale], sources)
            if problems:
                raise SystemExit(f"{locale}: {len(problems)} problems, e.g. " + "; ".join(map(str, problems[:5])))
            loaded[locale] = (targets, intro)
        key_sets = {frozenset(t) for t, _ in loaded.values()}
        if len(key_sets) != 1:
            raise SystemExit("locales would ship different key sets")
        for locale in runs:
            final = OUT / locale
            for old in final.rglob("*.txt") if final.is_dir() else []:
                if old.read_text(encoding="utf-8").startswith(MARKER):
                    old.unlink()
            for new in (staging / locale).rglob("*.txt"):
                dest = final / new.relative_to(staging / locale)
                if dest.exists() and not dest.read_text(encoding="utf-8").startswith(MARKER):
                    continue  # hand-maintained file, already in place
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(new, dest)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    summary = {locale: {"lines": len(t), "intro": len(i), "files": sum(1 for _ in (OUT / locale).rglob("*.txt"))}
               for locale, (t, i) in loaded.items()}
    print(json.dumps({**stats, "locales": summary}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
