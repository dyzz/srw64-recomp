#!/usr/bin/env python3
"""Export each stage's metadata, for the in-game stage info page, from the guide's data.

The guide (guide/data/<lang>/) is the one place this text is written. The game's
stage info page shows one stage at a time: its hidden elements and one table of the
enemies worth knowing about, the parts they drop and the retreats that lose them
(docs/design/stage-info.md). This turns each guide card into that shape, keyed by
scene number, for each language:

- the guide's comparison badges ({≠} {+} {?}) are dropped;
- a leading "Brad's scenario only" / "Real:" qualifier becomes a protagonist list
  and an "OZ route only" one a route list, so the page can show only what the
  player's game can meet; "Third party" becomes a flag;
- the parts and the retreat-only rows become one list, and a row whose note says
  the unit leaves the map is marked as a retreat;
- a secret's text is its one-line `short` and a note its `short_note` where the
  guide has them (the qualifiers still come from the full text).

The qualifiers are read from the Chinese text (the reference language) and the
same segment is cut from every translation, which must say exactly the phrase
listed below, so a reworded translation fails here instead of showing twice.

The result is one metadata file per stage, content/stages/scene-NNN.json
(srw64.stage-meta.v1): its title and, per route that plays it (two for the side-branch
stages 123 and 124), the episode, the lane, the hidden elements and the enemies, each
text an object by language. A custom campaign's stages carry the same format
(docs/design/custom-campaign.md). content/stages/status.json carries the status rules
(guide/data/status-rules.json) with the scene graph they are judged on
(tools/content/stage_status.py), for the whole story.

    python3 tools/content/export_stage_info.py          # writes content/stages/*.json
    python3 tools/content/export_stage_info.py --check  # fails when a file is stale
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_status  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
GUIDE = ROOT / "guide" / "data"
OUTPUT = ROOT / "content" / "stages"
LANGS = ("zh-Hans", "ja", "en")
SCHEMA = "srw64.stage-info.v1"     # one language's view, the step before the metadata
META_SCHEMA = "srw64.stage-meta.v1"
STATUS_SCHEMA = "srw64.stage-status.v1"
PROTAGONISTS = ("ark", "serein", "brad", "manami")
BADGE = re.compile(r"\s*\{[≠+?]\}")
# How each language ends a note's segment (the guide writes 「A；B」 / "A; B" / 「A。B」 or 「A；B」).
SEGMENT = {"zh-Hans": "；", "en": "; ", "ja": "[。；]"}
PREFIX = {"zh-Hans": "：", "en": ": ", "ja": "："}

# Leading note segments → (field, values), with each translation's wording.
NOTE_QUALIFIERS = {
    "仅阿克篇": ("protagonists", ["ark"], "Ark's scenario only", "アーク編のみ"),
    "仅塞蕾茵篇": ("protagonists", ["serein"], "Selain's scenario only", "セレイン編のみ"),
    "仅布拉德篇": ("protagonists", ["brad"], "Brad's scenario only", "ブラッド編のみ"),
    "仅玛娜米篇": ("protagonists", ["manami"], "Manami's scenario only", "マナミ編のみ"),
    "仅阿克篇、塞蕾茵篇": ("protagonists", ["ark", "serein"], "Ark's and Selain's scenarios only", "アーク編、セレイン編のみ"),
    "仅布拉德篇、玛娜米篇": ("protagonists", ["brad", "manami"], "Brad's and Manami's scenarios only", "ブラッド編、マナミ編のみ"),
    "仅阿克篇、塞蕾茵篇、布拉德篇": ("protagonists", ["ark", "serein", "brad"],
                         "Ark's, Selain's and Brad's scenarios only", "アーク編、セレイン編、ブラッド編のみ"),
    "仅独立军路线": ("routes", ["ia"], "Independence Army route only", "独立軍ルートのみ"),
    "仅 OZ 路线": ("routes", ["oz"], "OZ route only", "OZルートのみ"),
    "完全和平路线或 OZ 路线": ("routes", ["cp", "oz"], "Complete Pacifism route or OZ route", "完全平和ルートまたはOZルート"),
    "第三方": ("third_party", True, "Third party", "第三勢力"),
}
# Leading 「X：」 of a card's secret text → protagonists, with each translation's prefix.
SECRET_QUALIFIERS = {
    "阿克篇": (["ark"], "Ark's scenario", "アーク編"),
    "塞蕾茵篇": (["serein"], "Selain's scenario", "セレイン編"),
    "布拉德篇": (["brad"], "Brad's scenario", "ブラッド編"),
    "玛娜米篇": (["manami"], "Manami's scenario", "マナミ編"),
    "真实系": (["ark", "serein"], "Real", "リアル系"),
    "超级系": (["brad", "manami"], "Super", "スーパー系"),
}
# A hidden element whose kicker names one protagonist belongs to that scenario.
KICKER = {"阿克专属": "ark", "塞蕾茵专属": "serein", "布拉德专属": "brad", "玛娜米专属": "manami"}
RETREAT = re.compile(r"撤退|离场|离开")


def load(lang: str, name: str) -> dict:
    return json.loads((GUIDE / lang / name).read_text(encoding="utf-8"))


def clean(text: str) -> str:
    return BADGE.sub("", text).strip()


def wording(phrases: tuple, lang: str) -> str:
    return {"en": phrases[0], "ja": phrases[1]}[lang]


def split_note(zh: str, note: str, lang: str, row: dict, where: str) -> str:
    """Move the qualifier segments of NOTE into ROW's fields; return the rest. The
    scenario and route qualifiers lead a note; "third party" may stand anywhere."""
    zh_parts = zh.split("；")
    if not any(p in NOTE_QUALIFIERS for p in zh_parts):
        return note
    tokens = re.split(f"({SEGMENT[lang]})", note)
    parts, delims = tokens[0::2], tokens[1::2] + [""]
    if len(parts) != len(zh_parts):
        raise ValueError(f"{lang} {where}: {note!r} has not the segments of {zh!r}")
    keep, leading = [], True
    for zh_part, part, delim in zip(zh_parts, parts, delims):
        known = NOTE_QUALIFIERS.get(zh_part)
        if not known or not (leading or known[0] == "third_party"):
            leading = False
            keep.append((part, delim))
            continue
        field, value, *phrases = known
        # English capitalises only a note's first segment.
        if lang != "zh-Hans" and part.lower() != wording(tuple(phrases), lang).lower():
            raise ValueError(f"{lang} {where}: expected {wording(tuple(phrases), lang)!r} in {note!r}")
        row[field] = value
    rest = "".join(part + (delim if i < len(keep) - 1 else "") for i, (part, delim) in enumerate(keep))
    if lang == "en" and rest:
        rest = rest[0].upper() + rest[1:]
    return rest


def secret_text(zh: str, text: str, lang: str, row: dict, where: str) -> str:
    head, sep, rest = zh.partition(PREFIX["zh-Hans"])
    if not sep or head not in SECRET_QUALIFIERS:
        return text
    # A text with a part for another scenario too (「阿克篇：…；布拉德篇：…」) is everyone's.
    if any(f"{other}{PREFIX['zh-Hans']}" in rest for other in SECRET_QUALIFIERS if other != head):
        return text
    protagonists, *phrases = SECRET_QUALIFIERS[head]
    prefix = (head if lang == "zh-Hans" else wording(tuple(phrases), lang)) + PREFIX[lang]
    if not text.startswith(prefix):
        raise ValueError(f"{lang} {where}: expected the text to start with {prefix!r}")
    row["protagonists"] = protagonists
    rest = text[len(prefix):]
    return rest[0].upper() + rest[1:] if lang == "en" and rest else rest


def export(lang: str) -> dict:
    cards, base = load(lang, "progression.json")["cards"], load("zh-Hans", "progression.json")["cards"]
    entries = {e["id"]: e for e in load(lang, "hidden-elements.json")["entries"]}
    base_entries = {e["id"]: e for e in load("zh-Hans", "hidden-elements.json")["entries"]}
    scenes: dict[str, list] = {}
    for card, zh in zip(cards, base):
        if card["id"] != zh["id"]:
            raise ValueError(f"{lang}: card order differs from zh-Hans at {card['id']}")
        secrets = []
        for s, s_zh in zip(card.get("secrets") or [], zh.get("secrets") or []):
            row = {"ref": s["ref"], "title": clean(entries[s["ref"]]["title"])}
            kicker = base_entries[s["ref"]].get("kicker", "")
            owner = [p for k, p in KICKER.items() if k in kicker]
            if owner:
                row["protagonists"] = owner
            text = clean(secret_text(clean(s_zh["text"]), clean(s["text"]), lang, row, f'{card["id"]} {s["ref"]}'))
            # The page shows the guide's one-line version where it has one (`short`).
            row["text"] = clean(s["short"]) if s.get("short") else text
            secrets.append(row)
        enemies = []
        for key in ("parts", "retreats"):
            for p, p_zh in zip(card.get(key) or [], zh.get(key) or []):
                row = {"part": clean(p["part"]) if key == "parts" else None, "from": clean(p["from"])}
                note = split_note(clean(p_zh.get("note", "")), clean(p.get("note", "")), lang, row, f'{card["id"]} {key}')
                if p.get("short_note"):
                    note = clean(p["short_note"])
                if note:
                    row["note"] = note
                row["retreat"] = key == "retreats" or bool(RETREAT.search(p_zh.get("note", "")))
                enemies.append(row)
        scenes.setdefault(str(card["scene"]), []).append({
            "card": card["id"], "route": card["route"], "lane": clean(card["lane"]), "episode": card["stage"],
            "title": clean(card["title"]), "title_ja": zh["title_ja"], "secrets": secrets, "enemies": enemies})
    ordered = {k: scenes[k] for k in sorted(scenes, key=int)}
    return {"schema": SCHEMA, "locale": lang, "source": "guide/data (tools/content/export_stage_info.py)",
            "protagonists": list(PROTAGONISTS), "scenes": ordered}


def stage_meta() -> dict[int, dict]:
    """The three languages' views joined into one metadata document per scene: every
    text becomes {language: text}; the rest is the same in all three (tests check)."""
    views = {lang: export(lang)["scenes"] for lang in LANGS}
    by = lambda key: lambda rows: {lang: rows[lang][key] for lang in LANGS}
    meta = {}
    for scene, cards in views["zh-Hans"].items():
        out = []
        for i, card in enumerate(cards):
            each = {lang: views[lang][scene][i] for lang in LANGS}
            secrets = []
            for j, row in enumerate(card["secrets"]):
                rows = {lang: each[lang]["secrets"][j] for lang in LANGS}
                item = {"ref": row["ref"], "title": by("title")(rows), "text": by("text")(rows)}
                if "protagonists" in row:
                    item["protagonists"] = row["protagonists"]
                secrets.append(item)
            enemies = []
            for j, row in enumerate(card["enemies"]):
                rows = {lang: each[lang]["enemies"][j] for lang in LANGS}
                item = {"part": by("part")(rows) if row["part"] is not None else None, "from": by("from")(rows)}
                if "note" in row:
                    item["note"] = by("note")(rows)
                for key in ("protagonists", "routes", "third_party"):
                    if key in row:
                        item[key] = row[key]
                item["retreat"] = row["retreat"]
                enemies.append(item)
            out.append({"guide_card": card["card"], "route": card["route"], "episode": card["episode"],
                        "lane": by("lane")(each), "secrets": secrets, "enemies": enemies})
        title = {lang: views[lang][scene][0]["title"] for lang in LANGS}
        meta[int(scene)] = {"schema": META_SCHEMA, "scene": int(scene), "title": title,
                            "title_ja": cards[0]["title_ja"], "cards": out}
    return meta


def outputs() -> dict[Path, str]:
    files = {OUTPUT / f"scene-{scene:03d}.json": json.dumps(doc, ensure_ascii=False, indent=1) + "\n"
             for scene, doc in stage_meta().items()}
    files[OUTPUT / "status.json"] = json.dumps(status(), ensure_ascii=False, indent=1) + "\n"
    return files


def status() -> dict:
    """The status rules with the scene graph, after checking they fit the guide."""
    rules, graph = stage_status.load_rules(), stage_status.scene_graph()
    entries = [e["id"] for e in load("zh-Hans", "hidden-elements.json")["entries"]]
    errors = stage_status.check_rules(rules["rules"], entries, graph)
    if errors:
        raise ValueError("; ".join(errors))
    return {"schema": STATUS_SCHEMA, "source": "guide/data/status-rules.json (tools/content/export_stage_info.py)",
            "starts": stage_status.STARTS,
            "graph": {p: {str(s): t for s, t in g.items()} for p, g in graph.items()},
            "edge_guards": rules["edge_guards"], "rules": rules["rules"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when a file differs from the guide data")
    args = parser.parse_args()
    stale = 0
    files = outputs()
    # A stage that lost its card leaves no file behind.
    for old in OUTPUT.glob("scene-*.json"):
        if old not in files:
            if args.check:
                print(f"{old.relative_to(ROOT)} is left over; run tools/content/export_stage_info.py", file=sys.stderr)
                stale = 1
            else:
                old.unlink()
    for path, text in files.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                print(f"{path.relative_to(ROOT)} is stale; run tools/content/export_stage_info.py", file=sys.stderr)
                stale = 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)} ({len(text.encode()) // 1024} KiB)")
    return stale


if __name__ == "__main__":
    sys.exit(main())
