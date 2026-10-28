#!/usr/bin/env python3
"""Checks a MOD package before it is listed: a .srw64mod (or .zip) file, or a package folder.

The rules are docs/design/mod-packages.md §2 (mod.json), §3 (ids and requirements) and
§13.4 (what a submission must satisfy). The manifest and archive rules are the host's
(src/host/mod_library.hpp parse_manifest, safe_entry and the install limits) with the same
problem codes, so a package this accepts installs, and the wording can be the game's own
(--locale). tests/mods/manifests.json holds manifest cases both sides must agree on.

    tools/mods/validate.py example.gaiden-1.2.0.srw64mod
    tools/mods/validate.py pkg.srw64mod --index web/public/mods/index.json \\
        --owners web/src/data/mods/owners.json --owner github:someone \\
        --original-hashes tools/mods/original-hashes.txt --json

Exit code 1 when anything is wrong. --summary prints what the index entry copies from the
manifest (name, description, authors, license, game, requires, optional, conflicts,
contents, bytes, sha256).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

SCHEMA = "srw64.mod.v1"
CAMPAIGN_IMAGE_SCHEMA = "srw64.campaign-image.v1"
PACKAGE_EXTENSION = ".srw64mod"
# src/host/mod_library.hpp `licenses`: the ones that allow the package to be passed on.
LICENSES = ("CC-BY-4.0", "CC-BY-SA-4.0", "CC-BY-NC-4.0", "CC-BY-NC-SA-4.0", "CC0-1.0", "MIT", "GPL-3.0-or-later")
KNOWN_FIELDS = ("schema", "id", "version", "name", "description", "authors", "license", "game", "requires", "optional", "conflicts")
# §2: what a package may hold at its top.
CONTENT_DIRS = ("campaign", "challenge", "story", "data", "art", "dialogue", "audio")
TOP_FILES = ("mod.json", "rules.json")
# The file kinds the content folders are read for (mod_library.hpp summarize), plus notes.
EXTENSIONS = (".json", ".txt", ".md", ".png", ".webp", ".dds", ".ogg", ".mp3", ".flac", ".wav")
# mod_library.cpp install limits.
MAX_TOTAL = 4 << 30
MAX_ENTRIES = 20000
# §13.4: the size a submission may have.
MAX_SUBMISSION = 100 << 20

# The host words these through mod_error_<key> (frontend.cpp mod_problem_text).
MANIFEST_CODES = {"manifest", "field", "schema", "id", "version", "name", "description", "authors", "license", "requires", "optional", "conflicts"}
ARCHIVE_CODES = {"open", "archive", "archive_size", "archive_path"}
# Codes only this check gives (the host never sees a submission), in English.
SUBMISSION_TEXT = {
    "too_large": "The package is larger than the limit ({detail})",
    "filename": "The file must be named <id>-<version>.srw64mod ({detail})",
    "license_missing": "mod.json needs a license, one of: {detail}",
    "layout": "Not one of the package folders or files ({detail})",
    "file_type": "Not a file kind a package may hold ({detail})",
    "empty": "The package holds no content",
    "original": "A file is the original's own data; refer to it with copy_from instead ({detail})",
    "not_newer": "The version must be newer than the listed one ({detail})",
    "missing": "A required MOD is not listed: {detail}",
    "owner": "The id's prefix belongs to someone else ({detail})",
    "no_owner": "The id's prefix is not registered in owners.json ({detail})",
    "campaign": "campaign/campaign.json is not a compiled campaign of this package ({detail})",
}


@dataclass
class Problem:
    code: str
    detail: str = ""

    def __iter__(self):
        yield self.code
        yield self.detail


# --- The host's rules (mod_library.hpp) ---------------------------------------------------

def parse_version(text: str) -> tuple[int, int, int] | None:
    """Up to three numbers, "1", "1.2" or "1.2.3" (missing parts are 0); each at most six digits."""
    if not isinstance(text, str):
        return None
    parts = text.split(".")
    if not 1 <= len(parts) <= 3 or any(not p.isdigit() or not p.isascii() or len(p) > 6 for p in parts):
        return None
    numbers = [int(p) for p in parts] + [0] * (3 - len(parts))
    return numbers[0], numbers[1], numbers[2]


def version_text(v: tuple[int, int, int]) -> str:
    return f"{v[0]}.{v[1]}.{v[2]}"


def _clauses(text: str) -> list[tuple[str, tuple[int, int, int]]] | None:
    out = []
    for clause in text.split(" "):
        if not clause:
            continue
        op = 0
        while op < len(clause) and clause[op] in "<>=":
            op += 1
        oper, version = clause[:op], parse_version(clause[op:])
        if oper not in (">=", ">", "<=", "<", "=") or version is None:
            return None
        out.append((oper, version))
    return out or None


def valid_requirement(text: str) -> bool:
    """`*` or space-separated clauses, each an operator and a version: ">=1.0 <2"."""
    return isinstance(text, str) and (text == "*" or _clauses(text) is not None)


def satisfies(v: tuple[int, int, int], requirement: str) -> bool:
    if requirement == "*":
        return True
    clauses = _clauses(requirement)
    if clauses is None:
        return False
    ops: dict[str, Callable[[tuple, tuple], bool]] = {">=": lambda a, b: a >= b, ">": lambda a, b: a > b, "<=": lambda a, b: a <= b, "<": lambda a, b: a < b, "=": lambda a, b: a == b}
    return all(ops[oper](v, want) for oper, want in clauses)


def valid_id(text: str) -> bool:
    """campaign_library.hpp valid_id: a folder name of letters, digits, `.`, `_`, `-`, at most 64."""
    return isinstance(text, str) and 0 < len(text) <= 64 and text not in (".", "..") and \
        all(c.isascii() and (c.isalnum() or c in "._-") for c in text)


def safe_entry(name: str) -> str | None:
    """A path inside an archive: relative, no `..`, no drive or root, '/' separated; without a leading ./"""
    name = name.replace("\\", "/")
    while name.startswith("./"):
        name = name[2:]
    if not name or name[0] == "/" or ":" in name:
        return None
    parts = name.split("/")
    for i, part in enumerate(parts):
        if part == ".." or (part == "" and i < len(parts) - 1):
            return None
    return name


def _text_field(value) -> bool:
    if isinstance(value, str):
        return value != ""
    return isinstance(value, dict) and bool(value) and all(isinstance(t, str) for t in value.values())


def parse_manifest(data) -> tuple[dict | None, Problem | None]:
    """mod.json, strictly: unknown fields are errors. The codes are the host's."""
    if not isinstance(data, dict):
        return None, Problem("manifest", "not an object")
    for key in data:
        if key not in KNOWN_FIELDS:
            return None, Problem("field", key)
    if data.get("schema") != SCHEMA:
        return None, Problem("schema", data["schema"] if isinstance(data.get("schema"), str) else "")
    if not valid_id(data.get("id")):
        return None, Problem("id", "")
    m: dict = {"id": data["id"]}
    if not isinstance(data.get("version"), str):
        return None, Problem("version", "")
    version = parse_version(data["version"])
    if version is None:
        return None, Problem("version", data["version"])
    m["version"] = version
    if not _text_field(data.get("name")):
        return None, Problem("name", "")
    m["name"] = data["name"]
    if "description" in data:
        if not _text_field(data["description"]):
            return None, Problem("description", "")
        m["description"] = data["description"]
    m["authors"] = []
    if "authors" in data:
        if not isinstance(data["authors"], list) or any(not isinstance(a, str) for a in data["authors"]):
            return None, Problem("authors", "")
        m["authors"] = list(data["authors"])
    m["license"] = ""
    if "license" in data:
        if not isinstance(data["license"], str) or data["license"] not in LICENSES:
            return None, Problem("license", data["license"] if isinstance(data["license"], str) else "")
        m["license"] = data["license"]
    m["game"] = "*"
    if "game" in data:
        if not valid_requirement(data["game"]):
            return None, Problem("game", "")
        m["game"] = data["game"]
    for key in ("requires", "optional", "conflicts"):
        m[key] = {}
        if key not in data:
            continue
        if not isinstance(data[key], dict):
            return None, Problem(key, "")
        for other, want in data[key].items():
            if not valid_id(other) or other == m["id"] or not valid_requirement(want):
                return None, Problem(key, other)
            m[key][other] = want
    return m, None


