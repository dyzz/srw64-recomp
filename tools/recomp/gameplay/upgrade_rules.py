#!/usr/bin/env python3
"""Upgrade rules file for the native host (docs/gameplay/upgrade-limits.md).

  export OUTPUT [--units] [--weapons]   write a file holding the original values
  check PATH                            validate a file the way the host will

Pass the file to the game with play_native.py --upgrade-rules PATH.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native import upgrade_rules  # noqa: E402


def labels(kind: str) -> list[str] | None:
    records = ROOT / f"assets/original-data/records/{kind}.jsonl"
    if not records.exists():
        return None
    return [json.loads(line)["label"].strip() for line in records.read_text().splitlines()]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    export = commands.add_parser("export", help="write a template with the original values")
    export.add_argument("output", type=Path)
    export.add_argument("--units", action="store_true", help="also list every unit's cap")
    export.add_argument("--weapons", action="store_true", help="also list every weapon's upgrade type")
    export.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    check = commands.add_parser("check", help="validate a rules file")
    check.add_argument("path", type=Path)
    args = parser.parse_args()
    if args.command == "export":
        document = upgrade_rules.template(args.rom.read_bytes(), labels("units"), labels("weapons"),
                                          units=args.units, weapons=args.weapons)
        upgrade_rules.validate(document)
        with args.output.open("x") as out:
            out.write(json.dumps(document, ensure_ascii=False, indent=2) + "\n")
        print(f"已写出 {args.output}")
        return 0
    try:
        summary = upgrade_rules.report(args.path)
    except ValueError as error:
        print(f"升级规则文件无效：{error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
