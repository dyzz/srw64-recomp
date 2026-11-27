#!/usr/bin/env python3
"""Write Traditional Chinese (zh-Hant Taiwan, zh-Hant-HK Hong Kong) from the Simplified Chinese.

Every zh-Hant and zh-Hant-HK file is generated: run this again after changing zh-Hans
text or content/translation/zh-Hant(-HK)/, and never edit the outputs by hand. The
conversions are src/srw64_native/zh_hant.py (OpenCC s2twp, Taiwan names, 「」 quotation
marks) and src/srw64_native/zh_hant_hk.py (Hong Kong names, words and characters).
Hong Kong stands on Taiwan (its locale has "fallback": "zh-Hant"), so its files hold only
what reads differently in Hong Kong. Writes:

  content/locales/terms/zh-Hant.json      the term table, name sections from names.json
  content/locales/zh-Hant.json            the UI labels, and the entries apply_terms expands
  content/dialogue/zh-Hant/               every dialogue file of content/dialogue/zh-Hans
  content/locales/terms/zh-Hant-HK.json   Hong Kong's whole term table
  content/locales/zh-Hant-HK.json         the labels and entries Hong Kong writes differently
  content/dialogue/zh-Hant-HK/            the dialogue entries Hong Kong writes differently
  content/locales/library-names.json      a "zh-Hant-HK" after "zh-Hant" where it differs, and
                                          src/host/library_names_table.hpp from it
  content/stages/*.json                   a "zh-Hant" beside each "zh-Hans", and a "zh-Hant-HK"
                                          where it differs (export_stage_info.py does the same
                                          when it rewrites them)

    PYTHONPATH=src:tools .venv/bin/python tools/translation/make_zh_hant.py [--check]
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

from srw64_native.catalog import compile_locale, source_catalog
from srw64_native.terms import expand, load_sections, merge
from srw64_native.zh_hant import Converter
from srw64_native.zh_hant_hk import HongKong

ROOT = Path(__file__).resolve().parents[2]
LOCALE = "zh-Hant"
DISPLAY_NAME = "繁體中文"
HK = "zh-Hant-HK"
HK_DISPLAY_NAME = "繁體中文（香港）"
MARKER = "# 自動產生：tools/translation/make_zh_hant.py 由 content/dialogue/zh-Hans 轉換。重新產生時會整體覆蓋；要修改請把條目複製到使用者目錄（見 docs/guide/dialogue-text.md）。"
HK_MARKER = "# 自動產生：tools/translation/make_zh_hant.py 由 content/dialogue/zh-Hans 轉換，只有香港寫法與台灣不同的條目，其餘照 content/dialogue/zh-Hant。重新產生時會整體覆蓋；要修改請把條目複製到使用者目錄（見 docs/guide/dialogue-text.md）。"


def dump(document) -> str:
    return json.dumps(document, ensure_ascii=False, indent=2) + "\n"


def terms(converter, locale_id: str = LOCALE) -> dict:
    simplified = json.loads((ROOT / "content/locales/terms/zh-Hans.json").read_text())
    sections = {section: {ja: converter.term(section, ja, hans) for ja, hans in rows.items()}
                for section, rows in simplified["sections"].items()}
    return {**simplified, "locale": locale_id, "sections": sections}


def hong_kong_locale(hk: HongKong, table: dict, taiwan: dict) -> dict:
    """Hong Kong over Taiwan: the labels and entries it writes differently."""
    simplified = json.loads((ROOT / "content/locales/zh-Hans.json").read_text())
    japanese = json.loads((ROOT / "content/locales/ja.json").read_text())["ui"]
    ui = {key: hk.convert(text, japanese.get(key)) for key, text in simplified["ui"].items()}
    document = {"schema": simplified["schema"], "locale": HK, "source_locale": simplified["source_locale"], "fallback": LOCALE, "region": "HK",
                "font": taiwan["font"], "display_name": HK_DISPLAY_NAME,
                "scope": "Hong Kong labels and records over zh-Hant (Taiwan), generated from zh-Hans by "
                         "tools/translation/make_zh_hant.py: what this leaves out reads as content/locales/zh-Hant.json. "
                         "Dialogue text lives in content/dialogue/zh-Hant-HK, over content/dialogue/zh-Hant.",
                "ui": {key: text for key, text in ui.items() if text != taiwan["ui"].get(key)}, "entries": []}
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    full = merge({**document, "ui": ui}, expand(load_sections(ROOT), table, sources), hashes)
    compile_locale(full, sources, hashes)
    # The chapter cards' brush (content/fonts/SRW64BrushTC.ttf, a subset for Taiwan's titles):
    # a Hong Kong form it lacks keeps Taiwan's, as Super Robot Wars Z's pictures do.
    brush = brush_characters()
    back = {hk_form: tw_form for tw_form, hk_form in hk.regional["characters"].items()}
    for row in full["entries"]:
        if TITLE.fullmatch(row["key"]):
            row["target"] = "".join(back.get(c, c) if c not in brush else c for c in row["target"])
    own = {row["key"]: row["target"] for row in taiwan["entries"]}
    document["entries"] = [row for row in full["entries"] if own.get(row["key"]) != row["target"]]
    return document


TITLE = re.compile(r"base:t00_00(2[89]\d|3\d\d|4[01]\d|42[0-3])")  # the chapter titles (build_title_fonts.py)


def brush_characters() -> set[str]:
    from fontTools.ttLib import TTFont
    return {chr(code) for code in TTFont(ROOT / "content/fonts/SRW64BrushTC.ttf").getBestCmap()}


def locale(converter: Converter, table: dict) -> dict:
    simplified = json.loads((ROOT / "content/locales/zh-Hans.json").read_text())
    japanese = json.loads((ROOT / "content/locales/ja.json").read_text())["ui"]
    if any(row.get("origin") != "terms" for row in simplified["entries"]):
        raise ValueError("zh-Hans has hand-written entries; convert them here too")
    document = {**simplified, "locale": LOCALE, "font": "PingFangTC-Medium", "display_name": DISPLAY_NAME, "region": "TW",
                "scope": f"Generated from zh-Hans by tools/translation/make_zh_hant.py. {simplified['scope']}"
                         .replace("zh-Hans.json", "zh-Hant.json").replace("dialogue/zh-Hans", "dialogue/zh-Hant"),
                "ui": {key: converter.convert(text, japanese.get(key)) for key, text in simplified["ui"].items()},
                "entries": []}
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    document = merge(document, expand(load_sections(ROOT), table, sources), hashes)
    compile_locale(document, sources, hashes)
    return document


def dialogue_blocks(converter, text: str) -> tuple[bool, list[list[str]]]:
    """One dialogue file's entries: the Japanese (> lines) of an entry decides which names it
    has. The first block is what comes before the first entry."""
    lines = text.split("\n")
    generated = bool(lines) and lines[0].startswith("# 自动生成")
    blocks: list[list[str]] = [[]]
    block: list[str] = []

    def flush():
        source = "\n".join(line[1:] for line in block if line.startswith(">"))
        out = blocks[-1]
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

    for line in lines[1 if generated else 0:]:
        if line.startswith("@"):
            flush()
            blocks.append([])
        block.append(line)
    flush()
    return generated, blocks


def dialogue_file(converter: Converter, text: str) -> str:
    generated, blocks = dialogue_blocks(converter, text)
    return "\n".join(([MARKER] if generated else []) + [line for block in blocks for line in block])


def hong_kong_dialogue(hk: HongKong, taiwan: Converter, text: str) -> str | None:
    """The entries of one dialogue file that Hong Kong writes differently, or None."""
    _, ours = dialogue_blocks(taiwan, text)
    _, theirs = dialogue_blocks(hk, text)
    different = [block for mine, block in zip(ours[1:], theirs[1:]) if mine != block]
    if not different:
        return None
    lines = [HK_MARKER, ""]
    for block in different:
        while block and not block[-1].strip():
            block = block[:-1]
        lines += block + [""]
    return "\n".join(lines)


def library_names(hk: HongKong, document: dict) -> str:
    """The Library's own names with a Hong Kong one after zh-Hant where it reads differently
    (written one row per line, as the file is)."""
    lines = ["{", f' "schema": {json.dumps(document["schema"])},', f' "about": {json.dumps(document["about"], ensure_ascii=False)},']
    kinds = [kind for kind in document if kind not in ("schema", "about")]
    for k, kind in enumerate(kinds):
        lines.append(f' "{kind}": {{')
        rows = list(document[kind].items())
        for i, (record, names) in enumerate(rows):
            out = {}
            for locale, text in names.items():
                if locale == HK:
                    continue
                out[locale] = text
                if locale == LOCALE and "zh-Hans" in names and (own := hk.name(names["zh-Hans"])) != text:
                    out[HK] = own
            lines.append(f'  "{record}": {json.dumps(out, ensure_ascii=False)}' + ("," if i + 1 < len(rows) else ""))
        lines.append(" }" + ("," if k + 1 < len(kinds) else ""))
    return "\n".join(lines + ["}"]) + "\n"


def library_header(text: str) -> str:
    """src/host/library_names_table.hpp for this library-names.json (tools/content/library_names.py)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("library_names", ROOT / "tools/content/library_names.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory() as temp:
        module.SOURCE = Path(temp) / "library-names.json"
        module.SOURCE.write_text(text, encoding="utf-8")
        return module.header()


def outputs(converter: Converter, hk: HongKong | None = None) -> dict[Path, str]:
    hk = hk or HongKong(ROOT)
    table = terms(converter)
    taiwan = locale(converter, table)
    hk_table = terms(hk, HK)
    files = {ROOT / "content/locales/terms/zh-Hant.json": dump(table),
             ROOT / "content/locales/zh-Hant.json": dump(taiwan),
             ROOT / "content/locales/terms/zh-Hant-HK.json": dump(hk_table),
             ROOT / "content/locales/zh-Hant-HK.json": dump(hong_kong_locale(hk, hk_table, taiwan))}
    simplified = ROOT / "content/dialogue/zh-Hans"
    for path in sorted(simplified.rglob("*.txt")):
        text = path.read_text(encoding="utf-8")
        files[ROOT / "content/dialogue" / LOCALE / path.relative_to(simplified)] = dialogue_file(converter, text)
        if (own := hong_kong_dialogue(hk, converter, text)) is not None:
            files[ROOT / "content/dialogue" / HK / path.relative_to(simplified)] = own
    library = ROOT / "content/locales/library-names.json"
    files[library] = library_names(hk, json.loads(library.read_text(encoding="utf-8")))
    files[ROOT / "src/host/library_names_table.hpp"] = library_header(files[library])
    for path in sorted((ROOT / "content/stages").glob("*.json")):
        files[path] = json.dumps(hk.add_to(converter.add_to(json.loads(path.read_text()))), ensure_ascii=False, indent=1) + "\n"
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing; fail when a zh-Hant or zh-Hant-HK file is stale")
    args = parser.parse_args()
    files = outputs(Converter(ROOT))
    trees = [ROOT / "content/dialogue" / LOCALE, ROOT / "content/dialogue" / HK]
    left = sorted(p for tree in trees if tree.is_dir() for p in tree.rglob("*.txt") if p not in files)
    stale = [p for p, text in files.items() if not p.exists() or p.read_text(encoding="utf-8") != text]
    for path in stale + left:
        print(f"{path.relative_to(ROOT)} is {'left over' if path in left else 'stale'}", file=sys.stderr if args.check else sys.stdout)
    if args.check:
        return 1 if stale or left else 0
    # The dialogue trees are replaced whole, so a file zh-Hans dropped goes too.
    for dialogue in trees:
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
        if not any(path.is_relative_to(tree) for tree in trees) and (not path.exists() or path.read_text(encoding="utf-8") != text):
            path.write_text(text, encoding="utf-8")
    print(f"{len(stale)} files written, {len(left)} removed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
