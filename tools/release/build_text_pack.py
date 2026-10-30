#!/usr/bin/env python3
"""A text pack: the text that changed since a release, for that release's game to download.

docs/design/text-updates.md. The game's update check reads /latest.json; its `text` section
(web/src/data/text-packs.json) names one pack per release, and a game of that release
downloads it, checks it and lays it over the text it shipped with. A pack holds, compared
with the release's tag:

  dialogue/<locale>/<file>.txt   every dialogue file that changed or is new, whole
  locales/<locale>.json          {"entries": {key: target}, "ui": {key: text}}: data-text
                                 entries that changed or are new, and UI labels that changed,
                                 only keys the release has (a new feature's labels wait for
                                 the release that has the feature)
  library-names.json             the Library's names, whole, when they changed
  text.json                      what the pack is (schema srw64.text-pack.v1)

Everything is read from a commit (HEAD by default), so the pack is the text as committed and
the same commit gives the same zip. Before writing, the commit's dialogue files load with no
problems, and its locale catalogs compile against the ROM's records (the checks the game and
the importer make).

    .venv/bin/python tools/release/build_text_pack.py --base v0.5.3 --notes notes.json

notes.json: {"zh": "…", "tw": "…", "en": "…", "ja": "…"}, one line for the About page.
The zip goes to web/public/text/<base>/ and its entry to web/src/data/text-packs.json; then
deploy the website (docs/guide/release.md, "Text only").
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

SCHEMA = "srw64.text-pack.v1"
SITE = "https://srw64.dreamquest.club"
PACKS = ROOT / "web/src/data/text-packs.json"
PUBLIC = ROOT / "web/public/text"
PROFILE = "config/recomp/profiles/play-profile.json"
DIALOGUE = "content/dialogue"
LIBRARY = "content/locales/library-names.json"
NOTE_LANGUAGES = ("zh", "tw", "en", "ja")
# Packs are made for this many releases, the newest; an older game is told to update.
KEPT = 2
# A fixed time in every zip entry, so a commit gives the same bytes.
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


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


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def changes(base: str, commit: str) -> dict:
    """What the pack holds: {path in the zip: bytes} and the counts for text.json."""
    old_profile = json.loads(show(base, PROFILE))
    new_profile = json.loads(show(commit, PROFILE))
    locales = [locale for locale in old_profile["locales"] if locale in new_profile["locales"]]
    files: dict[str, bytes] = {}
    counts: dict[str, dict] = {}
    old_dialogue, new_dialogue = blobs(base, DIALOGUE), blobs(commit, DIALOGUE)
    for path, blob in sorted(new_dialogue.items()):
        parts = path.split("/")
        if len(parts) < 4 or parts[2] not in locales or not path.endswith(".txt"):
            continue
        if old_dialogue.get(path) != blob:
            files["dialogue/" + "/".join(parts[2:])] = show(commit, path)
            counts.setdefault(parts[2], {}).setdefault("dialogue_files", 0)
            counts[parts[2]]["dialogue_files"] += 1
    old_ja = json.loads(show(base, old_profile["locales"]["ja"]))
    new_ja = json.loads(show(commit, new_profile["locales"]["ja"]))
    for locale in locales:
        old = json.loads(show(base, old_profile["locales"][locale]))
        new = json.loads(show(commit, new_profile["locales"][locale]))
        before = {row["key"]: row["target"] for row in old["entries"]}
        entries = {row["key"]: row["target"] for row in new["entries"] if before.get(row["key"]) != row["target"]}
        # The labels as the game sees them: Japanese under the locale's own.
        old_ui, new_ui = {**old_ja["ui"], **old["ui"]}, {**new_ja["ui"], **new["ui"]}
        ui = {key: new_ui[key] for key in sorted(old_ui) if key in new_ui and new_ui[key] != old_ui[key]}
        if entries or ui:
            files[f"locales/{locale}.json"] = (json.dumps({"entries": dict(sorted(entries.items())), "ui": ui},
                                                          ensure_ascii=False, indent=1) + "\n").encode()
            counts.setdefault(locale, {}).update({"entries": len(entries), "ui": len(ui)})
    try:
        old_library = show(base, LIBRARY)
    except subprocess.CalledProcessError:
        old_library = b""
    new_library = show(commit, LIBRARY)
    if new_library != old_library:
        files["library-names.json"] = new_library
    return {"files": files, "counts": counts, "locales": locales}


def check(commit: str, rom: Path, pack: dict) -> None:
    """The commit's text as the game would read it: no problem anywhere, or no pack."""
    from srw64_native.catalog import compile_locale, signature, source_catalog
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
        for locale in pack["locales"]:
            _, _, found = load([tree / DIALOGUE / locale], sources)
            problems += [f"{locale}/{problem}" for problem in found]
    for locale in pack["locales"]:
        try:
            compile_locale(json.loads(show(commit, profile["locales"][locale])), sources, hashes)
        except ValueError as error:
            problems.append(f"{locale}: {error}")
        raw = pack["files"].get(f"locales/{locale}.json")
        if raw:
            for key, target in json.loads(raw)["entries"].items():
                if key not in sources or signature(target) != signature(sources[key]):
                    problems.append(f"{locale}: {key} does not fit its record")
    if problems:
        raise SystemExit("The text has problems; no pack:\n  " + "\n  ".join(map(str, problems[:40])))


