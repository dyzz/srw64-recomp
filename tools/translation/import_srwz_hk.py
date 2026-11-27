#!/usr/bin/env python3
"""Take Super Robot Wars Z's Hong Kong tables into zh-Hant-HK (src/srw64_native/zh_hant_hk.py).

Super Robot Wars Z (our Chinese translation of it, the srwz-zh repository) settled its
Taiwan and Hong Kong text with the user in 2026-11. This copies, with the commit and
file hashes it read:

  regional.json  its Hong Kong words (config/zh-hant/regional-terms.json, the "hk" of each
                 rule) and Hong Kong characters (config/zh-hant/hk-characters.json); our own
                 rules (group srw64) stay
  names.json     one row per Japanese name of content/translation/zh-Hant/names.json:
                   srwz     Super Robot Wars Z's Hong Kong name (config/zh-hant/regional-names.json)
                   derived  our Taiwan name with the Hong Kong names and works in it
                            (アムロ in アムロ・レイ, ガンダム in Zガンダム), in Hong Kong characters
                   tw       our Taiwan name in Hong Kong characters, nothing found
                 Rows researched or decided since (any other basis) are kept as they are.

    PYTHONPATH=src:tools .venv/bin/python tools/translation/import_srwz_hk.py --srwz PATH/TO/srwz-zh
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import date
from pathlib import Path

from srw64_native.zh_hant import HELD, HOLD, NAME_SECTIONS, SYSTEM_SECTIONS, TERMS, characters
from srw64_native.zh_hant_hk import NAMES, REGIONAL

ROOT = Path(__file__).resolve().parents[2]
FILES = ("config/zh-hant/regional-terms.json", "config/zh-hant/hk-characters.json", "config/zh-hant/regional-names.json")
OURS = {"srwz", "derived", "tw"}


def dump(document) -> str:
    return json.dumps(document, ensure_ascii=False, indent=1) + "\n"


def regional(srwz: Path, source: dict, previous: dict) -> dict:
    terms = json.loads((srwz / FILES[0]).read_text())
    chars = json.loads((srwz / FILES[1]).read_text())
    rules = []
    for rule in terms["rules"]:
        row = {"group": rule["group"], "sc": rule["sc"], "tw": rule["tw"], "hk": rule["hk"]}
        for key in ("jp", "jp_not", "note"):
            if key in rule:
                row[key] = rule[key]
        rules.append(row)
    # Ours (group srw64): words Super Robot Wars Z's text never had (界面 介面, 手柄 手掣).
    rules += [rule for rule in previous.get("terms", []) if rule.get("group") == "srw64"]
    return {"schema": "srw64.zh-hant-hk-regional.v1",
            "about": "Hong Kong words and characters for zh-Hant-HK (src/srw64_native/zh_hant_hk.py), from Super Robot Wars Z "
                     "(tools/translation/import_srwz_hk.py). terms: a mainland word (sc) becomes hk where the record's Japanese has "
                     "one of jp and none of jp_not; tw is Taiwan's, for reference. Group srw64 rules are ours, kept on import. characters: Taiwan form -> Hong Kong form.",
            "source": source, "sources": terms.get("sources", {}), "terms": rules,
            "characters": chars["map"], "left_out": chars.get("left_out", {})}


def names(srwz: Path, source: dict, table: dict, previous: dict) -> dict:
    taiwan = json.loads((ROOT / "content/translation/zh-Hant/names.json").read_text())
    sections = json.loads((ROOT / TERMS).read_text())["sections"]
    hans_of: dict[str, str] = {}
    for section in NAME_SECTIONS + SYSTEM_SECTIONS:
        for ja, hans in sections.get(section, {}).items():
            hans_of.setdefault(ja.strip(), hans.strip())
    by_ja = {ja: row for row in json.loads((srwz / FILES[2]).read_text())["names"] for ja in row["ja"]}
    hk_map = str.maketrans(table["characters"])
    hk_chars = lambda text: characters(text).translate(hk_map)
    tw_of = {row["ja"]: row["zh_hant"] or characters(hans_of.get(row["ja"], "")) for row in taiwan["entries"]}
    found = {ja: by_ja[ja]["hk"]["to"] for ja in tw_of if ja in by_ja}
    # Rows researched or decided since stand as they are, and name the rows built on them too.
    found.update({ja: row["zh_hant_hk"] for ja, row in previous.items() if row.get("basis") not in OURS and ja in tw_of})
    # The Hong Kong names inside a longer Taiwan name, longest first.
    parts = sorted(((ja, tw_of[ja], hk) for ja, hk in found.items() if len(ja) > 1 and tw_of[ja] and tw_of[ja] != hk),
                   key=lambda r: (-len(r[1]), r[1]))
    people = {ja.strip() for section in ("pilots", "pilot_full_names", "character_list", "default_names")
              for ja in sections.get(section, {})}
    rules = sorted(table["terms"], key=lambda r: (-len(r["sc"]), r["sc"], not r.get("jp")))
    entries = []
    for row in taiwan["entries"]:
        ja, tw = row["ja"], tw_of[row["ja"]]
        keep = previous.get(ja)
        if keep and keep.get("basis") not in OURS:
            entries.append(keep)
            continue
        out = {"ja": ja, "zh_hant": tw, "group": row.get("group")}
        if row.get("spellings"):
            out["spellings"] = row["spellings"]
        # A row looked into that kept its name keeps what was found (checked, alt, note).
        for key in ("checked", "alt", "note"):
            if keep and key in keep:
                out[key] = keep[key]
        if ja in found:
            out.update(zh_hant_hk=found[ja], basis="srwz", source=by_ja[ja]["id"])
        else:
            held: list[str] = []
            text, hans = tw, hans_of.get(ja, "")
            for part_ja, part_tw, hk in parts:
                # A person's name only as a whole part of a full name (アムロ in アムロ・レイ, not
                # アラン in バイアラン); a robot's name (ガンダム in Zガンダム) anywhere.
                whole = part_ja in ja.split("・") or (len(part_ja) >= 4 and part_ja in ja and part_ja not in people)
                if whole and part_tw in text:
                    held.append(hk)
                    text = text.replace(part_tw, chr(HOLD + len(held) - 1))
            for rule in rules:
                if rule.get("tw") and rule["sc"] in hans and rule["tw"] in text and rule["tw"] != rule["hk"] and \
                        (rule.get("group") == "franchise" or "jp" not in rule or any(w in ja for w in rule["jp"])):
                    held.append(rule["hk"])
                    text = text.replace(rule["tw"], chr(HOLD + len(held) - 1))
            # Taiwan writes 砲 for the mainland's 炮 (names.json words); Hong Kong keeps 炮.
            if "炮" in hans:
                text = text.replace("砲", "炮")
            hk = HELD.sub(lambda m: held[ord(m.group()) - HOLD], hk_chars(text))
            changed = held or hk != hk_chars(tw)
            out.update(zh_hant_hk=hk, basis="derived" if changed else "tw")
        entries.append(out)
    return {"schema": "srw64.zh-hant-hk-names.v1",
            "about": "Hong Kong names for zh-Hant-HK (src/srw64_native/zh_hant_hk.py), one row per Japanese name of "
                     "content/translation/zh-Hant/names.json; zh_hant is the Taiwan name for reference. basis: srwz = "
                     "Super Robot Wars Z's Hong Kong name (source = its row id); derived = the Taiwan name with the Hong Kong "
                     "names and works in it; tw = the Taiwan name in Hong Kong characters (nothing found yet); hk_dub, "
                     "hk_release, hk_common, official_tc = researched (source = where); decided = the user's choice. "
                     "checked: the date a srwz/derived/tw row was looked into and kept. "
                     "tools/translation/import_srwz_hk.py rewrites srwz, derived and tw rows and keeps the others.",
            "source": source, "entries": entries}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--srwz", type=Path, required=True, help="a checkout of the srwz-zh repository")
    args = parser.parse_args()
    commit = subprocess.run(["git", "-C", str(args.srwz), "rev-parse", "HEAD"], check=True, capture_output=True, text=True).stdout.strip()
    source = {"repository": "Super-Robot-Wars-Z/srwz-zh", "commit": commit, "date": date.today().isoformat(),
              "files": {name: hashlib.sha256((args.srwz / name).read_bytes()).hexdigest() for name in FILES}}
    kept = ROOT / REGIONAL
    table = regional(args.srwz, source, json.loads(kept.read_text()) if kept.exists() else {})
    path = ROOT / NAMES
    previous = {row["ja"]: row for row in json.loads(path.read_text())["entries"]} if path.exists() else {}
    document = names(args.srwz, source, table, previous)
    path.parent.mkdir(parents=True, exist_ok=True)
    (ROOT / REGIONAL).write_text(dump(table), encoding="utf-8")
    path.write_text(dump(document), encoding="utf-8")
    counts: dict[str, int] = {}
    for row in document["entries"]:
        counts[row["basis"]] = counts.get(row["basis"], 0) + 1
    print(f"{len(document['entries'])} names: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
