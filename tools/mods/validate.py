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
CAMPAIGN_IMAGE_SCHEMAS = ("srw64.campaign-image.v1", "srw64.campaign-image.v2")
PACKAGE_EXTENSION = ".srw64mod"
# src/host/mod_library.hpp `licenses`: the ones that allow the package to be passed on.
LICENSES = ("CC-BY-4.0", "CC-BY-SA-4.0", "CC-BY-NC-4.0", "CC-BY-NC-SA-4.0", "CC0-1.0", "MIT", "GPL-3.0-or-later")
KNOWN_FIELDS = ("schema", "id", "version", "name", "description", "authors", "license", "game", "requires", "optional", "conflicts")
# §2: what a package may hold at its top.
CONTENT_DIRS = ("campaign", "challenge", "story", "data", "art", "dialogue", "audio")
TOP_FILES = ("mod.json", "rules.json")
# The file kinds the content folders are read for (mod_library.hpp summarize), plus notes.
EXTENSIONS = (".json", ".txt", ".md", ".png", ".webp", ".dds", ".ogg", ".mp3", ".flac", ".wav", ".ttf", ".otf")
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
    "language": "A language folder is not right ({detail})",
}


@dataclass
class Problem:
    code: str
    detail: str = ""

    def __iter__(self):
        yield self.code
        yield self.detail


# The game reads a .srw64mod in place (mod_library.hpp ArchiveFiles), pulling each file out
# when it is needed. Pictures and sound are compressed already: deflating them again saves
# nothing and makes every read inflate them. A package should store them; one that does not
# still works, so this is a warning, not a problem (tools/mods/pack.py, web mod-package.ts).
STORED = (".png", ".ogg")


# --- The host's rules (mod_library.hpp) ---------------------------------------------------

def parse_version(text: str, most: int = 3) -> tuple[int, ...] | None:
    """Up to three numbers, "1", "1.2" or "1.2.3" (missing parts are 0); each at most six digits.
    A requirement's version may have a fourth (most=4), the game's fix releases ("0.5.4.1"):
    then four numbers come back. A package's own version keeps three (Thunderstore's x.y.z)."""
    if not isinstance(text, str):
        return None
    parts = text.split(".")
    if not 1 <= len(parts) <= most or any(not p.isdigit() or not p.isascii() or len(p) > 6 for p in parts):
        return None
    return tuple([int(p) for p in parts] + [0] * (most - len(parts)))


def version_text(v: tuple[int, ...]) -> str:
    return ".".join(str(n) for n in v[:3]) + (f".{v[3]}" if len(v) > 3 and v[3] else "")


def _clauses(text: str) -> list[tuple[str, tuple[int, ...]]] | None:
    out = []
    for clause in text.split(" "):
        if not clause:
            continue
        op = 0
        while op < len(clause) and clause[op] in "<>=":
            op += 1
        oper, version = clause[:op], parse_version(clause[op:], 4)
        if oper not in (">=", ">", "<=", "<", "=") or version is None:
            return None
        out.append((oper, version))
    return out or None


def valid_requirement(text: str) -> bool:
    """`*` or space-separated clauses, each an operator and a version: ">=1.0 <2"."""
    return isinstance(text, str) and (text == "*" or _clauses(text) is not None)


def satisfies(v: tuple[int, ...], requirement: str) -> bool:
    if requirement == "*":
        return True
    v = tuple(v) + (0,) * (4 - len(v))
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
    deflated: bool = False   # in a zip, compressed rather than stored


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
        entries.append(Entry(safe, info.file_size, (lambda i=info, a=archive: a.read(i)), info.compress_type != zipfile.ZIP_STORED))
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
NAME_LOCALES = ("en", "ja", "zh-Hans", "zh-Hant", "zh-Hant-HK", "ko")
# The ones a package's language may stand on: Korean stands on English itself (docs/design/korean.md).
FALLBACKS = ("en", "ja", "zh-Hans", "zh-Hant")
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


def names_problem(document, languages: tuple[str, ...] = ()) -> str:
    """dialogue/names.json by the host's rules (src/host/mod_names.hpp parse), "" when it is good.
    `languages`: the package's own reading languages, which it may name too."""
    known = NAME_LOCALES + tuple(languages)
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
                        if locale not in known:
                            return f"{where}：未知语言（{'、'.join(known)}）"
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
                if locale not in known:
                    return f"unit.{locale}：未知语言（{'、'.join(known)}）"
                if why := _name_problem(text, f"unit.{locale}", 24):
                    return why
        elif key in ("pilots", "units", "weapons"):
            if not isinstance(value, dict):
                return f"{key}：要是对象"
            count = {"pilots": 361, "units": 363, "weapons": 1329}[key]
            for number, by_locale in value.items():
                where = f"{key}.{number}"
                new = _reference(number)   # a package's new unit, weapon or character
                if not new and (not _plain_number(number) or int(number) >= count):
                    return f"{where}：编号要是 0–{count - 1} 的十进制整数，或新机体、新武器、新人物的 @名字"
                if key == "pilots" and not new and 25 <= int(number) < 33:
                    return f"{where}：原创主角和搭档的名字写在 heroes 里"
                if not isinstance(by_locale, dict):
                    return f"{where}：要是对象"
                for locale, entry in by_locale.items():
                    if locale not in known:
                        return f"{where}.{locale}：未知语言（{'、'.join(known)}）"
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
        elif key == "models":
            # A new unit's model line in the Library (table_window::model_record).
            if not isinstance(value, dict):
                return "models：要是对象"
            for unit, by_locale in value.items():
                where = f"models.{unit}"
                if not _reference(unit):
                    return f"{where}：要是新机体的 @名字（data/new_units.json 里的）"
                if not isinstance(by_locale, dict):
                    return f"{where}：要是对象"
                for locale, text in by_locale.items():
                    if locale not in known:
                        return f"{where}.{locale}：未知语言（{'、'.join(known)}）"
                    if not isinstance(text, str) or not text:
                        return f"{where}.{locale}：要是非空字符串"
                    if len(text) > 48:
                        return f"{where}.{locale}：太长（最多 48 个字符）"
                    if any(ord(c) < 0x20 or c in "<>{}" for c in text):
                        return f"{where}.{locale}：不能有控制字符或 < > {{ }}"
        else:
            return f"未知字段 {key}（schema、heroes、unit、pilots、units、weapons、models）"
    return ""


