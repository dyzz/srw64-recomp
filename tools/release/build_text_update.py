#!/usr/bin/env python3
"""The text update: the whole text, file by file, for every release's game.

docs/design/text-updates.md. The game's update check reads /latest.json; its `text` section
(web/src/data/text-update.json) names the newest list, web/public/text/text-r<N>.json:

  {"schema": "srw64.text-update.v1", "revision": N, "release": "0.5.3", "date", "commit",
   "notes": {zh, tw, en, ja}, "files": {path: {"hash": 12 hex digits, "bytes": size}}}

and each file is web/public/text/files/<hash>.z, zlib-compressed. The paths:

  dialogue/<locale>/<file>.txt   a dialogue file, whole
  locales/<locale>.json          {"entries": {key: target}, "ui": {key: text}}: the locale's
                                 data-text entries and UI labels, all of them (the game takes
                                 only the labels it has)
  library-names.json             the Library's names

Every file is listed, whatever any release shipped: a game downloads those whose hash differs
from the copy it installed before, so after the first time only what changed. `release` is the
newest release the commit holds: a game newer than that ships newer text and leaves the list.

Everything is read from a commit (HEAD by default), so the list is the text as committed.
Before writing, the commit's dialogue files load with no problems and its locale catalogs
compile against the ROM's records (the checks the game and the importer make).

    .venv/bin/python tools/release/build_text_update.py --notes notes.json

notes.json: {"zh": "…", "tw": "…", "en": "…", "ja": "…"}, one line for the About page.
Then deploy the website (docs/guide/release.md, "Text only").
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

SCHEMA = "srw64.text-update.v1"
SITE = "https://srw64.dreamquest.club"
DATA = ROOT / "web/src/data/text-update.json"
PUBLIC = ROOT / "web/public/text"
PROFILE = "config/recomp/profiles/play-profile.json"
DIALOGUE = "content/dialogue"
LIBRARY = "content/locales/library-names.json"
NOTE_LANGUAGES = ("zh", "tw", "en", "ja")
# A file's name and check: the first hex digits of its SHA-256. About 1200 files, so 48
# bits leave no collision to fear; the list itself is checked whole (its sha256 in latest.json).
HASH_DIGITS = 12
# Lists kept on the website: the newest, and the one before for a game that read it just
# before the deploy. Their files stay; the rest go.
KEPT = 2


def git(*args: str, data: bool = False):
    result = subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True)
    return result.stdout if data else result.stdout.decode()


def blobs(revision: str, prefix: str) -> dict[str, str]:
    """{path: blob id} for the files under `prefix` at `revision`."""
    listed = git("ls-tree", "-r", "-z", revision, "--", prefix)
    found = {}
    for row in listed.split("\0"):
        if row:
            meta, path = row.split("\t", 1)
            found[path] = meta.split()[2]
    return found


def show(revision: str, path: str) -> bytes:
    return git("show", f"{revision}:{path}", data=True)


def version_key(version: str) -> tuple[int, ...]:
    return tuple(int(part) for part in version.split("."))


def short_hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:HASH_DIGITS]


def release(commit: str) -> str:
    """The newest release tag the commit holds, as a version."""
    tag = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*.[0-9]*.[0-9]*", commit).strip()
    return tag[1:]


def text_files(revision: str) -> dict[str, bytes]:
    """{path in the list: bytes} for the text at `revision`."""
    profile = json.loads(show(revision, PROFILE))
    files: dict[str, bytes] = {}
    for path in blobs(revision, DIALOGUE):
        parts = path.split("/")
        if len(parts) >= 4 and parts[2] in profile["locales"] and path.endswith(".txt"):
            files["dialogue/" + "/".join(parts[2:])] = show(revision, path)
    ja = json.loads(show(revision, profile["locales"]["ja"]))
    for locale, source in profile["locales"].items():
        catalog = json.loads(show(revision, source))
        # The labels as the game sees them: Japanese under the locale's own.
        slim = {"entries": dict(sorted((row["key"], row["target"]) for row in catalog["entries"])),
                "ui": dict(sorted({**ja["ui"], **catalog["ui"]}.items()))}
        files[f"locales/{locale}.json"] = (json.dumps(slim, ensure_ascii=False, indent=1) + "\n").encode()
    try:
        files["library-names.json"] = show(revision, LIBRARY)
    except subprocess.CalledProcessError:
        pass
    return files


def check(commit: str, rom: Path) -> None:
    """The commit's text as the game would read it: no problem anywhere, or no update."""
    from srw64_native.catalog import compile_locale, source_catalog
    from srw64_native.dialogue_text import load
    sources, hashes, _ = source_catalog(ROOT, rom)
    profile = json.loads(show(commit, PROFILE))
    problems = []
    with tempfile.TemporaryDirectory() as scratch:
        tree = Path(scratch)
        for path in blobs(commit, DIALOGUE):
            target = tree / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(show(commit, path))
        for locale in profile["locales"]:
            _, _, found = load([tree / DIALOGUE / locale], sources)
            problems += [f"{locale}/{problem}" for problem in found]
    for locale, source in profile["locales"].items():
        try:
            compile_locale(json.loads(show(commit, source)), sources, hashes)
        except ValueError as error:
            problems.append(f"{locale}: {error}")
    if problems:
        raise SystemExit("The text has problems; no update:\n  " + "\n  ".join(map(str, problems[:40])))


