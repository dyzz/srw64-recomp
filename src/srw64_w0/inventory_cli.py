from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from .baseline import BaselineError
from .inventory import InventoryError, build_text_inventory
from .text_ir import TextIRError
from .translation import TranslationError


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="srw64-text-inventory")
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument(
        "--baseline", type=Path, default=Path("config/srw64-jp-rev0.json")
    )
    parser.add_argument("--glyph-map", required=True, type=Path)
    parser.add_argument("--unknown-glyph-policy", required=True, type=Path)
    parser.add_argument("--overlay", type=Path)
    parser.add_argument(
        "--work-dir", type=Path, default=Path("build/text-inventory")
    )
    args = parser.parse_args(argv)
    try:
        report = build_text_inventory(
            args.rom,
            args.baseline,
            args.glyph_map,
            args.unknown_glyph_policy,
            args.work_dir,
            args.overlay,
        )
    except (
        BaselineError,
        InventoryError,
        TextIRError,
        TranslationError,
        OSError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
