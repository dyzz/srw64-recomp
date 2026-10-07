#!/usr/bin/env python3
"""Export the in-game Library (図鑑) to JSON for the website's Library pages.

Reproduces ``src/host/library.cpp`` (docs/native/library.md) from the player's ROM:
the same ROM tables, the same placeholder / duplicate rules, the same grouping by
work (作品) and the same "(2)" numbering, built once per language the way the game
builds it for its reading language, then merged by record id.

    .venv/bin/python web/scripts/export_library.py [--rom rom.z64] [--out web/.data/library.json] [--check]

``--check`` asserts known in-game facts (counts, the RX-78-2 page, シロー's page)
and exits 1 on any mismatch.

Output (``web/.data/library.json``, schema ``srw64.web-library.v1``). Every
localized string is ``{"ja", "zh", "en"}``; Japanese is the ROM text, zh/en come
from ``content/locales/<zh-Hans|en>.json`` (checked against the ROM like the game
does) and fall back to the ROM text where a record has no translation.

- ``schema``, ``generated_from`` (ROM sha256), ``counts`` {units, people}
- ``series``: [{id, name}] -- the 25 works in the order the unit list first shows
  them (then any only the character list shows), and ``{"id": null}`` = その他.
  ``unit_series_order`` / ``people_series_order``: each list's own group order.
- ``labels``: {key: localized} -- the page's stat, weapon and ``library_*`` labels.
- ``upgrade_types``: [{type 1-4, power[15], price[15]}] per-step increments / funds.
- ``units`` (in-game list order):
  id, series (int|null), model (str|null), name, upgrade_cap, hp, en, move,
  mobility, armor, limit, size (SS..LL), movement_types [localized],
  terrain {air,land,sea,space} ('A'..'D' or '-'), abilities [localized],
  shield (bool), part_slots, repair_cost, weapons [...], upgrade_totals
  [{type, power, funds}] (sum of the type's steps up to the cap, for each type the
  unit's weapons use), image {pose:[scene,atlas,palette], hd: path|null} | null.
  weapon: id, name (without markers), markers (any of "格" "射" "P" "B" "MAP"),
  power, max_power (power at the unit's cap; null without an upgrade type),
  upgrade_type (1-4|null), range [min,max], hit, crit, ammo|null, en|null,
  morale|null, required_skill {id, name}|null (weapon +8 when >= 2, named by text
  record +8 as library.cpp does; those records are terrain names -- 池, 湖, 海, 川...
  -- so it is really a terrain the weapon needs; the in-game page does not show it),
  terrain {...}, unlock_at_full_upgrade {weapon, name}|null (the
  weapon whose full upgrade unlocks this one), combination (bool).
- ``people`` (in-game list order):
  id (actor), series, name, full_name, enemy (bool|null: the original character
  list's flag, null when not listed), role ("sub_pilot"|"fairy"|null; the game
  prints such a pilot's six stats and two-action level as "–"), no_battle (bool:
  no ability record; only portrait and names are shown),
  stats {lv1:{melee,shooting,evade,hit,reaction,skill,sp}, lv99:{...}} | null,
  terrain {...}|null, double_move_level (int|null; null for sub-pilots/fairies
  or 0), spirits [{level, id, name, cost}], skills [{name, levels:[{level, rank,
  label}]}] (level = pilot level reaching rank L; same-level steps collapsed to
  the highest), love [{partner_id, name, mutual, portrait}],
  portrait {image, palette, hd: path|null, silhouette_rgb: [r,g,b]|null}.

Image paths are repository-relative source PNGs under ``assets/`` (the HD pack
named by ``content/art/stage1-hd.json``); the website build converts them.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "src")]

from srw64_native.catalog import compile_locale, source_catalog  # noqa: E402
from srw64_native.battle_graphics import read_triplets  # noqa: E402

LANGS = {"ja": "ja", "zh": "zh-Hans", "en": "en"}

# --- ROM layout (library.cpp) ---------------------------------------------------
def resident(vram): return vram - 0x80075610
def overlay(vram): return vram - 0x801C4500 + 0x8F4B0
def title_overlay(vram): return vram - 0x801C4500 + 0x10DA50

UNITS_AT, UNIT_SIZE, UNIT_COUNT = 0x71B80, 0x24, 363
WEAPON_LISTS, WEAPON_ENTRY = 0x7E210, 12
WEAPONS_AT, WEAPON_SIZE, WEAPON_COUNT = 0x74E90, 0x10, 1329
STATS_MAP, SPIRITS_MAP = resident(0x800CA9C4), resident(0x800CA6F4)
SPIRIT_COSTS = 0x100AD0
PILOT_STATS_AT, PILOT_STATS_SIZE = 0x7A1A0, 0x10
THRESHOLDS_AT, THRESHOLDS_SIZE = 0x7B1B0, 30
SPIRITS_AT, SPIRITS_SIZE = 0x7CFB0, 12
ACTOR_COUNT, MAPPED_ACTORS = 361, 360
MOVE_ICONS, ABILITY_TABLE, ABILITY_ENTRIES = overlay(0x801DC8F0), overlay(0x801DC8FC), 16
T_UNIT, T_WEAPON_PURE, T_WEAPON_MENU, T_PILOT, T_PILOT_FULL, T_SPIRITS = 527, 1370, 2699, 4382, 4743, 969
T_SIZES, T_SHIELD_YES, T_WEAPON_LABELS = 0x446, 0x38F, 0xFF1
FIRST_PERSON, PEOPLE, T_PEOPLE, T_PEOPLE_FULL = 25, 8, 487, 495
CHARACTER_LIST, ROBOT_LIST = title_overlay(0x801CB3A0), title_overlay(0x801CB964)
ACTOR_ALIASES, UNIT_ALIASES = resident(0x800C6A08), resident(0x800C6A8C)
CHARACTER_ROWS, ROBOT_ROWS, ACTOR_ALIAS_COUNT, UNIT_ALIAS_COUNT, WORK_COUNT = 246, 316, 33, 22, 25
T_WORKS, T_MODELS = 60, 110
UNIT_WORKS = [(5, 8), (29, 8), (361, 8), (362, 8), (211, 24), (214, 24), (267, 10), (347, 20), (348, 20), (349, 20), (350, 20),
              (354, 13), (355, 13), (356, 13), (360, 13), (357, 14), (358, 14), (359, 14), (177, 13), (178, 13), (179, 13)]
ACTOR_WORKS = [(19, 8), (159, 24), (211, 22), (288, 9), (289, 9), (305, 9), (306, 9), (307, 9), (308, 9), (290, 9), (355, 9),
               (291, 20), (292, 4), (309, 4), (310, 4), (314, 4), (293, 5), (311, 5), (312, 5), (294, 23), (295, 12), (296, 22),
               (297, 22), (298, 21), (299, 8)]
WEAPON_INCREMENTS, UNLOCK_TABLE, UPGRADE_LEVELS = resident(0x800CA590), overlay(0x801DC87C), 15
WEAPON_PRICES = [overlay(0x801DC7BC), overlay(0x801DC77C), overlay(0x801DC73C), overlay(0x801DC6FC)]
LOVE_TABLE, LOVE_ROWS = 0x1012B4, 47
SKILLS = [(0x04, 0x433, 0), (0x08, 0x40F, 0), (0x10, 0x418, 0), (0x20, 0x421, 0), (0x40, 0x42A, 0), (0x01, 0x43C, 1), (0x02, 0x406, 2)]
LABEL_TEXTS = {"hp": 0xFE7, "en": 0xFE8, "mobility": 0xFE9, "armor": 0xFEA, "limit": 0xFEB, "size": 0x1003, "repair": 0x1004,
               "abilities": 0x1005, "type": 0x1006, "move": 0x1007, "terrain": 0xFF6, "air": 0xFF7, "land": 0xFF8, "sea": 0xFF9,
               "space": 0xFFA, "sp": 0x100B, "melee": 0x100C, "evade": 0x100D, "reaction": 0x100E, "shooting": 0x1023,
               "hit": 0xFF4, "skill": 0x100F, "spirits": 0x1010, "skills": 0x1011, "level": 0xFE1}
WEAPON_LABEL_KEYS = ["weapon", "power", "range", "hit", "ammo", "terrain", "air", "land", "sea", "space", "morale", "en", "skill", "critical"]
TERRAIN_KEYS = ("air", "land", "sea", "space")
MARKERS_PREFIX = ["格", "射", ""]
MARKERS_SUFFIX = [("", []), ("P", ["P"]), ("B", ["B"]), ("PB", ["P", "B"]), ("MAP", ["MAP"]), ("BMAP", ["B", "MAP"]), ("PBMAP", ["P", "B", "MAP"])]

TOKEN = re.compile(r"<([^>]*)>")


class Rom:
    def __init__(self, data: bytes):
        self.d = data

    def u8(self, at): return self.d[at] if at < len(self.d) else 0
    def s8(self, at): v = self.u8(at); return v - 256 if v >= 128 else v
    def u16(self, at): return self.u8(at) << 8 | self.u8(at + 1)
    def s16(self, at): v = self.u16(at); return v - 65536 if v >= 32768 else v
    def u32(self, at): return self.u16(at) << 16 | self.u16(at + 2)
    def bytes(self, at, size): return self.d[at:at + size] if at + size <= len(self.d) else b""


class Texts:
    """dialogue::ui_text for one catalog: the translation, else the ROM record, expanded."""

    def __init__(self, sources, glyphs, translated):
        self.sources, self.glyphs, self.translated = sources, glyphs, translated
        self.warnings = set()

    def __call__(self, tid: int) -> str:
        key = f"base:t00_{tid:05d}"
        value = self.translated.get(key, self.sources.get(key))
        if value is None:
            return str(tid)
        out = []
        pos = 0
        for m in TOKEN.finditer(value):
            out.append(value[pos:m.start()])
            pos = m.end()
            token = m.group(1)
            if token == "END":
                return "".join(out)
            if token == "BR":
                out.append("\n")
            elif token == "STOP":
                out.append("\f")
            elif token.startswith("G:"):
                code = int(token[2:], 16)
                if 0x124 <= code <= 0x12C:
                    self.warnings.add(f"text {tid} uses a name field")
                else:
                    out.append(self.glyphs.get(str(code), f"〔{code:04X}〕"))
        out.append(value[pos:])
        return "".join(out)


def placeholder(name: str) -> bool:
    return not name or all(c in "? 0123456789" for c in name)


def terrain(rom: Rom, at: int) -> dict:
    letters = {1: "D", 2: "C", 3: "B", 4: "A"}
    return {k: letters.get(rom.u8(at + n), "-") for n, k in enumerate(TERRAIN_KEYS)}


def weapon_markers(menu: str, pure: str):
    """upgrade_page::weapon_markers: split the menu name's 格／射／P／B／MAP icons off."""
    candidates = [(pure, False)]
    if len(pure.encode()) > 3 and pure.endswith("MAP"):
        candidates.insert(0, (pure[:-3], True))
    for display, requires_map in candidates:
        for prefix in MARKERS_PREFIX:
            for suffix, tokens in MARKERS_SUFFIX:
                if requires_map and "MAP" not in tokens:
                    continue
                if display and menu == prefix + display + suffix:
                    return display, ([prefix] if prefix else []) + tokens
    return menu, []