SONG_NAME = re.compile(r"(?![0-9]+$)[a-z0-9._-]{1,40}")


def music_file_problem(name: str) -> str | None:
    """audio/music/<song>.ogg (src/host/music.hpp collect): a song number 1-255 replaces that
    song; a name (lowercase, not all digits) is the package's own song, "@name" in music.json."""
    stem, _, ext = name.rpartition(".")
    if "/" in name or ext != "ogg":
        return "要是 audio/music/ 下的 .ogg"
    if stem.isascii() and stem.isdigit():
        return None if 1 <= int(stem) <= 255 and not stem.startswith("0") else "歌号要是 1–255"
    return None if SONG_NAME.fullmatch(stem) else "文件名要是歌号，或自己的曲名（小写字母、数字和 - _ .）"


def music_settings_problem(document, songs: set[str], pilots: set[str], units: set[str]) -> str:
    """audio/music.json: the "@name" songs it names have their files (``songs``), the "@name"
    characters and units are the package's (``pilots``, ``units``); another package's
    ("other@name") is the host's to find."""
    if not isinstance(document, dict) or document.get("schema") != "srw64.music.v1":
        return "schema 要是 srw64.music.v1"
    def song(value, where: str) -> str:
        if isinstance(value, bool) or not (isinstance(value, int) and 1 <= value <= 255 or isinstance(value, str) and value.startswith("@")):
            return f"{where}：要是歌号 1–255 或 @曲名"
        if isinstance(value, str) and value[1:] not in songs:
            return f"{where}：包里没有 audio/music/{value[1:]}.ogg"
        return ""
    for key in document.get("tracks", {}) or {}:
        if key.startswith("@") and (why := song(key, f"tracks.{key}")):
            return why
    battle = document.get("battle", {}) or {}
    for part, own in (("pilot", pilots), ("unit", units)):
        for key, value in (battle.get(part, {}) or {}).items():
            where = f"battle.{part}.{key}"
            if key.startswith("@") and key[1:] not in own:
                return f"{where}：包里没有这个新{'人物' if part == 'pilot' else '机体'}"
            if why := song(value, where):
                return why
    return ""


def voice_problem(rest: str, named: set[str] = frozenset()) -> str | None:
    """A battle voice (audio/voice/battle/<record>.ogg) by the host's rules (src/host/music.hpp
    collect_voices): an Ogg file named by a battle quote's text record, 5799-17346, or one a
    line of the package's dialogue/battle_quotes.json names (``named``)."""
    folder, _, name = rest.rpartition("/")
    if folder != "battle":
        return "暂不支持：战斗语音放在 audio/voice/battle/"
    stem, _, ext = name.rpartition(".")
    if ext.lower() != "ogg":
        return "要是 Ogg Vorbis（.ogg）"
    if name in named:
        return None
    if not (stem.isdigit() and stem.isascii() and not stem.startswith("0")) or not 5799 <= int(stem) <= 17346:
        return "文件名要是战斗台词的记录号（5799–17346），或 dialogue/battle_quotes.json 里写的文件名"
    return None


# dialogue/battle_quotes.json (src/host/mod_quotes.hpp, docs/design/unit-mod-kit.md §7): a new
# character's own battle quotes. The shape is checked here; numbers, weapons and the opponents'
# voice parts are the host's to resolve.
BATTLE_QUOTES_SCHEMA = "srw64.battle-quotes.v1"
QUOTE_SITUATIONS = ("attack", "shot_down", "heavy", "medium", "light", "evade", "no_damage", "no_ammo", "out_of_range")


def battle_quotes_problem(document) -> tuple[str, set[str]]:
    """"" when it is good, and the voice files its lines name."""
    voices: set[str] = set()

    def line_problem(line, where: str) -> str:
        if not isinstance(line, dict):
            return f"{where}：要是对象（各语言的台词，可加 voice）"
        for key, value in line.items():
            if not isinstance(value, str):
                return f"{where}.{key}：要是字符串"
            if key == "voice":
                if not value.lower().endswith(".ogg") or len(value) < 5 or "/" in value or "\\" in value or ".." in value:
                    return f"{where}.voice：要写 audio/voice/battle/ 里的 .ogg 文件名"
                voices.add(value)
            elif not value or len(value) > 200:
                return f"{where}.{key}：台词为空或太长"
        if "ja" not in line:
            return f"{where}：每句都要有 ja"
        if len(line["ja"]) > 42:
            return f"{where}.ja：战斗台词最多 42 字（一页三行）"
        return ""

    if not isinstance(document, dict) or document.get("schema") != BATTLE_QUOTES_SCHEMA:
        return f"schema 要写 {BATTLE_QUOTES_SCHEMA}", voices
    for key in document:
        if key not in ("schema", "pilots"):
            return f"未知字段 {key}（schema、pilots）", voices
    pilots = document.get("pilots")
    if not isinstance(pilots, dict):
        return "pilots 要是对象", voices
    for who, entry in pilots.items():
        where = f"pilots.{who}"
        if "@" not in who:
            return f"{where}：要写包里的新人物（@名字）", voices
        if not isinstance(entry, dict):
            return f"{where}：要是对象", voices
        specials = 0
        for key, value in entry.items():
            if key in QUOTE_SITUATIONS:
                if not isinstance(value, list) or not 1 <= len(value) <= 255:
                    return f"{where}.{key}：要是 1–255 句的数组", voices
                for i, line in enumerate(value):
                    if why := line_problem(line, f"{where}.{key}[{i}]"):
                        return why, voices
            elif key == "special":
                if not isinstance(value, list):
                    return f"{where}.special：要是数组", voices
                for n, c in enumerate(value):
                    at = f"{where}.special[{n}]"
                    if not isinstance(c, dict):
                        return f"{at}：要是对象", voices
                    for k in c:
                        if k not in ("weapon", "opponent", "situation", "sometimes", "lines"):
                            return f"{at}：未知字段 {k}（weapon、opponent、situation、sometimes、lines）", voices
                    if ("weapon" in c) == ("opponent" in c):
                        return f"{at}：weapon 和 opponent 要写一个", voices
                    if "weapon" in c:
                        if "situation" in c:
                            return f"{at}.situation：武器台词总是攻击时", voices
                        if not _weapon_or_new(c["weapon"]):
                            return f"{at}.weapon：没有这件武器", voices
                        if "sometimes" in c and not isinstance(c["sometimes"], bool):
                            return f"{at}.sometimes：要是 true 或 false", voices
                    else:
                        if "sometimes" in c:
                            return f"{at}.sometimes：只有武器台词能写", voices
                        if not _int_in(c["opponent"], 0, 359):
                            return f"{at}.opponent：要写原版人物（0–359）", voices
                        if c.get("situation", "attack") not in QUOTE_SITUATIONS:
                            return f"{at}.situation：要写九种情境之一", voices
                    lines = c.get("lines")
                    if not isinstance(lines, list) or not lines:
                        return f"{at}.lines：要是非空数组", voices
                    for i, line in enumerate(lines):
                        if why := line_problem(line, f"{at}.lines[{i}]"):
                            return why, voices
                    specials += len(lines)
            else:
                return f"{where}：未知字段 {key}（{'、'.join(QUOTE_SITUATIONS)}、special）", voices
        if specials > 119:
            return f"{where}.special：条件台词最多 119 句", voices
    return "", voices


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
    if ext.lower() != "png" or not (_reference(stem) or (_plain_number(stem) and int(stem) < 363)):
        return "文件名要是机体号.png（0–362）或新机体的 @名字.png"
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return "不是 PNG"
    width, height = int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    if width != height or width not in (16, 32, 64, 128):
        return f"{width}×{height}，要是 16、32、64 或 128 像素的正方形"
    return None


