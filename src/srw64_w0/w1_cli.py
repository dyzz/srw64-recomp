from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from .baseline import BaselineError
from .resources import ResourceError
from .text_ir import TextIRError
from .translation import TranslationError
from .w1 import build_w1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="srw64-w1")
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--config", type=Path, default=Path("config/w1-slice.json"))
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--work-dir", type=Path, default=Path("build/w1"))
    args = parser.parse_args(argv)
    try:
        report = build_w1(args.rom, args.config, args.font, args.work_dir)
    except (BaselineError, ResourceError, TextIRError, TranslationError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