# --- Language-independent structure ---------------------------------------------
def weapon_numbers(rom: Rom, unit: int) -> list[int]:
    numbers = []
    at = WEAPON_LISTS + rom.u32(WEAPON_LISTS + unit * 4)
    while at + WEAPON_ENTRY <= len(rom.d):
        number = rom.u16(at)
        if number == 0xFFFF:
            break
        if any(rom.u16(at + n * 2) == unit for n in range(1, 6)) and number < WEAPON_COUNT:
            numbers.append(number)
        at += WEAPON_ENTRY
    return numbers


def unlock_rows(rom: Rom):
    rows, row = [], UNLOCK_TABLE
    while rom.s16(row) != 999 and row < len(rom.d):
        rows.append((rom.u16(row), rom.u16(row + 2), rom.u16(row + 4)))
        row += 6
    return rows


def increments(rom: Rom, wtype: int) -> list[int]:
    return [rom.u16(WEAPON_INCREMENTS + (wtype * UPGRADE_LEVELS + n) * 2) for n in range(UPGRADE_LEVELS)]


def prices(rom: Rom, wtype: int) -> list[int]:
    return [rom.u32(WEAPON_PRICES[wtype - 1] + n * 4) for n in range(UPGRADE_LEVELS)]


def build_language(rom: Rom, text: Texts):
    """library.cpp build() for one reading language: ids, names and works in page order."""
    units, kept = [], set()
    for uid in range(UNIT_COUNT):
        numbers = weapon_numbers(rom, uid)
        name = text(T_UNIT + uid)
        if placeholder(name):
            continue
        key = (name, rom.bytes(UNITS_AT + uid * UNIT_SIZE, UNIT_SIZE), tuple(numbers))
        if key not in kept:
            kept.add(key)
            units.append({"id": uid, "name": name, "numbers": numbers})
    pilots, kept = [], set()
    for actor in range(ACTOR_COUNT):
        stats = rom.s16(STATS_MAP + actor * 2) if actor < MAPPED_ACTORS else -1
        spirits = rom.s16(SPIRITS_MAP + actor * 2) if actor < MAPPED_ACTORS else -1
        person = FIRST_PERSON <= actor < FIRST_PERSON + PEOPLE
        name = text(T_PEOPLE + actor - FIRST_PERSON if person else T_PILOT + actor)
        full = text(T_PEOPLE_FULL + actor - FIRST_PERSON if person else T_PILOT_FULL + actor)
        if placeholder(name):
            continue
        key = [name, full]
        if stats >= 0:
            key += [rom.bytes(PILOT_STATS_AT + stats * PILOT_STATS_SIZE, PILOT_STATS_SIZE),
                    rom.bytes(THRESHOLDS_AT + stats * THRESHOLDS_SIZE, THRESHOLDS_SIZE)]
        if spirits >= 0:
            key.append(rom.bytes(SPIRITS_AT + spirits * SPIRITS_SIZE, SPIRITS_SIZE))
        key = tuple(key)
        if key not in kept:
            kept.add(key)
            pilots.append({"id": actor, "name": name, "full_name": full, "stats": stats, "spirits": spirits})
    assign_works(rom, units, ROBOT_LIST, ROBOT_ROWS, True, UNIT_ALIASES, UNIT_ALIAS_COUNT, UNIT_WORKS)
    assign_works(rom, pilots, CHARACTER_LIST, CHARACTER_ROWS, False, ACTOR_ALIASES, ACTOR_ALIAS_COUNT, ACTOR_WORKS)
    number_repeats(units)
    number_repeats(pilots)
    return units, pilots