def icon_facing_problem(data) -> str | None:
    """art/icons.json (src/host/native_icon.cpp facings): {"schema": "srw64.icons.v1",
    "facing": {"<unit>": "left" | "right" | "front"}}, the way a picture looks when it is not
    the original icon's."""
    if not isinstance(data, dict) or data.get("schema") != "srw64.icons.v1" or not isinstance(data.get("facing"), dict):
        return "要是 {\"schema\": \"srw64.icons.v1\", \"facing\": {…}}"
    for key in data:
        if key not in ("schema", "facing"):
            return f"不认识的字段 {key}"
    for key, value in data["facing"].items():
        if not (_reference(key) or (_plain_number(key) and int(key) < 363)):
            return f"{key} 要是机体号（0–362）或新机体的 @名字"
        if value not in ("left", "right", "front"):
            return f"{key} 的朝向要是 left、right 或 front"
    return None


def _plain_number(text: str) -> bool:
    return text.isdigit() and text.isascii() and len(text) <= 4 and (text == "0" or not text.startswith("0"))


# src/host/mod_data.hpp: each file's fields as (low, high, scale), the special ones by name.
DATA_FIELDS = {
    "units.json": (363, {"hp": (1, 65535, 1), "en": (0, 65535, 1), "move": (1, 30, 1), "mobility": (0, 65535, 1),
                         "armor": (0, 65535, 1), "limit": (0, 65535, 1), "repair_cost": (0, 65535, 1), "upgrade_cap": (0, 15, 1),
                         "swap_class": (0, 8, 1), "funds": (0, 65535, 1), "exp": (0, 255, 1)}),
    "pilots.json": (360, {"melee": (0, 255, 1), "ranged": (0, 255, 1), "evade": (0, 255, 1), "hit": (0, 255, 1),
                          "reaction": (0, 255, 1), "skill": (0, 255, 1), "sp": (0, 255, 1), "double_move": (0, 99, 1), "swap_class": (0, 8, 1)}),
    "weapons.json": (1329, {"power": (0, 25500, 100), "range_min": (0, 30, 1), "range_max": (1, 30, 1), "hit": (-128, 127, 1),
                            "ammo": (-1, 99, 1), "en": (0, 255, 1), "morale": (0, 255, 1), "critical": (-128, 127, 1),
                            "upgrade_type": (0, 4, 1)}),
}
REQUIREMENTS = ("ally", "hyper_mode", "super_mode", "level_10", "level_15", "level_20", "level_25", "level_30", "level_35",
                "level_45", "newtype", "newtype_4", "holy_warrior", "holy_warrior_3", "v_max", "v_max_red")
TERRAIN = ("air", "land", "sea", "space")
MOVE_TYPES = ("land", "air", "sea", "space", "underground")
ABILITIES = ("beam_coat", "i_field", "planet_defensor", "aura_barrier", "afterimage", "mach_special", "true_mach_special",
             "god_shadow", "getter_vision", "shungensoku", "hyper_jammer", "hp_regen_10", "hp_regen_20")
SKILLS = ("potential", "newtype", "enhanced", "holy_warrior", "esp", "parry", "shield_defense")
# The parts with a combine recipe in the original (D_800CAFEC): Combattler V's, Dancouga's.
RECIPE_PARTS = frozenset((*range(197, 206), 219, 220, 221, 225, 226, 227, *range(230, 236)))
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


# data/new_units.json (src/host/mod_units.hpp, docs/design/new-unit-ids.md): the shape and the
# names; the numbers (the host's, past the ROM's) and the layout (weapon lists, animation
# records, inheritance rows) are the host's.
NEW_UNITS_SCHEMA = "srw64.new-units.v2"
NAME = re.compile(r"[a-z0-9._-]{1,40}")
REFERENCE = re.compile(r"([a-z0-9][a-z0-9._-]*)?@[a-z0-9._-]{1,40}")