# --- The package on disk -------------------------------------------------------------------

@dataclass
class Entry:
    name: str           # '/'-separated, without the single top folder a zip may wrap everything in
    size: int
    read: Callable[[], bytes]


@dataclass
class Package:
    path: Path
    is_file: bool
    entries: list[Entry] = field(default_factory=list)
    manifest: dict | None = None
    raw_manifest: dict | None = None


def _is_junk(name: str) -> bool:
    return name.startswith("__MACOSX/") or name.endswith(".DS_Store")


def _collect(path: Path) -> tuple[list[Entry], Problem | None]:
    """Every file in the package, paths checked, junk left out, the way the installer reads them."""
    entries: list[Entry] = []
    if path.is_dir():
        for root, dirs, files in os.walk(path):
            dirs[:] = sorted(d for d in dirs if not _is_junk(d + "/"))
            for name in sorted(files):
                full = Path(root) / name
                rel = full.relative_to(path).as_posix()
                if _is_junk(rel):
                    continue
                entries.append(Entry(rel, full.stat().st_size, (lambda p=full: p.read_bytes())))
        return entries, None
    try:
        archive = zipfile.ZipFile(path)
        infos = archive.infolist()
    except (zipfile.BadZipFile, OSError) as error:
        return [], Problem("archive", path.name if isinstance(error, zipfile.BadZipFile) else str(error))
    if len(infos) > MAX_ENTRIES:
        return [], Problem("archive_size", "")
    total = 0
    for info in infos:
        if _is_junk(info.filename):
            continue
        safe = safe_entry(info.filename)
        if safe is None:
            return [], Problem("archive_path", info.filename)
        total += info.file_size
        if info.is_dir():
            continue
        entries.append(Entry(safe, info.file_size, (lambda i=info, a=archive: a.read(i))))
    if total > MAX_TOTAL:
        return [], Problem("archive_size", "")
    return entries, None