def assign_works(rom, entries, table, rows, models, aliases, alias_count, fixed):
    listed = {}
    for n in range(rows):
        at = table + n * 6
        listed.setdefault(rom.u16(at), {"work": rom.s16(at + 2), "model": rom.s16(at + 4) if models else -1,
                                        "place": n, "flag": -1 if models else rom.u16(at + 4)})
    alias = {}
    for n in range(alias_count):
        alias[rom.u16(aliases + n * 4)] = rom.u16(aliases + n * 4 + 2)
    by_name = {}
    for e in entries:
        if e["id"] in listed:
            by_name.setdefault(e["name"], listed[e["id"]])
    blank = {"work": -1, "model": -1, "place": 0xFFFFFFFF, "flag": -1}
    for e in entries:
        uid = e["id"]
        if uid in listed:
            work = dict(listed[uid])
        elif uid in alias and alias[uid] in listed:
            work = dict(listed[alias[uid]])
        elif e["name"] in by_name:
            work = dict(by_name[e["name"]])
        else:
            work = dict(blank)
        for fid, fwork in fixed:
            if fid == uid:
                work["work"] = fwork
        if not 0 <= work["work"] < WORK_COUNT:
            work["work"] = -1
        e["work"], e["place"], e["model"], e["flag"] = work["work"], work["place"], work["model"], work["flag"]
    rank = {}
    for n in range(rows):
        rank.setdefault(rom.s16(table + n * 6 + 2), n)
    def key(e):
        w = e["work"]
        return (0xFFFFFFFF if w < 0 else rank.get(w, rows), e["place"])
    entries.sort(key=key)   # stable, as std::stable_sort