def zipped(files: dict[str, bytes]) -> bytes:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, files[name], compresslevel=9)
    return out.getvalue()


def build(base_tag: str, commit: str, revision: int, date: str, notes: dict, rom: Path | None) -> tuple[str, bytes, dict]:
    if not re.fullmatch(r"v\d+\.\d+\.\d+", base_tag):
        raise SystemExit(f"--base takes a release tag such as v0.5.3, not {base_tag}")
    base = base_tag[1:]
    pack = changes(base_tag, commit)
    if not pack["files"]:
        raise SystemExit(f"No text changed since {base_tag}")
    if rom:
        check(commit, rom, pack)
    files = pack["files"]
    digests = {name: sha(data) for name, data in sorted(files.items())}
    about = {"schema": SCHEMA, "base": base, "revision": revision, "date": date,
             "commit": git("rev-parse", commit).strip(), "notes": notes, "counts": pack["counts"],
             "files": digests, "content_sha256": sha(json.dumps(digests, sort_keys=True).encode())}
    files = {**files, "text.json": (json.dumps(about, ensure_ascii=False, indent=2) + "\n").encode()}
    return f"Marchwind64-Text-{base}-r{revision}.zip", zipped(files), about


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", required=True, help="the release the pack is for, its tag (v0.5.3)")
    parser.add_argument("--commit", default="HEAD", help="the text to pack (default HEAD)")
    parser.add_argument("--notes", type=Path, help='JSON {"zh","tw","en","ja"}: the one-line note the About page shows')
    parser.add_argument("--revision", type=int, help="default: one more than the listed pack for this release, else 1")
    parser.add_argument("--date", default=datetime.date.today().isoformat())
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64", help="the JP ROM, for the checks")
    parser.add_argument("--dry-run", action="store_true", help="list what would go in, write nothing")
    args = parser.parse_args()
    if not args.rom.is_file():
        raise SystemExit(f"No ROM at {args.rom}: the checks need it")
    if args.dry_run:
        pack = changes(args.base, args.commit)
        check(args.commit, args.rom, pack)
        print(json.dumps({"files": sorted(pack["files"]), "counts": pack["counts"]}, ensure_ascii=False, indent=2))
        return 0
    if not args.notes:
        raise SystemExit("--notes is required")
    notes = json.loads(args.notes.read_text(encoding="utf-8"))
    if set(notes) != set(NOTE_LANGUAGES) or not all(isinstance(v, str) and v.strip() for v in notes.values()):
        raise SystemExit(f"--notes needs a line for each of {', '.join(NOTE_LANGUAGES)}")
    listed = json.loads(PACKS.read_text()) if PACKS.exists() else {}
    base = args.base[1:]
    revision = args.revision or listed.get(base, {}).get("revision", 0) + 1
    name, data, about = build(args.base, args.commit, revision, args.date, notes, args.rom)
    folder = PUBLIC / base
    if folder.exists():
        shutil.rmtree(folder)  # earlier revisions: the game only takes the newest
    folder.mkdir(parents=True)
    (folder / name).write_bytes(data)
    listed[base] = {"revision": revision, "date": args.date, "name": name, "bytes": len(data), "sha256": sha(data),
                    "url": f"{SITE}/text/{base}/{name}", "notes": notes}
    kept = sorted(listed, key=version_key, reverse=True)[:KEPT]
    for old in set(listed) - set(kept):
        shutil.rmtree(PUBLIC / old, ignore_errors=True)
    PACKS.write_text(json.dumps({key: listed[key] for key in sorted(kept, key=version_key)}, ensure_ascii=False, indent=2) + "\n")
    print(f"{folder / name}: {len(data)} bytes, r{revision}, " +
          ", ".join(f"{locale} {row}" for locale, row in about["counts"].items()) +
          (", library names" if "library-names.json" in about["files"] else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
