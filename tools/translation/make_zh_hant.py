#!/usr/bin/env python3
"""Write Traditional Chinese (zh-Hant, Taiwan) from the Simplified Chinese.

Every zh-Hant file is generated: run this again after changing zh-Hans text or
content/translation/zh-Hant/names.json, and never edit the outputs by hand. The
conversion (OpenCC s2twp, Taiwan names, 「」 quotation marks) is
src/srw64_native/zh_hant.py. Writes:

  content/locales/terms/zh-Hant.json   the term table, name sections from names.json
  content/locales/zh-Hant.json         the UI labels, and the entries apply_terms expands
  content/dialogue/zh-Hant/            every dialogue file of content/dialogue/zh-Hans
  content/stages/*.json                a "zh-Hant" beside each "zh-Hans" (export_stage_info.py
                                       does the same when it rewrites them)

    PYTHONPATH=src:tools .venv/bin/python tools/translation/make_zh_hant.py [--check]
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path

from srw64_native.catalog import compile_locale, source_catalog
from srw64_native.terms import expand, load_sections, merge
from srw64_native.zh_hant import Converter

ROOT = Path(__file__).resolve().parents[2]
LOCALE = "zh-Hant"
DISPLAY_NAME = "繁體中文"
MARKER = "# 自動產生：tools/translation/make_zh_hant.py 由 content/dialogue/zh-Hans 轉換。重新產生時會整體覆蓋；要修改請把條目複製到使用者目錄（見 docs/guide/dialogue-text.md）。"


def dump(document) -> str:
    return json.dumps(document, ensure_ascii=False, indent=2) + "\n"


def terms(converter: Converter) -> dict:
    simplified = json.loads((ROOT / "content/locales/terms/zh-Hans.json").read_text())
    sections = {section: {ja: converter.term(section, ja, hans) for ja, hans in rows.items()}
                for section, rows in simplified["sections"].items()}
    return {**simplified, "locale": LOCALE, "sections": sections}


def locale(converter: Converter, table: dict) -> dict:
    simplified = json.loads((ROOT / "content/locales/zh-Hans.json").read_text())
    japanese = json.loads((ROOT / "content/locales/ja.json").read_text())["ui"]
    if any(row.get("origin") != "terms" for row in simplified["entries"]):
        raise ValueError("zh-Hans has hand-written entries; convert them here too")
    document = {**simplified, "locale": LOCALE, "font": "PingFangTC-Medium", "display_name": DISPLAY_NAME,
                "scope": f"Generated from zh-Hans by tools/translation/make_zh_hant.py. {simplified['scope']}"
                         .replace("zh-Hans.json", "zh-Hant.json").replace("dialogue/zh-Hans", "dialogue/zh-Hant"),
                "ui": {key: converter.convert(text, japanese.get(key)) for key, text in simplified["ui"].items()},
                "entries": []}
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    document = merge(document, expand(load_sections(ROOT), table, sources), hashes)
    compile_locale(document, sources, hashes)
    return document


def dialogue_file(converter: Converter, text: str) -> str:
    """One dialogue file: the Japanese (> lines) of an entry decides which names it has."""
    lines = text.split("\n")
    out, start = [], 0
    if lines and lines[0].startswith("# 自动生成"):
        out.append(MARKER)
        start = 1
    block: list[str] = []

    def flush():
        source = "\n".join(line[1:] for line in block if line.startswith(">"))
        for line in block:
            if line.startswith(">") or line == "---" or not line.strip():
                out.append(line)
            elif line.startswith("@"):
                record, _, note = line.partition(" ")
                # The note is "中文名 · 日本語名（…）": its Japanese is in it.
                out.append(record + (" " + converter.convert(note, note) if note else ""))
            else:
                out.append(converter.convert(line, source))
        block.clear()

    for line in lines[start:]:
        if line.startswith("@"):
            flush()
        block.append(line)
    flush()
    return "\n".join(out)


def outputs(converter: Converter) -> dict[Path, str]:
    table = terms(converter)
    files = {ROOT / "content/locales/terms/zh-Hant.json": dump(table),
             ROOT / "content/locales/zh-Hant.json": dump(locale(converter, table))}
    simplified = ROOT / "content/dialogue/zh-Hans"
    for path in sorted(simplified.rglob("*.txt")):
        files[ROOT / "content/dialogue" / LOCALE / path.relative_to(simplified)] = \
            dialogue_file(converter, path.read_text(encoding="utf-8"))
    for path in sorted((ROOT / "content/stages").glob("*.json")):
        files[path] = json.dumps(converter.add_to(json.loads(path.read_text())), ensure_ascii=False, indent=1) + "\n"
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; fail when a zh-Hant file is stale")
    args = parser.parse_args()
    files = outputs(Converter(ROOT))
    dialogue = ROOT / "content/dialogue" / LOCALE
    left = sorted(p for p in dialogue.rglob("*.txt") if p not in files) if dialogue.is_dir() else []
    stale = [p for p, text in files.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
    for path in stale + left:
        print(f"{path.relative_to(ROOT)} is {'left over' if path in left else 'stale'}", file=sys.stderr if args.check else sys.stdout)
    if args.check:
        return 1 if stale or left else 0
    # The dialogue tree is replaced whole, so a file zh-Hans dropped goes too.
    with tempfile.TemporaryDirectory(dir=ROOT / "content/dialogue") as temp:
        for path, text in files.items():
            if path.is_relative_to(dialogue):
                target = Path(temp) / path.relative_to(dialogue)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text, encoding="utf-8")
        if dialogue.exists():
            shutil.rmtree(dialogue)
        shutil.copytree(temp, dialogue)
    for path, text in files.items():
        if not path.is_relative_to(dialogue) and (not path.exists() or path.read_text(encoding="utf-8") != text):
            path.write_text(text, encoding="utf-8")
    print(f"{len(stale)} files written, {len(left)} removed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