def number_repeats(entries):
    total, seen = {}, {}
    for e in entries:
        total[(e["work"], e["name"])] = total.get((e["work"], e["name"]), 0) + 1
    for e in entries:
        k = (e["work"], e["name"])
        if total[k] > 1:
            seen[k] = seen.get(k, 0) + 1
            if seen[k] > 1:
                e["name"] = f"{e['name']} ({seen[k]})"


# --- Art ---------------------------------------------------------------------
def art_lookups(rom_bytes: bytes):
    pack = json.loads((ROOT / "content/art/stage1-hd.json").read_text())
    unit_dir = pack["units"]["path"]
    units_index = json.loads((ROOT / unit_dir / "units.json").read_text())
    hd_units = {(r["scene"], r["atlas"], r["palette"]): f"{unit_dir}/{r['file']}" for r in units_index["images"]}
    portrait_dir = pack["portraits"]["path"]
    portraits_index = json.loads((ROOT / portrait_dir / "portraits.json").read_text())
    hd_portraits = {r["image"]: r for r in portraits_index["images"]}
    silhouette = portraits_index["silhouette"]
    poses = read_triplets(rom_bytes, "unit_poses")
    spec = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())["images"]["actors"]

    def unit_image(uid):
        if uid >= len(poses) or not poses[uid][0]:
            return None
        triple = tuple(poses[uid])
        path = hd_units.get(triple)
        return {"pose": list(triple), "hd": path}

    def portrait(actor):
        if actor >= spec["count"]:
            return None
        image, palette = struct.unpack_from(">2H", rom_bytes, spec["rom_offset"] + actor * spec["stride"])
        row = hd_portraits.get(image)
        out = {"image": image, "palette": palette, "hd": None, "silhouette_rgb": None}
        if row and palette == row["palette"]:
            out["hd"] = f"{portrait_dir}/{row['file']}"
        elif row and palette == silhouette["palette"]:
            out["hd"] = f"{portrait_dir}/{row['file']}"
            out["silhouette_rgb"] = silhouette["rgb"]
        return out
    return unit_image, portrait


