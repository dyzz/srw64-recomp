"""Per-section term tables expanded into ordinary checked locale entries.

Names, labels and system messages repeat the same Japanese string under many
TextKeys (one weapon name per unit that carries it, a pilot's name once per
form). A term table translates each distinct original string of a section once;
expansion turns it into entries with source hashes, so the runtime and the C++
importer only ever read the usual locale entries."""
from __future__ import annotations

import json
import re
from pathlib import Path

from .catalog import signature, text_key
from .weapon_traits import split_menu

SECTIONS = "content/locales/terms/sections.json"
ORIGIN = "terms"
# Kana, CJK ideographs and full-width forms: a string without any is already
# readable in every locale (型番, HP, V-MAX, %), so complete sections skip it.
JAPANESE = re.compile(r"[぀-ヿ㐀-鿿！-ﾟ]")


def load_sections(root: Path) -> list[dict]:
    document = json.loads((root / SECTIONS).read_text())
    if document.get("schema") != "srw64.term-sections.v1":
        raise ValueError("Unsupported term sections schema")
    sections, owner = document["sections"], {}
    for section in sections:
        name = section["name"]
        if any(s["name"] == name for s in sections[:sections.index(section)]):
            raise ValueError(f"Duplicate term section: {name}")
        for first, last in section["ranges"]:
            if type(first) is not int or type(last) is not int or not 0 <= first <= last < 65536:
                raise ValueError(f"Invalid range in term section {name}")
            for index in range(first, last + 1):
                if index in owner:
                    raise ValueError(f"Record {index} is in both {owner[index]} and {name}")
                owner[index] = name
        derive = section.get("derive")
        if derive and not any(s["name"] == derive["from"] for s in sections[:sections.index(section)]):
            raise ValueError(f"Term section {name} derives from an unknown or later section")
    return sections


def section_keys(section: dict) -> list[str]:
    return [text_key(0, index) for first, last in section["ranges"] for index in range(first, last + 1)]


def body(source: str) -> str:
    if not source.endswith("<END>"):
        raise ValueError("Message must end with <END>")
    return source[:-5]


def expand(sections: list[dict], terms: dict, sources: dict) -> dict[str, str]:
    """TextKey -> target for every record a term covers; raises on unusable terms."""
    if terms.get("schema") != "srw64.terms.v1":
        raise ValueError("Unsupported term table schema")
    tables = terms["sections"]
    unknown = set(tables) - {s["name"] for s in sections if "derive" not in s}
    if unknown:
        raise ValueError(f"Terms for unknown or derived sections: {sorted(unknown)}")
    targets: dict[str, str] = {}
    for section in sections:
        name = section["name"]
        derive = section.get("derive")
        for key in section_keys(section):
            if key not in sources:
                continue
            if derive:
                target = derived(key, derive, tables.get(derive["from"], {}), sources)
            else:
                target = tables.get(name, {}).get(body(sources[key]))
                target = None if target is None else target + "<END>"
            if target is None:
                continue
            if not target[:-5].strip() or signature(target) != signature(sources[key]):
                raise ValueError(f"Term for {key} ({name}) changes glyph parameters or is empty")
            targets[key] = target
    return targets


def derived(key: str, derive: dict, table: dict, sources: dict) -> str | None:
    """weapon_menus: 格／射 + translated name + P／B／MAP, following the menu grammar."""
    index = int(key.split("_")[1])
    base_key = text_key(0, index + derive["offset"])
    name, menu = body(sources[base_key]), body(sources[key])
    split = split_menu(name, menu)
    if split is None or name not in table:
        return None
    prefix, _, suffix, map_moved = split
    translated = table[name]
    if map_moved:
        if not translated.endswith("MAP"):
            raise ValueError(f"Translation of {name} must keep the trailing MAP")
        translated = translated[:-3]
    return prefix + translated + suffix + "<END>"


def unused(sections: list[dict], terms: dict, sources: dict) -> list[tuple[str, str]]:
    """(section, source) pairs whose source string never occurs in that section."""
    result = []
    for section in sections:
        present = {body(sources[k]) for k in section_keys(section) if k in sources}
        result += [(section["name"], s) for s in terms["sections"].get(section["name"], {}) if s not in present]
    return result


def missing(sections: list[dict], terms: dict, sources: dict) -> list[tuple[str, str]]:
    """(section, source) pairs a complete section still leaves in Japanese."""
    result = []
    for section in sections:
        if not section.get("complete"):
            continue
        table = terms["sections"].get(section["name"], {})
        seen = set()
        for key in section_keys(section):
            text = body(sources[key]) if key in sources else ""
            if text not in seen and JAPANESE.search(text) and text not in table:
                result.append((section["name"], text))
            seen.add(text)
    return result


def merge(document: dict, targets: dict[str, str], hashes: dict) -> dict:
    """Replace the locale's term entries; hand-written entries must not overlap them."""
    hand = [row for row in document["entries"] if row.get("origin") != ORIGIN]
    overlap = sorted({row["key"] for row in hand} & targets.keys())
    if overlap:
        raise ValueError(f"Hand-written entries shadow term entries: {overlap[:5]}")
    generated = [{"key": key, "source_sha256": hashes[key], "target": target,
                  "review_status": "draft", "origin": ORIGIN} for key, target in targets.items()]
    return {**document, "entries": sorted(hand + generated, key=lambda row: row["key"])}