def _int_in(value, low: int, high: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and low <= value <= high


def _triple(value) -> bool:
    return isinstance(value, list) and len(value) == 3 and all(_int_in(v, 1, 0xFFFF) for v in value)


def _reference(value) -> bool:
    """A package's new unit, weapon or scene by name: "@final" (its own) or "package@final"."""
    return isinstance(value, str) and REFERENCE.fullmatch(value) is not None


def _unit_or_new(value, high: int = 362) -> bool:
    return _int_in(value, 0, high) or _reference(value)


def _weapon_or_new(value) -> bool:
    return _int_in(value, 0, 1328) or _reference(value)


def _picture(value) -> bool:
    """A scene or pose: three resource numbers, or "@sprite"."""
    return _triple(value) or _reference(value)


def _recolor_problem(value) -> str:
    """A recoloured original scene (src/host/mod_units.hpp recolor): {"recolor": battle scene,
    "colors": {"index": "#rrggbb"}}. Whether each index is in the scene's palette the game
    checks when it loads the package (it reads the palette from the ROM)."""
    if set(value) - {"recolor", "colors"}:
        return f"未知字段 {sorted(set(value) - {'recolor', 'colors'})[0]}（recolor、colors）"
    if not _int_in(value.get("recolor"), 0, 1052):
        return ".recolor：要是原版战斗场景号（0–1052）"
    colors = value.get("colors")
    if not isinstance(colors, dict) or not colors:
        return '.colors：要写 {"格号": "#rrggbb"}，至少一格'
    for key, color in colors.items():
        if not re.fullmatch(r"\d{1,3}", key) or int(key) > 255:
            return f".colors：{key} 不是调色板的格号（0–255）"
        if not isinstance(color, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
            return f".colors.{key}：颜色要写 #rrggbb"
    return ""


# Per behaviour, how far it moves its actor to the scene resources around its own, and the
# behaviours that compute with scene numbers otherwise (src/host/battle_steps.hpp, from the
# recompiled code; web/src/data/weapon-animations.json "steps" and "complex").
def _steps() -> tuple[dict[int, tuple[int, int]], set[int]]:
    text = (Path(__file__).resolve().parents[2] / "src/host/battle_steps.hpp").read_text(encoding="utf-8")
    hard = text[text.index("complex[]={"):]
    return ({int(b): (int(lo), int(hi)) for b, lo, hi in re.findall(r"\{(\d+),(\d+),(\d+)\}", text)},
            {int(b) for b in re.findall(r"(\d+),", hard[:hard.index("};")])})


def _reach_problem(document, actor, here: str, complex_record: bool = False) -> str:
    """A picture of this package on an actor whose routine moves to the scene resources next to
    its own needs "around" that far; in a record with a routine that computes with scene numbers
    only a cut-in may have one (the host refuses the package otherwise)."""
    scene = actor.get("scene")
    if not isinstance(scene, str) or not scene.startswith("@"):
        return ""
    target = document.get("scenes", {}).get(scene[1:])
    if not isinstance(target, str) or not target.startswith("@"):
        return ""
    sprite = document.get("sprites", {}).get(target[1:])
    if not isinstance(sprite, dict):
        return ""
    if complex_record and actor.get("behavior") != 240:
        return f"{here}：这条演出里有按场景号计算的行为例程，演员只能用原版的图（cut-in 除外）"
    if actor.get("behavior") not in _STEPS:
        return ""
    before, after = _STEPS[actor["behavior"]]
    has = sprite.get("around", [0, 0])
    if has[0] < before or has[1] < after:
        return f"{here}：行为 {actor['behavior']} 会切到场景前后的资源，图片 {target[1:]} 要写 \"around\": [{before}, {after}]"
    return ""


_STEPS, _COMPLEX = _steps()


def sprites_problem(sprites, files: set[str] | None) -> str:
    """data/new_units.json "sprites" (src/host/mod_units.hpp sprites): PNGs the host turns into
    the game's resources; their colours and size are checked when the game loads them."""
    for name, entry in sprites.items():
        where = f"sprites.{name}"
        if not isinstance(entry, dict):
            return f"{where}：要是对象"
        unknown = set(entry) - {"image", "frames", "origin", "mirror", "ticks", "loop", "hd", "around"}
        if unknown:
            return f"{where}：未知字段 {sorted(unknown)[0]}（image、frames、origin、mirror、ticks、loop、hd、around）"
        if ("image" in entry) == ("frames" in entry):
            return f"{where}：image 和 frames 写一个"
        frames = entry["frames"] if "frames" in entry else [entry["image"]]
        hd = entry.get("hd", [])
        hd = hd if isinstance(hd, list) else [hd]
        if not isinstance(frames, list) or not 1 <= len(frames) <= 64:
            return f"{where}：frames 要 1–64 张"
        if hd and len(hd) != len(frames):
            return f"{where}.hd：要每张图一张 HD 图"
        for path in frames + hd:
            if not isinstance(path, str) or not path.lower().endswith(".png") or path.startswith("/") or ".." in path.split("/"):
                return f"{where}：图片要写包里的 PNG 相对路径"
            if files is not None and path not in files:
                return f"{where}：包里没有 {path}"
        origin = entry.get("origin")
        if not isinstance(origin, list) or len(origin) != 2 or not all(_int_in(v, -2048, 2048) for v in origin):
            return f"{where}.origin：要写 [x, y]"
        if "mirror" in entry and not isinstance(entry["mirror"], bool):
            return f"{where}.mirror：要是 true 或 false"
        ticks = entry.get("ticks", [])
        if not isinstance(ticks, list) or (ticks and len(ticks) != len(frames)) or not all(_int_in(v, 1, 255) for v in ticks):
            return f"{where}.ticks：要每张图一个 1–255 的拍数"
        if not _int_in(entry.get("loop", 0), 0, len(frames) - 1):
            return f"{where}.loop：要是 0 到图数减 1"
        around = entry.get("around", [0, 0])
        if not isinstance(around, list) or len(around) != 2 or not all(_int_in(v, 0, 32) for v in around):
            return f"{where}.around：要写 [前, 后]：自己的场景前后还要几份（0–32）"
    return ""


def new_units_problem(document, files: set[str] | None = None) -> str:
    if not isinstance(document, dict) or document.get("schema") != NEW_UNITS_SCHEMA:
        return f"schema 要是 {NEW_UNITS_SCHEMA}"
    for key in document:
        if key not in ("schema", "sprites", "units", "weapons", "scenes", "pilots"):
            return f"未知分区 {key}（sprites、units、weapons、scenes、pilots）"
    for part in ("sprites", "scenes", "weapons", "units", "pilots"):
        if not isinstance(document.get(part, {}), dict):
            return f"{part} 要是对象"
        for key in document.get(part, {}):
            if not NAME.fullmatch(key):
                return f"{part}.{key}：名字只能用小写字母、数字和 - _ ."
    if why := sprites_problem(document.get("sprites", {}), files):
        return why
    for key, triple in document.get("scenes", {}).items():
        if isinstance(triple, dict):
            if why := _recolor_problem(triple):
                return f"scenes.{key}{why if why.startswith('.') else '：' + why}"
        elif not _picture(triple):
            return f'scenes.{key}：要是 [场景, 图集, 调色板] 三个资源号、@图片名，或 {{"recolor": 场景号, "colors": {{…}}}}'

    for key, entry in document.get("weapons", {}).items():
        where = f"weapons.{key}"
        if not isinstance(entry, dict):
            return f"{where}：要是对象"
        unknown = set(entry) - {"copy", "animation", "inherits_from", "combination"}
        if unknown:
            return f"{where}：未知字段 {sorted(unknown)[0]}（copy、animation、inherits_from、combination）"
        if not _weapon_or_new(entry.get("copy")):
            return f"{where}.copy：要写照抄的武器（原版武器号或 @名字）"
        if "inherits_from" in entry and not _weapon_or_new(entry["inherits_from"]):
            return f"{where}.inherits_from：要是武器号或 @名字"
        # A combination attack (src/host/mod_units.hpp, mod_combos.hpp).
        if "combination" in entry:
            c = entry["combination"]
            if not isinstance(c, dict) or set(c) - {"units", "parts", "multiplier", "pilots"}:
                return f'{where}.combination：要写 {{"units": [2–5 台], "parts": [每台的组成武器], "multiplier": 倍率}}'
            units, parts = c.get("units"), c.get("parts")
            if not isinstance(units, list) or not 2 <= len(units) <= 5 or not all(_unit_or_new(u) for u in units) \
                    or len({json.dumps(u) for u in units}) != len(units):
                return f"{where}.combination.units：要是 2–5 台不重复的机体号或 @名字"
            if not isinstance(parts, list) or len(parts) != len(units) or not all(_weapon_or_new(w) for w in parts):
                return f"{where}.combination.parts：每台参加机体写一件组成武器，顺序和 units 一样"
            m = c.get("multiplier", 1.5)
            if isinstance(m, bool) or not isinstance(m, (int, float)) or not 0.5 <= m <= 5:
                return f"{where}.combination.multiplier：要是 0.5–5 的数"
            pilots = c.get("pilots", [[0]])
            if not isinstance(pilots, list) or not pilots or not all(
                    isinstance(row, list) and 1 <= len(row) <= 4 and all(_int_in(i, 0, 511) or _reference(i) for i in row) for row in pilots):
                return f"{where}.combination.pilots：要写人物组合的数组，每组 1–4 个人物"
        animation = entry.get("animation", {"copy": entry["copy"]})
        if not isinstance(animation, dict):
            return f"{where}.animation：要是 {{\"copy\": 武器}} 或整条记录"
        if "copy" in animation:
            if len(animation) != 1 or not _weapon_or_new(animation["copy"]):
                return f"{where}.animation.copy：要是武器号或 @名字，且不和别的字段同写"
            continue
        unknown = set(animation) - {"camera", "hit", "action", "defender", "sounds", "actors"}
        if unknown:
            return f"{where}.animation：未知字段 {sorted(unknown)[0]}"
        for name in ("camera", "hit", "action", "defender"):
            if not _int_in(animation.get(name), 0, 0x7FFF):
                return f"{where}.animation.{name}：要是整数"
        sounds = animation.get("sounds", [])
        if not isinstance(sounds, list) or len(sounds) > 8 or not all(_int_in(s, -2, 0x7FFF) for s in sounds):
            return f"{where}.animation.sounds：最多 8 个音效号（-2 静音）"
        actors = animation.get("actors", [])
        if not isinstance(actors, list) or not 1 <= len(actors) <= 24:
            return f"{where}.animation.actors：要 1–24 个"
        for k, a in enumerate(actors):
            here = f"{where}.animation.actors[{k}]"
            if not isinstance(a, dict) or not (_int_in(a.get("scene"), 0, 1052) or _reference(a.get("scene"))):
                return f"{here}.scene：要是战斗场景总表的号（0–1052）或 @场景名"
            if not all(_int_in(a.get(n, 0), -32768, 32767) for n in ("x", "y", "z")):
                return f"{here}：x、y、z 要是整数"
            if not all(_int_in(a.get(n, 0), 0, 0xFFFF) for n in ("h4", "h5", "h6")):
                return f"{here}：h4、h5、h6 要是 0–65535"
            if not _int_in(a.get("behavior"), 3, 382):
                return f"{here}.behavior：要是行为例程号（3–382）"
            if why := _reach_problem(document, a, here, any(isinstance(x, dict) and x.get("behavior") in _COMPLEX for x in actors)):
                return why
        if 8 + 2 * len(sounds) + 2 + 16 * len(actors) + 2 > 0x3C0:
            return f"{where}.animation：记录太长"
    for key, entry in document.get("units", {}).items():
        where = f"units.{key}"
        if not isinstance(entry, dict):
            return f"{where}：要是对象"
        unknown = set(entry) - {"copy", "pose", "icon", "weapons", "inherits_from", "crew", "combine", "work", "forms"}
        if unknown:
            return f"{where}：未知字段 {sorted(unknown)[0]}（copy、pose、icon、weapons、inherits_from、crew、combine、work、forms）"
        if "work" in entry and not _int_in(entry["work"], 0, 24):
            return f"{where}.work：要是原版作品号 0–24"
        if not _unit_or_new(entry.get("copy"), 353):
            return f"{where}.copy：要写照抄的机体（原版 0–353 或 @名字）"
        if "pose" in entry and not _picture(entry["pose"]):
            return f"{where}.pose：要是 [场景, 图集, 调色板] 三个资源号，或 @图片名"
        if "icon" in entry and not _int_in(entry["icon"], 688, 1009):
            return f"{where}.icon：要是地图图标资源号（688–1009）"
        weapons_of = entry.get("weapons")
        if not isinstance(weapons_of, list) or not 1 <= len(weapons_of) <= 63 or not all(_weapon_or_new(w) for w in weapons_of):
            return f"{where}.weapons：要是 1–63 件武器（原版武器号或 @名字）的数组"
        if "inherits_from" in entry and not _unit_or_new(entry["inherits_from"]):
            return f"{where}.inherits_from：要是机体号或 @名字"
        if "crew" in entry:
            crew = entry["crew"]
            if not isinstance(crew, list) or not 1 <= len(crew) <= 5:
                return f"{where}.crew：要写 1–5 个人物号，第一个是主驾驶"
            if not all(_int_in(c, 0, 360) for c in crew):
                return f"{where}.crew：人物号是 0–360"
        if "combine" in entry:
            combine = entry["combine"]
            parts = combine.get("parts") if isinstance(combine, dict) else None
            if not isinstance(combine, dict) or set(combine) - {"parts", "over"} or not isinstance(parts, list) \
                    or not 2 <= len(parts) <= 5 or not all(_unit_or_new(p) for p in parts):
                return f"{where}.combine：要写 {{\"parts\": [2–5 台部件], \"over\": [原版合体机]}}"
            over = combine.get("over", [])
            if not isinstance(over, list) or not all(_int_in(o, 0, 362) for o in over):
                return f"{where}.combine.over：要是原版合体机的机体号"
            # Without `over` each part gets a recipe of its own (src/host/mod_combine.hpp
            # recipe_parts); a part with one in the original (D_800CAFEC) keeps the original's.
            taken = [p for p in parts if isinstance(p, int) and p in RECIPE_PARTS]
            if not over and taken:
                return f"{where}.combine.parts：{taken[0]} 原版已经有合体配方，要用 over 写它合出的原版合体机"
        # Form families (src/host/mod_units.hpp, mod_forms.hpp): other new units of this file.
        if "forms" in entry:
            forms = entry["forms"]
            units = document.get("units", {})
            if not isinstance(forms, list) or not 1 <= len(forms) <= 2 or not all(
                    isinstance(f, str) and f.startswith("@") and f[1:] in units and f[1:] != key for f in forms) \
                    or len(set(forms)) != len(forms):
                return f"{where}.forms：要写 1–2 个别的形态（同一文件里的新机体 @名字）"
    seen_forms: set[str] = set()
    for key, entry in document.get("units", {}).items():
        if isinstance(entry, dict) and "forms" in entry:
            for name in [key] + [f[1:] for f in entry["forms"]]:
                if name in seen_forms:
                    return f"units.{key}.forms：一台机体只能在一个变形族里"
                seen_forms.add(name)
    # New characters (src/host/mod_units.hpp, docs/design/unit-mod-kit.md §5.4). Whether the
    # copied character has an ability record only the game knows.
    for key, entry in document.get("pilots", {}).items():
        where = f"pilots.{key}"
        if not NAME.fullmatch(key):
            return f"{where}：名字只能用小写字母、数字和 - _ ."
        if not isinstance(entry, dict):
            return f"{where}：要是对象"
        unknown = set(entry) - {"copy", "voice", "portrait", "love", "combo_as"}
        if unknown:
            return f"{where}：未知字段 {sorted(unknown)[0]}（copy、voice、portrait、love、combo_as）"
        if "love" in entry:
            love = entry["love"]
            if not isinstance(love, list) or not love or not all(
                    isinstance(row, dict) and "with" in row and not set(row) - {"with", "mutual"}
                    and isinstance(row.get("mutual", False), bool) for row in love):
                return f'{where}.love：要写 [{{"with": 人物号或 @名字, "mutual": true/false}}]'
            if not all(_int_in(row["with"], 0, 360) or _reference(row["with"]) for row in love):
                return f"{where}.love.with：要是人物号（0–360）或新人物的 @名字"
        if "combo_as" in entry and not _int_in(entry["combo_as"], 0, 359):
            return f"{where}.combo_as：要写原版人物号（0–359）"
        if not _int_in(entry.get("copy"), 0, 359):
            return f"{where}.copy：要写照抄的原版人物（0–359，有能力记录的）"
        if "voice" in entry and not _int_in(entry["voice"], 0, 359):
            return f"{where}.voice：要写借台词的原版人物（0–359，有能力记录的）"
        portrait = entry.get("portrait")
        if isinstance(portrait, dict):
            # The package's own: a 96×96 picture (up to 63 colours, checked by the game) and an HD one.
            if set(portrait) - {"image", "hd"} or "image" not in portrait:
                return f"{where}.portrait：要写 {{\"image\": 96×96 的 PNG, \"hd\": 高清 PNG}}"
            for key in ("image", "hd"):
                path = portrait.get(key)
                if path is None:
                    continue
                if not isinstance(path, str) or not path.lower().endswith(".png") or path.startswith("/") or ".." in path.split("/"):
                    return f"{where}.portrait.{key}：要写包里的 PNG 相对路径"
                if files is not None and path not in files:
                    return f"{where}.portrait.{key}：包里没有 {path}"
        elif portrait is not None and (not isinstance(portrait, list) or len(portrait) != 2 or not all(_int_in(v, 1, 0xFFFF) for v in portrait)):
            return f"{where}.portrait：要是头像的两个资源号 [图, 调色板]，或 {{\"image\": …, \"hd\": …}}"
    return ""


def data_problem(name: str, document) -> str:
    """data/<name> by the host's rules (src/host/mod_data.hpp parse), "" when it is good. Whether a
    pilot has an ability or spirit record, and the ROM's own min/max range, only the game knows."""
    count, fields = DATA_FIELDS[name]
    if not isinstance(document, dict):
        return "顶层要是对象"
    for key, entry in document.items():
        new = _reference(key)   # a package's new unit, weapon or character
        if not new and (not _plain_number(key) or int(key) >= count):
            return f"{key}：编号要是 0–{count - 1} 的十进制整数，或新机体、新武器、新人物的 @名字"
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
            elif name == "weapons.json" and field in ("post_move", "beam", "parryable", "locked"):
                if not isinstance(value, bool):
                    return f"{where}：要是 true 或 false"
            elif name == "weapons.json" and field == "attack":
                if value not in ("melee", "ranged"):
                    return f"{where}：要是 melee 或 ranged"
            elif name == "weapons.json" and field == "terrain_at":
                if value not in ("attacker", "target"):
                    return f"{where}：要是 attacker 或 target"
            elif name == "weapons.json" and field == "requires":
                if value is not None and value not in REQUIREMENTS:
                    return f"{where}：未知条件 {value!r}（null、{'、'.join(REQUIREMENTS)}）"
            elif name == "units.json" and field == "size":
                if value not in ("SS", "S", "M", "L", "LL"):
                    return f"{where}：要是 SS、S、M、L 或 LL"
            elif name == "units.json" and field == "move_types":
                if not isinstance(value, list) or not value or any(v not in MOVE_TYPES for v in value):
                    return f"{where}：要是非空数组，每项是 {'、'.join(MOVE_TYPES)} 之一"
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
        deflated = [e.name for e in package.entries if e.deflated and Path(e.name).suffix.lower() in STORED]
        if deflated:
            summary["warnings"] = [f"{len(deflated)} picture or sound file(s) are deflated; store them (zip -0, "
                                   f"tools/mods/pack.py does): {', '.join(deflated[:3])}{', …' if len(deflated) > 3 else ''}"]
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
        if not isinstance(image, dict) or image.get("schema") not in CAMPAIGN_IMAGE_SCHEMAS:
            problems.append(Problem("campaign", "not " + " / ".join(CAMPAIGN_IMAGE_SCHEMAS)))
        elif image.get("id") != m["id"]:
            problems.append(Problem("campaign", f"id {image.get('id')!r}, the package is {m['id']!r}"))
    named_voices: set[str] = set()
    for e in package.entries:
        if e.name != "dialogue/battle_quotes.json":
            continue
        try:
            why, named_voices = battle_quotes_problem(json.loads(e.read().decode("utf-8")))
        except (ValueError, UnicodeDecodeError) as error:
            why = f"JSON 无效：{error}"
        if why:
            problems.append(Problem("quotes", f"{e.name}: {why}"))
        present = {x.name for x in package.entries}
        for name in sorted(named_voices):
            if f"audio/voice/battle/{name}" not in present:
                problems.append(Problem("quotes", f"{e.name}: 没有 audio/voice/battle/{name}"))
    # The events of a campaign's stages that the host runs (tools/mods/stage.py).
    for e in package.entries:
        if e.name != "campaign/story.json":
            continue
        try:
            image = json.loads(e.read().decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as error:
            problems.append(Problem("campaign", f"story.json: {error}"))
            continue
        if not isinstance(image, dict) or image.get("schema") != "srw64.story-image.v1" or image.get("campaign") != m["id"]:
            problems.append(Problem("campaign", f"story.json is not this campaign's story image ({m['id']!r})"))
    for e in package.entries:
        top, _, rest = e.name.partition("/")
        if top == "data" and rest in DATA_FIELDS:
            try:
                why = data_problem(rest, json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("data", f"{e.name}: {why}"))
        elif top == "data" and rest == "new_units.json":
            try:
                why = new_units_problem(json.loads(e.read().decode("utf-8")), {x.name for x in package.entries})
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
            why = voice_problem(e.name[len("audio/voice/"):], named_voices)
            if why:
                problems.append(Problem("voice", f"{e.name}: {why}"))
        elif e.name.startswith("audio/music/"):
            if why := music_file_problem(e.name[len("audio/music/"):]):
                problems.append(Problem("music", f"{e.name}: {why}"))
        elif e.name == "audio/music.json":
            songs = {x.name[len("audio/music/"):-4] for x in package.entries if x.name.startswith("audio/music/") and x.name.endswith(".ogg")}
            new_units = next((x for x in package.entries if x.name == "data/new_units.json"), None)
            try:
                units_doc = json.loads(new_units.read().decode("utf-8")) if new_units else {}
                why = music_settings_problem(json.loads(e.read().decode("utf-8")), songs,
                                             set((units_doc.get("pilots") or {})), set((units_doc.get("units") or {})))
            except (ValueError, UnicodeDecodeError, AttributeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("music", f"{e.name}: {why}"))
        elif e.name == "audio/voice.json":
            try:
                why = voice_settings_problem(json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("voice", f"{e.name}: {why}"))
        elif e.name == "art/icons.json":
            try:
                why = icon_facing_problem(json.loads(e.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("icon", f"{e.name}: {why}"))
        elif e.name.startswith("art/icons/") and e.name.count("/") == 2:
            why = icon_problem(e.name.rsplit("/", 1)[1], e.read())
            if why:
                problems.append(Problem("icon", f"{e.name}: {why}"))
    language_problems, summary["languages"] = languages_of(package.entries)
    problems += language_problems
    for e in package.entries:
        if e.name == "dialogue/names.json":
            try:
                why = names_problem(json.loads(e.read().decode("utf-8")), tuple(summary["languages"]))
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
            if other == "srw64.hd":   # built in (mod_library.hpp builtin_hd), not a package
                continue
            if other not in listed and other not in also_listed:
                problems.append(Problem("missing", other))
    if owners is not None:
        prefix = m["id"].split(".", 1)[0]
        if prefix not in owners:
            problems.append(Problem("no_owner", prefix))
        elif owner is not None and owners[prefix] != owner:
            problems.append(Problem("owner", f"{prefix}: {owners[prefix]}"))
    return problems, summary


# --- Language packages (src/host/mod_languages.hpp, docs/design/mod-language-packs.md) -----

LANGUAGE_SCHEMA = "srw64.language.v1"
TERMS_SCHEMA = "srw64.language-terms.v1"
LOCALE_TAG = re.compile(r"[a-z]{2,3}(-[A-Za-z0-9]{2,8})*")
MAX_FONT = 32 << 20
MAX_LANGUAGES = 16
FIRST_DIALOGUE_RECORD = 5644
RECORD_KEY = re.compile(r"base:t00_([0-9]{5})")


def language_problem(data, folder: str, names: dict[str, "Entry"]) -> str:
    """language.json in dialogue/<folder>/ by the host's rules (parse_language), "" when it is good."""
    if not isinstance(data, dict):
        return "要是对象"
    for key in data:
        if key not in ("schema", "locale", "name", "fallback", "fonts"):
            return f"未知字段 {key}"
    if data.get("schema") != LANGUAGE_SCHEMA:
        return f"schema 要是 {LANGUAGE_SCHEMA}"
    if data.get("locale") != folder:
        return "locale 要和文件夹名一样"
    name = data.get("name")
    if not isinstance(name, str) or not 1 <= len(name) <= 24:
        return "name 要有 1–24 个字"
    if "fallback" in data and data["fallback"] not in FALLBACKS:
        return "fallback 要是 ja、zh-Hans、zh-Hant、en 之一"
    fonts = data.get("fonts", {})
    if not isinstance(fonts, dict):
        return "fonts 要是对象"
    listed = []
    for key, value in fonts.items():
        if key == "text":
            if not isinstance(value, list) or len(value) > 4:
                return "fonts.text 要是数组（最多 4 个）"
            listed += [("fonts.text", v) for v in value]
        elif key == "title":
            listed.append(("fonts.title", value))
        else:
            return f"fonts 的未知字段 {key}"
    for where, value in listed:
        if not isinstance(value, str):
            return f"{where}：要是字符串"
        if not re.fullmatch(r"fonts/[^/]+\.(ttf|otf|TTF|OTF)", value):
            return f"{where}：{value}：只能是 fonts/ 里的 .ttf 或 .otf"
        entry = names.get(f"dialogue/{folder}/{value}")
        if entry is None:
            return f"{where}：{value}：没有这个文件"
        if entry.size > MAX_FONT:
            return f"{where}：{value}：超过 32 MB"
    if not LOCALE_TAG.fullmatch(folder) or len(folder) > 35:
        return "语言代码要像 ko、fr、pt-BR"
    return ""


def terms_problem(data) -> str:
    """terms.json's shape (parse_terms); the records are checked against the ROM in the game."""
    if not isinstance(data, dict) or data.get("schema") != TERMS_SCHEMA:
        return f"schema 要是 {TERMS_SCHEMA}"
    for key in data:
        if key not in ("schema", "ui", "entries"):
            return f"未知字段 {key}"
    ui, entries = data.get("ui", {}), data.get("entries", {})
    if not isinstance(ui, dict) or not isinstance(entries, dict):
        return "ui 和 entries 要是对象"
    for key, text in ui.items():
        if not isinstance(text, str) or not text:
            return f"ui.{key}：要是非空字符串"
    for key, value in entries.items():
        m = RECORD_KEY.fullmatch(key)
        if not m or int(m.group(1)) >= FIRST_DIALOGUE_RECORD:
            return f"entries.{key}：只能是 base:t00_00000–base:t00_05643，台词写进 .txt"
        text = value.get("text") if isinstance(value, dict) else value
        if not isinstance(text, str) or not text.endswith("<END>"):
            return f"entries.{key}：要是以 <END> 结尾的字符串，或带 text 的对象"
    return ""


def languages_of(entries: list["Entry"]) -> tuple[list[Problem], list[str]]:
    """Every dialogue/<locale>/ folder by the host's rules, and the new reading languages."""
    names = {e.name: e for e in entries}
    folders = sorted({e.name.split("/")[1] for e in entries if e.name.startswith("dialogue/") and e.name.count("/") >= 2})
    problems: list[Problem] = []
    languages: list[str] = []
    for folder in folders:
        where = f"dialogue/{folder}/"
        declared = names.get(where + "language.json")
        # Korean was a package's language in 0.5.3 (the demo package declares it); since it is
        # ours the game leaves the declaration unread and lays the files over ours.
        if folder in NAME_LOCALES and not (folder == "ko" and declared):
            if declared:
                problems.append(Problem("language", where + "language.json：内置语言不用声明"))
        elif not declared:
            problems.append(Problem("language", where + "：不是内置语言，要有 language.json"))
            continue
        else:
            try:
                why = language_problem(json.loads(declared.read().decode("utf-8")), folder, names)
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("language", where + "language.json：" + why))
            else:
                languages.append(folder)
        if (terms := names.get(where + "terms.json")) is not None:
            try:
                why = terms_problem(json.loads(terms.read().decode("utf-8")))
            except (ValueError, UnicodeDecodeError) as error:
                why = f"JSON 无效：{error}"
            if why:
                problems.append(Problem("language", where + "terms.json：" + why))
    if len(languages) > MAX_LANGUAGES:
        problems.append(Problem("language", f"最多 {MAX_LANGUAGES} 种语言"))
    for e in entries:
        if Path(e.name).suffix.lower() in (".ttf", ".otf") and not re.fullmatch(r"dialogue/[^/]+/fonts/[^/]+", e.name):
            problems.append(Problem("file_type", e.name))
    return problems, languages


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
        for warning in summary.get("warnings", []):
            print(f"warning: {warning}")
        if not problems or args.summary:
            print(json.dumps(summary, ensure_ascii=False, indent=2) if args.summary else f"ok: {summary.get('id')} {summary.get('version')}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