def open_package(path: Path) -> tuple[Package, Problem | None]:
    """Reads the package and its manifest; a zip may keep everything inside one top folder."""
    package = Package(path, path.is_file())
    entries, problem = _collect(path)
    if problem:
        return package, problem
    names = {e.name for e in entries}
    prefix = ""
    if "mod.json" not in names:
        tops = {e.name.split("/", 1)[0] for e in entries}
        if len(tops) == 1 and f"{next(iter(tops))}/mod.json" in names:
            prefix = next(iter(tops)) + "/"
        else:
            return package, Problem("no_manifest", "")
    for e in entries:
        e.name = e.name[len(prefix):]
    package.entries = entries
    manifest_entry = next(e for e in entries if e.name == "mod.json")
    try:
        package.raw_manifest = json.loads(manifest_entry.read().decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as error:
        return package, Problem("manifest", str(error))
    package.manifest, problem = parse_manifest(package.raw_manifest)
    return package, problem


def contents_of(entries: list[Entry]) -> list[str]:
    """The §2 folders and files the package actually has, in §2 order."""
    names = {e.name for e in entries}
    tops = {e.name.split("/", 1)[0] for e in entries}
    # A campaign counts when its image is there (mod_library.hpp summarize).
    return [d for d in CONTENT_DIRS if d in tops and (d != "campaign" or "campaign/campaign.json" in names)] + \
        [f for f in TOP_FILES if f in tops and f != "mod.json"]


NAMES_SCHEMA = "srw64.mod-names.v1"
NAME_LOCALES = ("en", "ja", "zh-Hans", "zh-Hant")
NAME_ROUTES = ("real-male", "real-female", "super-male", "super-female")


def _name_problem(value, where: str, limit: int) -> str:
    if not isinstance(value, str):
        return f"{where}：要是字符串"
    if not value.strip(" \t"):
        return f"{where}：不能为空"
    if value[0] == " " or value[-1] == " ":
        return f"{where}：首尾不能有空格"
    if any(ord(c) < 0x20 or ord(c) == 0x7F or c in "<>{}\u3000" for c in value):
        return f"{where}：不能有控制字符、全角空格或 < > {{ }}"
    if len(value) > limit:
        return f"{where}：太长（最多 {limit} 个字符）"
    return ""


def names_problem(document) -> str:
    """dialogue/names.json by the host's rules (src/host/mod_names.hpp parse), "" when it is good."""
    if not isinstance(document, dict):
        return "顶层要是对象"
    for key, value in document.items():
        if key == "schema":
            if value != NAMES_SCHEMA:
                return f"schema：要是 {NAMES_SCHEMA}"
        elif key == "heroes":
            if not isinstance(value, dict):
                return "heroes：要是对象"
            for route, people in value.items():
                if route not in NAME_ROUTES:
                    return f"heroes.{route}：未知路线（{'、'.join(NAME_ROUTES)}）"
                if not isinstance(people, dict):
                    return f"heroes.{route}：要是对象"
                for role, by_locale in people.items():
                    if role not in ("hero", "partner"):
                        return f"heroes.{route}.{role}：要是 hero 或 partner"
                    if not isinstance(by_locale, dict):
                        return f"heroes.{route}.{role}：要是对象"
                    for locale, fields in by_locale.items():
                        where = f"heroes.{route}.{role}.{locale}"
                        if locale not in NAME_LOCALES:
                            return f"{where}：未知语言（{'、'.join(NAME_LOCALES)}）"
                        if not isinstance(fields, dict):
                            return f"{where}：要是对象"
                        if "name" not in fields:
                            return f"{where}：缺少 name"
                        for field, text in fields.items():
                            if field not in ("name", "surname", "nickname"):
                                return f"{where}：未知字段 {field}（name、surname、nickname）"
                            if why := _name_problem(text, f"{where}.{field}", 16):
                                return why
        elif key == "unit":
            if not isinstance(value, dict):
                return "unit：要是对象"
            for locale, text in value.items():
                if locale not in NAME_LOCALES:
                    return f"unit.{locale}：未知语言（{'、'.join(NAME_LOCALES)}）"
                if why := _name_problem(text, f"unit.{locale}", 24):
                    return why
        elif key in ("pilots", "units", "weapons"):
            if not isinstance(value, dict):
                return f"{key}：要是对象"
            count = {"pilots": 361, "units": 363, "weapons": 1329}[key]
            for number, by_locale in value.items():
                where = f"{key}.{number}"
                if not _plain_number(number) or int(number) >= count:
                    return f"{where}：编号要是 0–{count - 1} 的十进制整数"
                if key == "pilots" and 25 <= int(number) < 33:
                    return f"{where}：原创主角和搭档的名字写在 heroes 里"
                if not isinstance(by_locale, dict):
                    return f"{where}：要是对象"
                for locale, entry in by_locale.items():
                    if locale not in NAME_LOCALES:
                        return f"{where}.{locale}：未知语言（{'、'.join(NAME_LOCALES)}）"
                    if key == "pilots":
                        if not isinstance(entry, dict) or "name" not in entry:
                            return f'{where}.{locale}：要是 {{"name": …, "full": …}}'
                        for field, text in entry.items():
                            if field not in ("name", "full"):
                                return f"{where}.{locale}：未知字段 {field}（name、full）"
                            if why := _name_problem(text, f"{where}.{locale}.{field}", 24):
                                return why
                    elif why := _name_problem(entry, f"{where}.{locale}", 24):
                        return why
        else:
            return f"未知字段 {key}（schema、heroes、unit、pilots、units、weapons）"
    return ""


def voice_problem(rest: str) -> str | None:
    """A battle voice (audio/voice/battle/<record>.ogg) by the host's rules (src/host/music.hpp
    collect_voices): an Ogg file named by a battle quote's text record, 5799-17346."""
    folder, _, name = rest.rpartition("/")
    if folder != "battle":
        return "暂不支持：战斗语音放在 audio/voice/battle/"
    stem, _, ext = name.rpartition(".")
    if ext.lower() != "ogg":
        return "要是 Ogg Vorbis（.ogg）"
    if not (stem.isdigit() and stem.isascii() and not stem.startswith("0")) or not 5799 <= int(stem) <= 17346:
        return "文件名要是战斗台词的记录号（5799–17346）"
    return None


def voice_settings_problem(data) -> str | None:
    """audio/voice.json: {"schema": "srw64.voice.v1", "gain": dB -30-12, "delay": ms 0-2000}."""
    if not isinstance(data, dict) or data.get("schema") != "srw64.voice.v1":
        return "schema 要是 srw64.voice.v1"
    for key in data:
        if key not in ("schema", "gain", "delay"):
            return f"不认识的字段 {key}"
    gain = data.get("gain", 0)
    if isinstance(gain, bool) or not isinstance(gain, (int, float)) or not -30 <= gain <= 12:
        return "gain 要是 -30 到 12 的数（dB）"
    delay = data.get("delay", 0)
    if isinstance(delay, bool) or not isinstance(delay, int) or not 0 <= delay <= 2000:
        return "delay 要是 0 到 2000 的整数（毫秒）"
    return None


def icon_problem(name: str, data: bytes) -> str | None:
    """A map icon (art/icons/<unit>.png) by the host's rules (src/host/native_icon.cpp,
    unit_icon_art.hpp): named by unit number, a square PNG of 16, 32, 64 or 128 pixels."""
    stem, _, ext = name.rpartition(".")
    if ext.lower() != "png" or not _plain_number(stem) or int(stem) >= 363:
        return "文件名要是机体号.png（0–362）"
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return "不是 PNG"
    width, height = int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    if width != height or width not in (16, 32, 64, 128):
        return f"{width}×{height}，要是 16、32、64 或 128 像素的正方形"
    return None


def _plain_number(text: str) -> bool:
    return text.isdigit() and text.isascii() and len(text) <= 4 and (text == "0" or not text.startswith("0"))


# src/host/mod_data.hpp: each file's fields as (low, high, scale), the special ones by name.
DATA_FIELDS = {
    "units.json": (363, {"hp": (1, 65535, 1), "en": (0, 65535, 1), "move": (1, 30, 1), "mobility": (0, 65535, 1),
                         "armor": (0, 65535, 1), "limit": (0, 65535, 1), "repair_cost": (0, 65535, 1), "upgrade_cap": (0, 15, 1)}),
    "pilots.json": (360, {"melee": (0, 255, 1), "ranged": (0, 255, 1), "evade": (0, 255, 1), "hit": (0, 255, 1),
                          "reaction": (0, 255, 1), "skill": (0, 255, 1), "sp": (0, 255, 1), "double_move": (0, 99, 1)}),
    "weapons.json": (1329, {"power": (0, 25500, 100), "range_min": (0, 30, 1), "range_max": (1, 30, 1), "hit": (-128, 127, 1),
                            "ammo": (-1, 99, 1), "en": (0, 255, 1), "morale": (0, 255, 1), "critical": (-128, 127, 1)}),
}
TERRAIN = ("air", "land", "sea", "space")
ABILITIES = ("beam_coat", "i_field", "planet_defensor", "aura_barrier", "afterimage", "mach_special", "true_mach_special",
             "god_shadow", "getter_vision", "shungensoku", "hyper_jammer", "hp_regen_10", "hp_regen_20")
SKILLS = ("potential", "newtype", "enhanced", "holy_warrior", "esp", "parry", "shield_defense")
SPIRITS = ("self_destruct", "scout", "accel", "mercy", "focus", "alert", "guts", "strike", "trust", "wall", "cover", "valor",
           "spirit", "great_guts", "luck", "drain", "effort", "soul", "stealth", "awaken", "refuel", "provoke", "encourage",
           "disrupt", "friendship", "love", "reactivate", "miracle", "prayer", "revive")


def _levels(value, where: str, ascending_pairs: bool = False) -> str:
    if not isinstance(value, list) or not value or len(value) > 9:
        return f"{where}：要是 1–9 个等级的数组，或 null 去掉"
    if any(not isinstance(v, int) or isinstance(v, bool) or not 1 <= v <= 99 for v in value):
        return f"{where}：等级要是 1–99 的整数"
    if value != sorted(value):
        return f"{where}：等级要从小到大"
    return ""


def data_problem(name: str, document) -> str:
    """data/<name> by the host's rules (src/host/mod_data.hpp parse), "" when it is good. Whether a
    pilot has an ability or spirit record, and the ROM's own min/max range, only the game knows."""
    count, fields = DATA_FIELDS[name]
    if not isinstance(document, dict):
        return "顶层要是对象"
    for key, entry in document.items():
        if not _plain_number(key) or int(key) >= count:
            return f"{key}：编号要是 0–{count - 1} 的十进制整数"
        if not isinstance(entry, dict):
            return f"{key}：要是对象"
        if name == "weapons.json" and "range_min" in entry and "range_max" in entry and \
                isinstance(entry["range_min"], int) and isinstance(entry["range_max"], int) and entry["range_min"] > entry["range_max"]:
            return f"{key}：range_min 不能大于 range_max"
        first_row = False
        for field, value in entry.items():
            where = f"{key}.{field}"
            if field == "terrain":
                if not isinstance(value, dict):
                    return f"{where}：要是对象"
                for channel, letter in value.items():
                    if channel not in TERRAIN:
                        return f"{where}：未知地形 {channel}（air、land、sea、space）"
                    if letter not in ("A", "B", "C", "D", "-"):
                        return f"{where}.{channel}：要是 A、B、C、D 或 -"
            elif field in fields:
                low, high, scale = fields[field]
                if not isinstance(value, int) or isinstance(value, bool):
                    return f"{where}：要是整数"
                if not low <= value <= high:
                    return f"{where}：超出范围（{low}–{high}）"
                if value % scale:
                    return f"{where}：要是 {scale} 的倍数"
            elif name == "units.json" and field == "abilities":
                if not isinstance(value, list) or any(v not in ABILITIES for v in value):
                    return f"{where}：要是数组，每项是 {'、'.join(ABILITIES)} 之一"
            elif name == "units.json" and field in ("shield", "sword"):
                if not isinstance(value, bool):
                    return f"{where}：要是 true 或 false"
            elif name == "units.json" and field == "size":
                if value not in ("SS", "S", "M", "L", "LL"):
                    return f"{where}：要是 SS、S、M、L 或 LL"
            elif name == "units.json" and field == "parts_slots":
                if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 4:
                    return f"{where}：要是 0–4 的整数"
            elif name == "pilots.json" and field == "skills":
                if not isinstance(value, dict):
                    return f"{where}：要是对象"
                for skill, levels in value.items():
                    if skill not in SKILLS:
                        return f"{where}：未知技能 {skill}（{'、'.join(SKILLS)}）"
                    if levels is None:
                        continue
                    if SKILLS.index(skill) < 5:
                        if first_row:
                            return f"{where}：底力、NT、强化人、圣战士、超能力只能有一个"
                        first_row = True
                    if why := _levels(levels, f"{where}.{skill}"):
                        return why
            elif name == "pilots.json" and field == "spirits":
                if not isinstance(value, list) or len(value) > 6:
                    return f"{where}：要是最多 6 项的数组，每项 [精神, 等级]"
                last = 0
                for item in value:
                    if not isinstance(item, list) or len(item) != 2 or not isinstance(item[0], str) or not isinstance(item[1], int):
                        return f"{where}：每项要是 [精神, 等级]"
                    if item[0] not in SPIRITS:
                        return f"{where}：未知精神 {item[0]}（{'、'.join(SPIRITS)}）"
                    if not 1 <= item[1] <= 99 or item[1] < last:
                        return f"{where}：习得等级要是 1–99，从小到大"
                    last = item[1]
            else:
                return f"{key}：未知字段 {field}"
    return ""


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_original_hashes(path: Path) -> set[str]:
    """One hash per line, `sha256  path` or just the hash; `#` comments."""
    out = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(line.split()[0].lower())
    return out


def check(path: Path, *, host_only: bool = False, require_license: bool = True, max_bytes: int = MAX_SUBMISSION,
          original_hashes: set[str] | None = None, index: dict | None = None, owners: dict | None = None,
          owner: str | None = None, also_listed: tuple[str, ...] = ()) -> tuple[list[Problem], dict]:
    """Every problem found (the host's first; an unreadable package stops there), and the summary."""
    package, problem = open_package(path)
    summary: dict = {"path": str(path)}
    if problem:
        return [problem], summary
    m = package.manifest
    assert m is not None
    summary.update({"id": m["id"], "version": version_text(m["version"]), "name": m["name"], "description": m.get("description", {}),
                    "authors": m["authors"], "license": m["license"], "game": m["game"], "requires": m["requires"],
                    "optional": m["optional"], "conflicts": m["conflicts"], "contents": contents_of(package.entries)})
    if package.is_file:
        summary["file"] = path.name
        summary["bytes"] = path.stat().st_size
        summary["sha256"] = sha256_of(path)
    if host_only:
        return [], summary
    problems: list[Problem] = []
    if package.is_file and path.stat().st_size > max_bytes:
        problems.append(Problem("too_large", f"{path.stat().st_size} > {max_bytes}"))
    if package.is_file:
        wanted = f"{m['id']}-{version_text(m['version'])}{PACKAGE_EXTENSION}"
        if path.name != wanted:
            problems.append(Problem("filename", f"{path.name}, expected {wanted}"))
    if require_license and not m["license"]:
        problems.append(Problem("license_missing", ", ".join(LICENSES)))
    for e in package.entries:
        top, _, rest = e.name.partition("/")
        if rest == "" and top not in TOP_FILES or rest != "" and top not in CONTENT_DIRS:
            problems.append(Problem("layout", e.name))
        elif Path(e.name).suffix.lower() not in EXTENSIONS:
            problems.append(Problem("file_type", e.name))
    if not summary["contents"]:
        problems.append(Problem("empty"))
    for e in package.entries:
        if e.name != "campaign/campaign.json":
            continue
        try:
            image = json.loads(e.read().decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as error:
            problems.append(Problem("campaign", str(error)))
            continue
        if not isinstance(image, dict) or image.get("schema") != CAMPAIGN_IMAGE_SCHEMA:
            problems.append(Problem("campaign", "not " + CAMPAIGN_IMAGE_SCHEMA))
        elif image.get("id") != m["id"]:
            problems.append(Problem("campaign", f"id {image.get('id')!r}, the package is {m['id']!r}"))
    for e in package.entries:
        top, _, rest = e.name.partition("/")
        if top == "data" and rest in DATA_FIELDS:
            try:
                why = data_problem(rest, json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("data", f"{e.name}: {why}"))
        elif top == "data" and rest.endswith(".json"):
            problems.append(Problem("data", f"{e.name}: 暂不支持这个文件"))
        elif e.name.startswith("art/portraits/") and e.name.count("/") == 2:
            stem, _, ext = e.name.rsplit("/", 1)[1].rpartition(".")
            if ext.lower() != "png" or not _plain_number(stem) or int(stem) > 360:
                problems.append(Problem("portrait", f"{e.name}: 文件名要是人物号.png"))
        elif e.name.startswith("audio/voice/"):
            why = voice_problem(e.name[len("audio/voice/"):])
            if why:
                problems.append(Problem("voice", f"{e.name}: {why}"))
        elif e.name == "audio/voice.json":
            try:
                why = voice_settings_problem(json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("voice", f"{e.name}: {why}"))
        elif e.name.startswith("art/icons/") and e.name.count("/") == 2:
            why = icon_problem(e.name.rsplit("/", 1)[1], e.read())
            if why:
                problems.append(Problem("icon", f"{e.name}: {why}"))
    for e in package.entries:
        if e.name == "dialogue/names.json":
            try:
                why = names_problem(json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("names", why))
    if original_hashes:
        for e in package.entries:
            if e.name != "mod.json" and hashlib.sha256(e.read()).hexdigest() in original_hashes:
                problems.append(Problem("original", e.name))
    if index is not None:
        listed = {p["id"]: p for p in index.get("packages", [])}
        if m["id"] in listed:
            old = parse_version(listed[m["id"]]["version"]) or (0, 0, 0)
            if m["version"] <= old:
                problems.append(Problem("not_newer", f"{version_text(m['version'])} <= {version_text(old)}"))
        for other in list(m["requires"]) + list(m["optional"]):
            if other not in listed and other not in also_listed:
                problems.append(Problem("missing", other))
    if owners is not None:
        prefix = m["id"].split(".", 1)[0]
        if prefix not in owners:
            problems.append(Problem("no_owner", prefix))
        elif owner is not None and owners[prefix] != owner:
            problems.append(Problem("owner", f"{prefix}: {owners[prefix]}"))
    return problems, summary


# --- Wording -----------------------------------------------------------------------------

def _find_key(data, key: str):
    if isinstance(data, dict):
        if key in data and isinstance(data[key], str):
            return data[key]
        for value in data.values():
            found = _find_key(value, key)
            if found is not None:
                return found
    return None


def wording(problem: Problem, labels: dict | None) -> str:
    """The game's own text for the host's codes (content/locales/<lang>.json mod_error_*), else English."""
    code, detail = problem.code, problem.detail
    if code in SUBMISSION_TEXT:
        return SUBMISSION_TEXT[code].replace("{detail}", detail)
    key = "mod_error_" + ("manifest" if code in MANIFEST_CODES else "archive" if code in ARCHIVE_CODES else code)
    text = _find_key(labels, key) if labels else None
    if text is None:
        text = _find_key(labels, "mod_error_other") if labels else None
        return (text or "Error ({detail})").replace("{detail}", f"{code} {detail}".strip())
    return text.replace("{detail}", detail or code)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="a .srw64mod/.zip file or a package folder")
    parser.add_argument("--host-only", action="store_true", help="only what the game checks when it installs")
    parser.add_argument("--license-optional", action="store_true", help="do not require mod.json's license")
    parser.add_argument("--max-mb", type=int, default=MAX_SUBMISSION >> 20, help="the size limit for a submission (default 100)")
    parser.add_argument("--original-hashes", type=Path, help="SHA-256 list of the original's exported data; a package may not carry any of it")
    parser.add_argument("--index", type=Path, help="the published index.json: the version must be newer and requirements listed")
    parser.add_argument("--also", action="append", default=[], help="an id counted as listed (another entry in the same PR)")
    parser.add_argument("--owners", type=Path, help="owners.json: {prefix: owner}")
    parser.add_argument("--owner", help="who submits, e.g. github:someone or maintainer-verified:Name")
    parser.add_argument("--locale", default="en", help="the game's wording for the host's codes: en, zh-Hans or ja")
    parser.add_argument("--json", action="store_true", help="print problems and the summary as JSON")
    parser.add_argument("--summary", action="store_true", help="print the summary even when there are problems")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[2]
    labels = None
    locale_file = root / "content" / "locales" / f"{args.locale}.json"
    if locale_file.is_file():
        labels = json.loads(locale_file.read_text(encoding="utf-8"))
    problems, summary = check(
        args.package, host_only=args.host_only, require_license=not args.license_optional, max_bytes=args.max_mb << 20,
        original_hashes=load_original_hashes(args.original_hashes) if args.original_hashes else None,
        index=json.loads(args.index.read_text(encoding="utf-8")) if args.index else None,
        owners=json.loads(args.owners.read_text(encoding="utf-8")) if args.owners else None,
        owner=args.owner, also_listed=tuple(args.also))
    if args.json:
        print(json.dumps({"ok": not problems, "problems": [{"code": p.code, "detail": p.detail, "text": wording(p, labels)} for p in problems],
                          "summary": summary}, ensure_ascii=False, indent=2))
    else:
        for p in problems:
            print(f"{p.code}: {wording(p, labels)}")
        if not problems or args.summary:
            print(json.dumps(summary, ensure_ascii=False, indent=2) if args.summary else f"ok: {summary.get('id')} {summary.get('version')}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
