#!/usr/bin/env python3
"""Writes a starting language package from one of our languages (docs/design/mod-language-packs.md).

    tools/mods/language_template.py --locale ko --name 한국어 --fallback en -o build/mods/ko-source

The folder holds mod.json, dialogue/<locale>/language.json, terms.json (every UI label and
every name record, in the fallback's words; with the Japanese original beside each record
when the ROM's text export is there, assets/text-export/records.jsonl) and every dialogue
file of the fallback, the Japanese '>' lines kept for reference. Translate what is in it,
delete what you leave to the fallback, pick a licence in mod.json, then build the package:

    tools/mods/pack.py build/mods/ko-source -o build/mods
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILT_IN = ("ja", "zh-Hans", "zh-Hant", "en")
LOCALE_TAG = re.compile(r"[a-z]{2,3}(-[A-Za-z0-9]{2,8})*")


def dump(document) -> str:
    return json.dumps(document, ensure_ascii=False, indent=2) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--locale", required=True, help="the new language's code: ko, fr, pt-BR …")
    parser.add_argument("--name", required=True, help="its name in the language menu, in that language")
    parser.add_argument("--fallback", default="en", choices=BUILT_IN, help="shown where the package has no text (default en)")
    parser.add_argument("--id", help="the package id (default: lang.<locale>)")
    parser.add_argument("--records", type=Path, default=ROOT / "assets/text-export/records.jsonl",
                        help="the ROM's text export, for the Japanese beside each record (optional)")
    parser.add_argument("-o", "--output", type=Path, required=True, help="the source folder to write (must not exist)")
    args = parser.parse_args(argv)
    if args.locale in BUILT_IN or not LOCALE_TAG.fullmatch(args.locale):
        parser.error("--locale must be a new language's code such as ko, fr or pt-BR")
    if args.output.exists():
        parser.error(f"{args.output} exists")
    package_id = args.id or "lang." + args.locale.lower()

    catalog = json.loads((ROOT / "content/locales" / f"{args.fallback}.json").read_text(encoding="utf-8"))
    japanese: dict[str, str] = {}
    if args.records.is_file():
        for line in args.records.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            if row.get("table") == 0 and row.get("id", 1 << 30) < 5644:
                japanese[row["key"]] = row["source"]
    entries = {}
    for row in catalog.get("entries", []):
        entries[row["key"]] = {"text": row["target"], "ja": japanese[row["key"]]} if row["key"] in japanese else row["target"]
    folder = args.output / "dialogue" / args.locale
    folder.mkdir(parents=True)
    (args.output / "mod.json").write_text(dump({
        "schema": "srw64.mod.v1", "id": package_id, "version": "0.1.0",
        "name": {args.locale: args.name, "en": f"{args.name} language"},
        "description": {args.locale: args.name, "en": f"Adds {args.name} as a reading language."},
        "authors": []}), encoding="utf-8")
    (folder / "language.json").write_text(dump({
        "schema": "srw64.language.v1", "locale": args.locale, "name": args.name, "fallback": args.fallback}), encoding="utf-8")
    (folder / "terms.json").write_text(dump({
        "schema": "srw64.language-terms.v1", "ui": catalog["ui"], "entries": entries}), encoding="utf-8")
    source = ROOT / "content/dialogue" / args.fallback
    files = 0
    if args.fallback != "ja" and source.is_dir():
        for path in sorted(source.rglob("*.txt")):
            target = folder / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
            # Our generator's header says the file is overwritten; the package's is not.
            if lines and lines[0].startswith(("# 自动生成", "# 自動產生")):
                lines = lines[1:]
            target.write_text("".join(lines), encoding="utf-8")
            files += 1
    (folder / "fonts").mkdir()
    print(f"{args.output}: {len(catalog['ui'])} labels, {len(entries)} records "
          f"({len(japanese) and sum(1 for k in entries if k in japanese)} with Japanese), {files} dialogue files from {args.fallback}")
    print("Add fonts to fonts/ and name them in language.json when ours lack the script's letters.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
