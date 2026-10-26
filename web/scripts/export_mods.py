#!/usr/bin/env python3
"""Pack the MOD packages the website hosts itself and write their index entries.

The site normally lists a package by its author's GitHub Release (docs/design/mod-packages.md
§13); our own small samples are hosted on the site instead (user, 2026-10-26), so a player
in mainland China can download them too. Each source folder in HOSTED is packed with
tools/mods/pack.py (reproducible: fixed dates, sorted entries) and checked with
tools/mods/validate.py, then

- web/public/mods/<id>-<version>.srw64mod is the file the site serves, and
- web/src/data/mods/<id>.json is its entry (§13.2's fields; url and page on this site),
  which the MOD download page (src/pages/[lang]/mods/index.astro) lists.

    .venv/bin/python web/scripts/export_mods.py           write both
    .venv/bin/python web/scripts/export_mods.py --check   exit 1 if either is out of date

Run it after changing a hosted package's source (tests/test_web_mods.py checks).
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mods"))
sys.path.insert(0, str(ROOT / "tools"))
import pack  # noqa: E402
import validate  # noqa: E402

# The packages the site hosts: source folders under the repository.
HOSTED = ["config/recomp/mods/space-a"]
PUBLIC = ROOT / "web/public/mods"
DATA = ROOT / "web/src/data/mods"
REPO = "https://github.com/dyzz/srw64-recomp"


def entry(source: str, summary: dict) -> dict:
    # The date the source last changed, so the entry does not move on every export.
    published = subprocess.run(["git", "log", "-1", "--format=%cs", "--", source], cwd=ROOT,
                               capture_output=True, text=True, check=True).stdout.strip()
    summary = {k: v for k, v in summary.items() if k != "path"}   # the temporary archive
    return {**summary, "url": f"/mods/{summary['file']}", "page": f"/mods/#{summary['id']}",
            "source": f"{REPO}/tree/main/{source}", "published": published or None, "yanked": None}


def build() -> list[tuple[Path, bytes, Path, str]]:
    """The files to write: (archive path, bytes, entry path, entry text)."""
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        for source in HOSTED:
            _, archive, _ = pack.build_package(ROOT / source, Path(tmp))
            problems, summary = validate.check(archive)
            if problems:
                raise SystemExit(f"{source}: " + "; ".join(str(p) for p in problems))
            text = json.dumps(entry(source, summary), ensure_ascii=False, indent=2) + "\n"
            out.append((PUBLIC / archive.name, archive.read_bytes(), DATA / f"{summary['id']}.json", text))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="only compare; exit 1 if out of date")
    args = parser.parse_args()
    stale = []
    files = build()
    for archive, data, path, text in files:
        if args.check:
            if not archive.exists() or archive.read_bytes() != data:
                stale.append(str(archive.relative_to(ROOT)))
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(str(path.relative_to(ROOT)))
        else:
            archive.parent.mkdir(parents=True, exist_ok=True)
            path.parent.mkdir(parents=True, exist_ok=True)
            archive.write_bytes(data)
            path.write_text(text, encoding="utf-8")
            print(archive.relative_to(ROOT), len(data), "bytes;", path.relative_to(ROOT))
    # Hosted files no longer produced (a package removed or versioned up).
    keep = {a.name for a, *_ in files} | {p.name for *_, p, _ in files}
    for old in [*PUBLIC.glob("*.srw64mod"), *DATA.glob("*.json")]:
        if old.name not in keep:
            stale.append(str(old.relative_to(ROOT)) + " (no longer hosted)")
            if not args.check:
                old.unlink()
    if args.check and stale:
        print("out of date, run web/scripts/export_mods.py:\n  " + "\n  ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
