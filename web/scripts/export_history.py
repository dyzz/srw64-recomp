#!/usr/bin/env python3
"""Export the translation history for the website's history pages
(web/src/pages/[lang]/story/history).

    .venv/bin/python web/scripts/export_history.py [--baseline TAG]

Every commit since the latest game release (the tag in web/src/data/release.json, or
--baseline) that changed the translation of at least one line or term, oldest first:
the story lines (with their scene), battle quotes (with their speaker) and other
dialogue whose Chinese or English text changed, and the term entries (names of people,
units, weapons, places...) whose translation changed. It reads committed files only
(git cat-file), so edits not committed yet never show. Run export_story.py first: the
scene titles come from its index. Writes web/.data/history/index.json and one
<commit>.json per commit.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import export_story as story  # noqa: E402  (pages, strip_codes, parse, ROOT, LOCALES)

ROOT = story.ROOT
OUT = ROOT / "web/.data/history"
PATHS = ["content/dialogue", "content/locales"]
LANG_OF = {locale: code for code, locale in story.LOCALES.items()}   # zh-Hans -> zh


class Blobs:
    """git cat-file --batch: files at a revision, or None where the file did not exist."""

    def __init__(self):
        self.proc = subprocess.Popen(["git", "-C", str(ROOT), "cat-file", "--batch"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def read(self, rev: str, path: str) -> str | None:
        self.proc.stdin.write(f"{rev}:{path}\n".encode())
        self.proc.stdin.flush()
        header = self.proc.stdout.readline().decode().split()
        if len(header) < 3 or header[1] == "missing":
            return None
        data = self.proc.stdout.read(int(header[2]))
        self.proc.stdout.read(1)  # the newline after the object
        return data.decode()


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def entries(text: str | None, path: str) -> dict:
    if text is None:
        return {}
    parsed, problems = story.parse(text, path)
    return {} if problems else {e.key: e for e in parsed}


def terms(text: str | None) -> dict[str, str]:
    if text is None:
        return {}
    return {e["key"]: story.strip_codes(e.get("target") or "") for e in json.loads(text).get("entries", [])}


def text_id(key: str) -> int | None:
    m = re.match(r"base:t00_(\d+)$", key)
    return int(m.group(1)) if m else None


def where(path: str) -> tuple[str, str]:
    """('story', scene) / ('battle', speaker file) / ('other', file name) for a dialogue file."""
    name = Path(path).stem
    if "/story/" in path and (m := re.match(r"scene-(\d+)$", name)):
        return "story", str(int(m.group(1)))
    if "/battle/" in path:
        return "battle", name
    return "other", name


def commit_changes(blobs: Blobs, commit: str, ja_terms: dict[str, str]) -> dict:
    parent = f"{commit}^"
    files = [f for f in git("diff", "--name-only", parent, commit, "--", *PATHS).split("\n") if f]
    lines: dict[tuple, dict] = {}       # (kind, group, key) -> item
    term_rows: dict[str, dict] = {}
    for path in files:
        if path.startswith("content/locales/") and path.endswith(".json"):
            code = LANG_OF.get(Path(path).stem)
            if not code:
                continue
            old, new = terms(blobs.read(parent, path)), terms(blobs.read(commit, path))
            for key, value in new.items():
                if key in old and old[key] != value:
                    row = term_rows.setdefault(key, {"key": key, "ja": ja_terms.get(key, ""), "was": {}, "now": {}})
                    row["was"][code], row["now"][code] = old[key], value
            continue
        if not path.endswith(".txt"):
            continue
        parts = Path(path).parts       # content/dialogue/<locale>/...
        code = LANG_OF.get(parts[2]) if len(parts) > 2 else None
        if not code:
            continue
        old, new = entries(blobs.read(parent, path), path), entries(blobs.read(commit, path), path)
        kind, group = where(path)
        for key, entry in new.items():
            now = story.pages(entry, "target")
            was = story.pages(old[key], "target") if key in old else []
            if now == was or not now:
                continue
            item = lines.setdefault((kind, group, key), {
                "key": key, "id": text_id(key), "who": entry.note.strip(),
                "ja": "\n".join(story.pages(entry, "source")), "was": {}, "now": {}})
            item["was"][code], item["now"][code] = "\n".join(was), "\n".join(now)
    groups: dict[tuple, dict] = {}
    for (kind, group, _), item in lines.items():
        groups.setdefault((kind, group), {"kind": kind, "group": group, "lines": []})["lines"].append(item)
    out = list(groups.values())
    out.sort(key=lambda g: ({"story": 0, "battle": 1, "other": 2}[g["kind"]], int(g["group"]) if g["group"].isdigit() else 0, g["group"]))
    for g in out:
        g["lines"].sort(key=lambda x: x["id"] if x["id"] is not None else 0)
    if term_rows:
        out.append({"kind": "terms", "group": "", "lines": sorted(term_rows.values(), key=lambda r: r["key"])})
    counts = {code: sum(1 for g in out for x in g["lines"] if code in x["now"]) for code in story.LOCALES}
    return {"groups": out, "counts": counts}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", help="release tag the history starts from (default: the tag in web/src/data/release.json)")
    args = parser.parse_args()
    tag = args.baseline or json.loads((ROOT / "web/src/data/release.json").read_text())["tag"]
    index_path = ROOT / "web/.data/story/index.json"
    if not index_path.exists():
        raise SystemExit("web/.data/story/index.json is missing: run web/scripts/export_story.py first")
    titles = {s["scene"]: s["title"] for s in json.loads(index_path.read_text())["scenes"]}
    ja_terms = terms((ROOT / "content/locales/ja.json").read_text())
    log = git("log", "--reverse", "--first-parent", "--format=%H%x09%h%x09%cI%x09%s", f"{tag}..HEAD", "--", *PATHS)
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.json"):
        old.unlink()
    blobs = Blobs()
    commits = []
    for row in filter(None, log.split("\n")):
        full, short, date, subject = row.split("\t", 3)
        data = commit_changes(blobs, full, ja_terms)
        if not any(data["counts"].values()):
            continue
        for g in data["groups"]:
            if g["kind"] == "story":
                g["title"] = titles.get(int(g["group"]))
        record = {"commit": short, "date": date, "subject": subject, **data}
        (OUT / f"{short}.json").write_text(json.dumps(record, ensure_ascii=False, separators=(",", ":")))
        commits.append({"commit": short, "date": date, "subject": subject, "counts": data["counts"],
                        "scenes": {code: [int(g["group"]) for g in data["groups"]
                                          if g["kind"] == "story" and any(code in x["now"] for x in g["lines"])]
                                   for code in story.LOCALES},
                        "terms": sum(len(g["lines"]) for g in data["groups"] if g["kind"] == "terms")})
    commits.reverse()   # newest first, as the page lists them
    (OUT / "index.json").write_text(json.dumps({"schema": "srw64.web-history.v1", "baseline": tag, "commits": commits},
                                               ensure_ascii=False, indent=1))
    print(f"since {tag}: {len(commits)} commits changed translations; "
          + ", ".join(f"{code} {sum(c['counts'][code] for c in commits)} line changes" for code in story.LOCALES))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
