#!/usr/bin/env python3
"""Validate/compile a native presentation profile without launching the game."""
import argparse
import json
from pathlib import Path

from srw64_native.profile import load_profile, prepare_profile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=ROOT / "config/recomp/play-profile.json")
    parser.add_argument("--language")
    parser.add_argument("--images", choices=("original", "hd"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profile = load_profile(args.profile, locale=args.language, images=args.images)
    result = prepare_profile(ROOT, profile, ROOT / "rom.z64", args.output.resolve())
    print(json.dumps({k: result[k] for k in ("schema", "source_records", "translated_records", "coverage", "art", "hd_available", "hd_unavailable_reason")}, indent=2))


if __name__ == "__main__":
    main()
