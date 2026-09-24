#!/usr/bin/env python3
"""Export every original text record, classified and with context, for translation work.

Reads the pinned ROM through the lossless text catalog and joins it with the extracted
original-data directory (story scenes, actors, weapons). Output stays local under
assets/ because it contains the Japanese source text.

    .venv/bin/python -B tools/content/export_text.py            # -> assets/text-export/
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path

from srw64_native.catalog import sha, source_catalog, text_headers
from srw64_native.text_export import (CATEGORIES, EXPORT_SCHEMA, RANGES, TextExporter, name_slot_legend,
                                      scene_batches, summarize)

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "assets/original-data"
DEFAULT_OUT = ROOT / "assets/text-export"
PRODUCERS = ("src/srw64_native/text_export.py", "tools/content/export_text.py", "src/srw64_native/catalog.py")
INPUTS = ("content/locales/terms/sections.json", "assets/transcriptions/intro-pages.ja.json")


def jsonl(path: Path):
    with path.open(encoding="utf-8") as file:
        return [json.loads(line) for line in file if line.strip()]


def load_locales() -> dict[str, dict]:
    result = {}
    for path in sorted((ROOT / "content/locales").glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        result[doc["locale"]] = {"entries": {e["key"]: e["target"] for e in doc.get("entries", [])},
                                 "ui": doc.get("ui", {})}
    return result


def context_summary(row: dict) -> str:
    ctx = row.get("context") or {}
    if "occurrences" in ctx:
        occ = ctx["occurrences"]
        first = occ[0] if occ else {}
        where = f"场景{first.get('scene')} {first.get('phase', '')} {first.get('section', '')}".strip()
        return f"{where}；{first.get('speaker') or ''}" + (f"（另 {len(occ) - 1} 处）" if len(occ) > 1 else "")
    if "speaker_run" in ctx:
        return f"段{ctx['speaker_run']}·{ctx['position']}；{ctx.get('speaker') or ''}" + ("；合体技起句" if ctx.get("combo_lead") else "")
    return "；".join(f"{k}={v}" for k, v in ctx.items())


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["key", "id", "group", "context", "display", "chars", "flags", "native_page", "terms_section",
                         "zh-Hans", "en"])
        for row in rows:
            existing = row.get("existing", {})
            writer.writerow([row["key"], row.get("id", ""), row["group"], context_summary(row),
                             row["display"].replace("<BR>", "⏎").replace("<STOP>", "▸"), row["chars"],
                             " ".join(row["flags"]), "yes" if row["native_page"] else "", row.get("terms_section", ""),
                             existing.get("zh-Hans", ""), existing.get("en", "")])


def summary_markdown(summary: dict, total: dict) -> str:
    lines = ["# SRW64 文本导出概览", "", f"源 ROM：`{total['rom_sha256'][:16]}…`；记录 {total['records']:,} 条"
             f"（ROM {total['rom_records']:,} + 开场转写 {total['intro_records']:,} + 原生 UI {total['native_ui_records']:,}）。", "",
             "| 类别 | 名称 | 策略 | 记录 | 唯一原文 | 可见字数 | 唯一字数 | 原生页 | 词条表 | 消费者 |",
             "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    for category, s in summary.items():
        lines.append(f"| `{category}` | {s['label']} | {s['policy']} | {s['records']:,} | {s['unique']:,} | "
                     f"{s['chars']:,} | {s['unique_chars']:,} | {s['native_page_records']:,} | {s['terms_records']:,} | {s['consumer']} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    out = args.output.resolve()
    if ROOT / "assets" not in out.parents:
        parser.error("output must stay under assets/ (it contains original Japanese text)")

    manifest = json.loads((DATA / "manifest.json").read_text())
    rom = ROOT / "rom.z64"
    rom_sha = sha(rom.read_bytes())
    if manifest["rom_sha256"] != rom_sha:
        parser.error("assets/original-data was extracted from a different ROM; run make recomp-data")
    sources, hashes, _ = source_catalog(ROOT, rom)
    headers = text_headers(ROOT, rom)
    categories = {name: jsonl(DATA / f"records/{name}.jsonl") for name in ("actors", "units", "weapons")}
    story_index = json.loads((DATA / "story/index.json").read_text())
    documents = [json.loads((DATA / s["file"]).read_text()) for s in story_index["scenes"]]

    sections_path = ROOT / "content/locales/terms/sections.json"
    sections = json.loads(sections_path.read_text())["sections"] if sections_path.exists() else []
    exporter = TextExporter(sources, hashes, headers, categories, documents, load_locales(), sections)
    rows = exporter.records()
    ui_rows = exporter.native_ui_records()
    intro_rows = []
    transcription = ROOT / "assets/transcriptions/intro-pages.ja.json"
    if transcription.exists():
        doc = json.loads(transcription.read_text(encoding="utf-8"))
        for page in doc["pages"]:
            image = ROOT / page["image"]
            if image.exists() and sha(image.read_bytes()) != page["image_sha256"]:
                parser.error(f"intro page {page['resource']} image changed since it was transcribed")
        intro_rows = exporter.intro_records(doc)
    else:
        print("note: assets/transcriptions/intro-pages.ja.json missing; intro pages not exported", file=sys.stderr)
    everything = rows + intro_rows + ui_rows
    summary = summarize(everything)

    tmp = Path(tempfile.mkdtemp(prefix=".text-export-", dir=out.parent))
    try:
        with (tmp / "records.jsonl").open("w", encoding="utf-8") as file:
            for row in everything:
                file.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
        (tmp / "categories").mkdir()
        for category in summary:
            write_csv(tmp / f"categories/{category}.csv", [r for r in everything if r["category"] == category])
        (tmp / "story").mkdir()
        scenes = scene_batches(rows)
        titles = {d["scene"]: d["title"] for d in documents}
        for scene, lines in scenes.items():
            doc = next(d for d in documents if d["scene"] == scene)
            (tmp / f"story/scene-{scene:04d}.json").write_text(json.dumps({
                "schema": "srw64.text-export-scene.v1", "scene": scene, "title": titles[scene],
                "title_key": doc["title_key"], "protagonist": doc.get("protagonist"),
                "next_scenes": doc["next_scenes"], "previous_scenes": doc.get("previous_scenes", []),
                "lines": lines}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        groups: dict[int, dict] = {}
        for row in rows:
            ctx = row.get("context") or {}
            if "speaker_run" in ctx:
                group = groups.setdefault(ctx["speaker_run"], {"speaker_run": ctx["speaker_run"],
                                                              "speaker_id": ctx["speaker_id"],
                                                              "speaker": ctx["speaker"], "lines": []})
                group["lines"].append({"key": row["key"], "category": row["category"], "header": row["header"],
                                       "source": row["source"], "display": row["display"],
                                       **({"combo_lead": True} if ctx.get("combo_lead") else {})})
        (tmp / "battle").mkdir()
        (tmp / "battle/speaker-runs.json").write_text(json.dumps(
            {"schema": "srw64.text-export-battle.v1",
             "rule": "表内顺序中同一说话人（文本头前三位）连续的一段为一段。约 14000 以前是各角色的通用情境块"
                     "（攻击、被击坠、大伤、小伤、回避、光束防御、弹尽／射程外）；其后为武器专用台词与多人合体技对话，"
                     "表头后缀 0024 为合体技起句。选择表尚未逆向。",
             "runs": list(groups.values())}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        total = {"rom_sha256": rom_sha, "records": len(everything), "rom_records": len(rows),
                 "intro_records": len(intro_rows), "native_ui_records": len(ui_rows)}
        (tmp / "summary.md").write_text(summary_markdown(summary, total), encoding="utf-8")
        files = {str(p.relative_to(tmp)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(tmp.rglob("*")) if p.is_file()}
        (tmp / "manifest.json").write_text(json.dumps({
            "schema": EXPORT_SCHEMA, **total,
            "original_data_manifest_sha256": sha((DATA / "manifest.json").read_bytes()),
            "producer_sha256": {p: sha((ROOT / p).read_bytes()) for p in PRODUCERS},
            "input_sha256": {p: sha((ROOT / p).read_bytes()) for p in INPUTS if (ROOT / p).exists()},
            "categories": summary,
            "ranges": [{"start": r.start, "end": r.end, "category": r.category, "group": r.group,
                        "evidence": r.evidence} for r in RANGES],
            "tokens": {"<BR>": "换行（FFFE）", "<STOP>": "翻页并等待（FFFD）", "<END>": "结束（FFFF）",
                       "<G:XXXX>": "特殊字形；0124–012C 为动态姓名槽", **name_slot_legend()},
            "flags": {"numeric-gap": "含两个以上连续空格，原版在空位绘制数字", "fragment": "运行时拼接的句子片段",
                      "dynamic-name": "含动态姓名槽", "paged": "含 <STOP> 翻页", "blank": "无可见字符"},
            "scenes": len(scenes), "battle_speaker_runs": len(groups), "files": files,
        }, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        if out.exists():
            old = json.loads((out / "manifest.json").read_text()) if (out / "manifest.json").exists() else {}
            if old.get("schema") != EXPORT_SCHEMA:
                raise SystemExit(f"{out} exists and is not a text export; refusing to replace it")
            shutil.rmtree(out)
        tmp.rename(out)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)
    print(json.dumps({"output": str(out.relative_to(ROOT)), **total, "scenes": len(scenes),
                      "battle_speaker_runs": len(groups)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