# --- Export ------------------------------------------------------------------
def locale_docs(rev: str | None = None) -> dict[str, dict]:
    """The term tables of each language: the working tree's, or as they were at REV."""
    docs = {}
    for short, locale in LANGS.items():
        path = f"content/locales/{locale}.json"
        text = (ROOT / path).read_text() if rev is None else subprocess.run(
            ["git", "-C", str(ROOT), "show", f"{rev}:{path}"], check=True, capture_output=True, text=True).stdout
        docs[short] = json.loads(text)
    return docs


# What kind of field a changed text is, by where it sits in an entry.
CHANGE_KINDS = {"name": "name", "full_name": "full_name", "weapons": "weapon", "abilities": "ability",
                "movement_types": "movement", "skills": "skill", "spirits": "spirit", "love": "love"}


def text_changes(new, old, path=()) -> list[dict]:
    """Every localized text ({ja, zh, en}) of NEW whose zh or en differs from OLD's."""
    if isinstance(new, dict) and isinstance(new.get("ja"), str) and "zh" in new and "en" in new:
        was = {lang: old.get(lang) for lang in ("zh", "en") if isinstance(old, dict) and old.get(lang) != new[lang]}
        return [{"path": path, "ja": new["ja"], "was": was, "now": {lang: new[lang] for lang in was}}] if was else []
    out = []
    if isinstance(new, dict) and isinstance(old, dict):
        for key in new:
            if key in old:
                out += text_changes(new[key], old[key], path + (key,))
    elif isinstance(new, list) and isinstance(old, list) and len(new) == len(old):
        for i, (a, b) in enumerate(zip(new, old)):
            out += text_changes(a, b, path + (i,))
    return out


def mark_changes(data: dict, before: dict, tag: str) -> None:
    """Each unit and person gets `changes` (kind, ja, was, now; one row per distinct text)
    and `changed` {zh, en} counts against the term tables of release TAG."""
    for kind in ("units", "people"):
        old = {e["id"]: e for e in before[kind]}
        for e in data[kind]:
            rows, seen = [], set()
            for c in text_changes(e, old.get(e["id"], {})):
                if c["path"][0] in ("image", "portrait"):
                    continue
                row = {"kind": CHANGE_KINDS.get(c["path"][0], "other"), "ja": c["ja"], "was": c["was"], "now": c["now"]}
                key = json.dumps(row, ensure_ascii=False, sort_keys=True)
                if key not in seen:
                    seen.add(key)
                    rows.append(row)
            if rows:
                e["changes"] = rows
            e["changed"] = {lang: sum(1 for r in rows if lang in r["was"]) for lang in ("zh", "en")}
    data["baseline"] = tag


