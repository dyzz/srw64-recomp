#!/usr/bin/env python3
"""Expand content/locales/terms/<locale>.json into that locale's entries.

Run after editing a term table; --check only reports whether a locale file is
stale, unused terms, and the Japanese strings complete sections still lack."""
import argparse
import json
import sys
from pathlib import Path

from srw64_native.catalog import compile_locale, source_catalog
from srw64_native.terms import expand, load_sections, merge, missing, unused

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="write nothing; fail if a locale file is stale")
    parser.add_argument("--missing", action="store_true", help="list every untranslated string of complete sections")
    parser.add_argument("locales", nargs="*", help="default: every locale that has a term table")
    args = parser.parse_args()
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    sections = load_sections(ROOT)
    locales = args.locales or sorted(p.stem for p in (ROOT / "content/locales/terms").glob("*.json")
                                     if p.name != "sections.json")
    stale = False
    for locale in locales:
        terms = json.loads((ROOT / f"content/locales/terms/{locale}.json").read_text())
        if terms.get("locale") != locale:
            raise ValueError(f"Term table identity mismatch: {locale}")
        path = ROOT / f"content/locales/{locale}.json"
        document = json.loads(path.read_text())
        targets = expand(sections, terms, sources)
        updated = merge(document, targets, hashes)
        compile_locale(updated, sources, hashes)
        text = json.dumps(updated, ensure_ascii=False, indent=2) + "\n"
        gaps, dead = missing(sections, terms, sources), unused(sections, terms, sources)
        print(f"{locale}: {len(targets)} records from terms, {len(gaps)} strings missing, {len(dead)} unused terms")
        for section, source in dead:
            print(f"  unused {section}: {source}")
        if args.missing:
            for section, source in gaps:
                print(f"  missing {section}: {source}")
        if text != path.read_text():
            stale = True
            if args.check:
                print(f"  {path.relative_to(ROOT)} is stale")
            else:
                path.write_text(text)
                print(f"  wrote {path.relative_to(ROOT)}")
    if args.check and stale:
        sys.exit(1)


if __name__ == "__main__":
    main()
