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