def export(rom_path: Path, docs: dict[str, dict] | None = None) -> dict:
    rom_bytes = rom_path.read_bytes()
    rom = Rom(rom_bytes)
    sources, hashes, glyphs = source_catalog(ROOT, rom_path)
    texts = {}
    docs = docs or locale_docs()
    for short in LANGS:
        doc = docs[short]
        texts[short] = (Texts(sources, glyphs, compile_locale(doc, sources, hashes)), doc["ui"])
    def loc(tid):
        return {lang: texts[lang][0](tid) for lang in LANGS}

    built = {lang: build_language(rom, texts[lang][0]) for lang in LANGS}
    notes = []
    ja_units, ja_people = built["ja"]
    for lang in ("zh", "en"):
        u, p = built[lang]
        for what, a, b in (("units", ja_units, u), ("people", ja_people, p)):
            if [(e["id"], e["work"]) for e in a] != [(e["id"], e["work"]) for e in b]:
                notes.append(f"{lang} {what}: the game's {lang} list differs from the Japanese one "
                             f"({len(b)} vs {len(a)} entries); the Japanese order is exported")
    display = {lang: ({e["id"]: e["name"] for e in built[lang][0]}, {e["id"]: e["name"] for e in built[lang][1]}) for lang in LANGS}
    def shown(lang, kind, eid, tid):
        return display[lang][kind].get(eid) or texts[lang][0](tid)

    unit_image, portrait = art_lookups(rom_bytes)
    unlocks = unlock_rows(rom)
    upgrade_types = [{"type": t, "power": increments(rom, t), "price": prices(rom, t)} for t in range(1, 5)]

    def weapon(number, cap, uid):
        at = WEAPONS_AT + number * WEAPON_SIZE
        names, markers = {}, None
        for lang in LANGS:
            name, mk = weapon_markers(texts[lang][0](T_WEAPON_MENU + number), texts[lang][0](T_WEAPON_PURE + number))
            names[lang] = name
            if lang == "ja":
                markers = mk
        wtype = rom.u8(at + 0xE)
        power = rom.u8(at + 1) * 100
        full = None
        if 1 <= wtype <= 4:
            full = power + sum(increments(rom, wtype)[:min(cap, UPGRADE_LEVELS)])
        skill = rom.u8(at + 8)
        unlock = None
        for u, upgraded, unlocked in unlocks:
            if u == uid and unlocked == number:
                unlock = {"weapon": upgraded, "name": loc(T_WEAPON_PURE + upgraded)}
        return {"id": number, "name": names, "markers": markers, "power": power, "max_power": full,
                "upgrade_type": wtype if 1 <= wtype <= 4 else None,
                "range": [rom.u8(at + 2), rom.u8(at + 3)], "hit": rom.s8(at + 4), "crit": rom.s8(at + 0xD),
                "ammo": rom.s8(at + 5) if rom.s8(at + 5) >= 0 else None,
                "en": rom.u8(at + 6) or None, "morale": rom.u8(at + 7) or None,
                "required_skill": {"id": skill, "name": loc(skill)} if skill >= 2 else None,
                "terrain": terrain(rom, at + 9), "unlock_at_full_upgrade": unlock,
                "combination": bool(rom.u8(at + 0xF) & 0x02)}

    units = []
    for e in ja_units:
        uid = e["id"]
        at = UNITS_AT + uid * UNIT_SIZE
        size_bits, move_bits, equipment = rom.u8(at + 4), rom.u8(at + 5), rom.u8(at + 0x18)
        size = 0 if size_bits & 1 else 1 if size_bits & 2 else 2 if size_bits & 4 else 3 if size_bits & 8 else 4
        types = [loc(rom.u16(MOVE_ICONS + bit * 2)) for bit in range(4) if move_bits & (1 << bit)]
        if move_bits & 0x10:
            types.append(loc(rom.u16(MOVE_ICONS + 8)))
        flags = rom.u32(at + 0x1C)
        abilities = []
        for n in range(ABILITY_ENTRIES):
            mask = rom.u32(ABILITY_TABLE + n * 8)
            if flags & mask:
                name = loc(rom.u16(ABILITY_TABLE + n * 8 + 4))
                if mask in (4, 8):
                    name = {k: v + (" 10%" if mask == 4 else " 20%") for k, v in name.items()}
                abilities.append(name)
        cap = rom.u8(at + 0x20)
        listed = list(e["numbers"])
        for u, _, unlocked in unlocks:
            if u == uid and unlocked not in listed and unlocked < WEAPON_COUNT:
                listed.append(unlocked)
        weapons = [weapon(n, cap, uid) for n in listed]
        present = sorted({w["upgrade_type"] for w in weapons if w["upgrade_type"]})
        totals = [{"type": t, "power": sum(increments(rom, t)[:min(cap, UPGRADE_LEVELS)]),
                   "funds": sum(prices(rom, t)[:min(cap, UPGRADE_LEVELS)])} for t in present]
        units.append({
            "id": uid, "series": e["work"] if e["work"] >= 0 else None,
            "model": texts["ja"][0](T_MODELS + e["model"]) if e["model"] >= 0 else None,
            "name": {lang: shown(lang, 0, uid, T_UNIT + uid) for lang in LANGS},
            "upgrade_cap": cap, "hp": rom.u16(at), "en": rom.u16(at + 2), "move": rom.u8(at + 6),
            "mobility": rom.u16(at + 8), "armor": rom.u16(at + 0xA), "limit": rom.u16(at + 0xC),
            "size": texts["ja"][0](T_SIZES + size), "movement_types": types, "terrain": terrain(rom, at + 0xE),
            "abilities": abilities, "shield": bool(equipment & 2), "part_slots": rom.u8(at + 0x19),
            "repair_cost": rom.u16(at + 0x14), "weapons": weapons, "upgrade_totals": totals,
            "image": unit_image(uid)})

    # 恋爱補正 (love bonus) partners, through the duplicate-record aliases.
    alias = {}
    for n in range(ACTOR_ALIAS_COUNT):
        alias[rom.u16(ACTOR_ALIASES + n * 4)] = rom.u16(ACTOR_ALIASES + n * 4 + 2)
    main_of = lambda a: alias.get(a, a)
    def partners_of(actor):
        out = []
        for row in range(LOVE_ROWS):
            holder = rom.s16(LOVE_TABLE + row * 8 + 2)
            if holder < 0 or main_of(holder) != main_of(actor):
                continue
            for field in (4, 6):
                partner = rom.s16(LOVE_TABLE + row * 8 + field)
                if partner >= 0 and partner not in out:
                    out.append(partner)
        return out
    def short_name(actor):
        person = FIRST_PERSON <= actor < FIRST_PERSON + PEOPLE
        return loc(T_PEOPLE + actor - FIRST_PERSON if person else T_PILOT + actor)

    people = []
    for e in ja_people:
        actor, stats, spirits = e["id"], e["stats"], e["spirits"]
        person = FIRST_PERSON <= actor < FIRST_PERSON + PEOPLE
        p = {"id": actor, "series": e["work"] if e["work"] >= 0 else None,
             "name": {lang: shown(lang, 1, actor, T_PEOPLE + actor - FIRST_PERSON if person else T_PILOT + actor) for lang in LANGS},
             "full_name": loc(T_PEOPLE_FULL + actor - FIRST_PERSON if person else T_PILOT_FULL + actor),
             "enemy": e["flag"] == 1 if e["flag"] >= 0 else None, "role": None, "no_battle": stats < 0,
             "stats": None, "terrain": None, "double_move_level": None, "spirits": [], "skills": []}
        if stats >= 0:
            at = PILOT_STATS_AT + stats * PILOT_STATS_SIZE
            base = {"melee": rom.u8(at + 1), "shooting": rom.u8(at + 2), "evade": rom.u8(at + 3), "hit": rom.u8(at + 4),
                    "reaction": rom.u8(at + 5), "skill": rom.u8(at + 6), "sp": rom.u8(at + 0xC)}
            gain = {"melee": 1, "shooting": 1, "evade": 2, "hit": 2, "reaction": 1, "skill": 1, "sp": 2}
            p["stats"] = {"lv1": base, "lv99": {k: v + 98 * gain[k] for k, v in base.items()}}
            p["terrain"] = terrain(rom, at + 7)
            role = rom.u8(at)
            p["role"] = "fairy" if role & 0x40 else "sub_pilot" if role & 0x80 else None
            if rom.u8(at + 0xE) and not p["role"]:
                p["double_move_level"] = rom.u8(at + 0xE)
            bits, first_group = rom.u8(at + 0xF), False
            for bit, tbase, group in SKILLS:
                if not bits & bit or (group == 0 and first_group):
                    continue
                if group == 0:
                    first_group = True
                levels = sorted(v for v in (rom.u8(THRESHOLDS_AT + stats * THRESHOLDS_SIZE + group * 10 + n) for n in range(9)) if v)
                steps = [{"level": levels[n], "rank": n + 1, "label": loc(tbase + n + 1)}
                         for n in range(len(levels)) if n + 1 == len(levels) or levels[n + 1] != levels[n]]
                if steps:
                    first = loc(tbase + 1)
                    p["skills"].append({"name": {k: re.sub(r"\s*L\d+$", "", v) for k, v in first.items()}, "levels": steps})
        if spirits >= 0:
            for n in range(6):
                at = SPIRITS_AT + spirits * SPIRITS_SIZE + n * 2
                level, command = rom.u8(at), rom.u8(at + 1)
                if level and level < 100 and command < 30:
                    p["spirits"].append({"level": level, "id": command, "name": loc(T_SPIRITS + command),
                                         "cost": rom.u8(SPIRIT_COSTS + command)})
        p["love"] = [{"partner_id": partner, "name": short_name(partner),
                      "mutual": any(main_of(back) == main_of(actor) for back in partners_of(partner)),
                      "portrait": portrait(partner)} for partner in partners_of(actor)]
        p["portrait"] = portrait(actor)
        people.append(p)

    def order(entries):
        seen = []
        for e in entries:
            if e["series"] not in seen:
                seen.append(e["series"])
        return seen
    unit_order, people_order = order(units), order(people)
    series_ids = [s for s in unit_order if s is not None] + [s for s in people_order if s is not None and s not in unit_order]
    series_ids += [s for s in range(WORK_COUNT) if s not in series_ids]
    series = [{"id": s, "name": loc(T_WORKS + s)} for s in series_ids]
    series.append({"id": None, "name": {lang: texts[lang][1].get("library_work_other", "その他") for lang in LANGS}})

    labels = {k: loc(tid) for k, tid in LABEL_TEXTS.items()}
    labels.update({f"weapon_{k}": loc(T_WEAPON_LABELS + n) for n, k in enumerate(WEAPON_LABEL_KEYS)})
    ja_ui = texts["ja"][1]
    for key in sorted(k for k in ja_ui if k.startswith("library_")):
        labels[key] = {lang: texts[lang][1].get(key, ja_ui[key]) for lang in LANGS}

    for lang in LANGS:
        notes += sorted(texts[lang][0].warnings)
    return {"schema": "srw64.web-library.v1", "generated_from": hashlib.sha256(rom_bytes).hexdigest(),
            "counts": {"units": len(units), "people": len(people)}, "notes": notes,
            "series": series, "unit_series_order": unit_order, "people_series_order": people_order,
            "labels": labels, "upgrade_types": upgrade_types, "units": units, "people": people}


