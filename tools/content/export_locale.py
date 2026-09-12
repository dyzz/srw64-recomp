#!/usr/bin/env python3
"""Export selected original-ROM messages as a checked contributor template."""
import argparse
import json
from pathlib import Path

from srw64_native.catalog import source_catalog, compile_locale

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--locale", required=True)
    parser.add_argument("--font", required=True, help="installed font PostScript name")
    parser.add_argument("--key", action="append", required=True, help="repeatable stable key, e.g. base:t00_17412")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sources, hashes, _ = source_catalog(ROOT, ROOT / "rom.z64")
    japanese = json.loads((ROOT / "content/locales/ja.json").read_text())
    document = {"schema": "srw64.locale.v1", "locale": args.locale, "source_locale": "ja", "font": args.font,
                "ui": japanese["ui"], "entries": [{"key": key, "source_sha256": hashes[key],
                "source": sources[key], "target": sources[key]} for key in args.key]}
    compile_locale(document, sources, hashes)
    with args.output.open("x") as file:
        file.write(json.dumps(document, ensure_ascii=False, indent=2) + "\n")
    print(f"Exported {len(args.key)} messages. Edit target/ui, retain source_sha256 and control/parameter tokens.")


if __name__ == "__main__":
    main()