def build(commit: str, revision: int, date: str, notes: dict) -> tuple[bytes, dict[str, bytes]]:
    """The list's bytes and {hash: compressed file}."""
    rows, stored = {}, {}
    for path, data in sorted(text_files(commit).items()):
        digest = short_hash(data)
        rows[path] = {"hash": digest, "bytes": len(data)}
        stored[digest] = zlib.compress(data, 9)
    about = {"schema": SCHEMA, "revision": revision, "release": release(commit), "date": date,
             "commit": git("rev-parse", commit).strip(), "notes": notes, "files": rows}
    return (json.dumps(about, ensure_ascii=False, indent=1) + "\n").encode(), stored


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--commit", default="HEAD", help="the text to list (default HEAD)")
    parser.add_argument("--notes", type=Path, help='JSON {"zh","tw","en","ja"}: the one-line note the About page shows')
    parser.add_argument("--revision", type=int, help="default: one more than the listed one, else 1")
    parser.add_argument("--date", default=datetime.date.today().isoformat())
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64", help="the JP ROM, for the checks")
    parser.add_argument("--dry-run", action="store_true", help="list what would go in, write nothing")
    args = parser.parse_args()
    if not args.rom.is_file():
        raise SystemExit(f"No ROM at {args.rom}: the checks need it")
    check(args.commit, args.rom)
    if args.dry_run:
        files = text_files(args.commit)
        print(f"{len(files)} files, {sum(map(len, files.values()))} bytes, for releases up to {release(args.commit)}")
        return 0
    if not args.notes:
        raise SystemExit("--notes is required")
    notes = json.loads(args.notes.read_text(encoding="utf-8"))
    if set(notes) != set(NOTE_LANGUAGES) or not all(isinstance(v, str) and v.strip() for v in notes.values()):
        raise SystemExit(f"--notes needs a line for each of {', '.join(NOTE_LANGUAGES)}")
    listed = json.loads(DATA.read_text()) if DATA.exists() else {}
    revision = args.revision or listed.get("revision", 0) + 1
    data, stored = build(args.commit, revision, args.date, notes)
    (PUBLIC / "files").mkdir(parents=True, exist_ok=True)
    name = f"text-r{revision}.json"
    (PUBLIC / name).write_bytes(data)
    for digest, packed in stored.items():
        (PUBLIC / "files" / f"{digest}.z").write_bytes(packed)
    # The newest lists stay, with every file one of them names.
    lists = sorted(PUBLIC.glob("text-r*.json"), key=lambda path: int(path.stem.removeprefix("text-r")), reverse=True)
    kept = set()
    for path in lists[:KEPT]:
        kept |= {row["hash"] for row in json.loads(path.read_text(encoding="utf-8"))["files"].values()}
    for path in lists[KEPT:]:
        path.unlink()
    for path in (PUBLIC / "files").glob("*.z"):
        if path.stem not in kept:
            path.unlink()
    DATA.write_text(json.dumps({"revision": revision, "date": args.date, "url": f"{SITE}/text/{name}", "bytes": len(data),
                                "sha256": hashlib.sha256(data).hexdigest()}, indent=2) + "\n")
    packed = sum(len(value) for value in stored.values())
    about = json.loads(data)
    print(f"{PUBLIC / name}: r{revision}, {len(about['files'])} files ({packed} bytes compressed), for releases up to {about['release']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