# --- Check -------------------------------------------------------------------
def check(data: dict) -> list[str]:
    bad = []
    def expect(what, got, want):
        if got != want:
            bad.append(f"{what}: got {got!r}, want {want!r}")
    expect("counts", data["counts"], {"units": 353, "people": 293})
    first = data["units"][0]
    series = {s["id"]: s for s in data["series"]}
    expect("first unit series", series[first["series"]]["name"]["ja"], "機動戦士ガンダム")
    expect("first unit", (first["name"]["ja"], first["model"]), ("ガンダム", "RX-78-2"))
    expect("RX-78-2 stats", {k: first[k] for k in ("hp", "en", "move", "mobility", "armor", "limit", "upgrade_cap", "shield", "part_slots", "repair_cost")},
           {"hp": 3000, "en": 80, "move": 5, "mobility": 70, "armor": 800, "limit": 240, "upgrade_cap": 15, "shield": True, "part_slots": 3, "repair_cost": 1600})
    expect("RX-78-2 terrain", first["terrain"], {"air": "-", "land": "A", "sea": "C", "space": "A"})
    want = [("60mmバルカン砲", 700, 3300, 4, [1, 1], 30, -20, 20), ("ビームサーベル", 1100, 4000, 2, [1, 1], 15, 0, None),
            ("ビームライフル", 1100, 4000, 2, [1, 5], 0, 10, 10), ("ハイパーバズーカ", 1200, 4100, 2, [3, 5], -20, -10, 4),
            ("ハイパーハンマー", 1400, 4300, 2, [1, 1], -10, 20, None)]
    got = [(w["name"]["ja"], w["power"], w["max_power"], w["upgrade_type"], w["range"], w["hit"], w["crit"], w["ammo"]) for w in first["weapons"]]
    for row in want:
        if row not in got:
            bad.append(f"RX-78-2 weapon {row[0]}: want {row}, have {[g for g in got if g[0] == row[0]] or 'none'}")
    expect("RX-78-2 upgrade totals", first["upgrade_totals"], [{"type": 2, "power": 2900, "funds": 480000}, {"type": 4, "power": 2600, "funds": 240000}])
    shiro = [p for p in data["people"] if p["name"]["ja"] == "シロー" and series[p["series"]]["name"]["ja"] == "第08MS小隊"]
    if not shiro:
        bad.append("シロー (第08MS小隊) missing")
        return bad
    s = shiro[0]
    expect("シロー zh full name", s["full_name"]["zh"], "天田士郎")
    expect("シロー double move", s["double_move_level"], 50)
    st = s["stats"]
    expect("シロー stats", {k: (st["lv1"][k], st["lv99"][k]) for k in st["lv1"]},
           {"melee": (129, 227), "shooting": (138, 236), "skill": (106, 204), "reaction": (90, 188), "hit": (102, 298), "evade": (120, 316), "sp": (60, 256)})
    expect("シロー terrain", s["terrain"], {"air": "A", "land": "A", "sea": "B", "space": "A"})
    expect("シロー spirits", [(sp["name"]["ja"], sp["level"], sp["cost"]) for sp in s["spirits"]],
           [("努力", 2, 20), ("必中", 4, 25), ("熱血", 13, 40), ("ひらめき", 19, 15), ("信頼", 25, 30), ("愛", 33, 90)])
    expect("シロー skills", [(sk["name"]["ja"], [(l["rank"], l["level"]) for l in sk["levels"]]) for sk in s["skills"]],
           [("切り払い", list(zip(range(1, 8), [5, 10, 22, 28, 39, 47, 58]))),
            ("S防御", list(zip(range(1, 10), [8, 16, 23, 31, 33, 37, 40, 46, 55])))])
    expect("シロー love", [(l["name"]["ja"], l["mutual"]) for l in s["love"]], [("アイナ", True)])
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "web/.data/library.json")
    parser.add_argument("--check", action="store_true", help="assert known in-game facts")
    parser.add_argument("--baseline", help="release tag whose term tables the changes are counted from "
                                           "(default: the tag in web/src/data/release.json)")
    args = parser.parse_args()
    data = export(args.rom)
    tag = args.baseline or json.loads((ROOT / "web/src/data/release.json").read_text())["tag"]
    mark_changes(data, export(args.rom, locale_docs(tag)), tag)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n")
    print(f"{args.out}: {data['counts']['units']} units, {data['counts']['people']} people")
    for kind in ("units", "people"):
        print(f"since {tag}, {kind}: " + ", ".join(
            f"{lang} {sum(1 for e in data[kind] if e['changed'][lang])} with changes" for lang in ("zh", "en")))
    for note in data["notes"]:
        print("note:", note)
    if args.check:
        bad = check(data)
        for line in bad:
            print("FAIL", line)
        print("check:", "FAILED" if bad else "ok")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
