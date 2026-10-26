#!/usr/bin/env python3
"""Prepare the Switch's data from the player's ROM (docs/design/switch-port.md), as the PS
Vita's is.

Builds srw64-switch-prepare (tools/release/switch/prepare_data.cpp) for this computer and
runs it: the output folder goes into /switch/Marchwind64 on the SD card as it is
(package.py --upload sends it with the program). The console then checks, imports and
rewrites nothing at start.

  tools/release/switch/prepare_data.py ROM [--out DIR]

The output is derived from the ROM: it is the player's own and never a release asset.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build/recomp/switch-prepare"
JSON_INCLUDE = ROOT / "build/recomp/upstream/RT64/src/contrib"


def build() -> Path:
    if not (BUILD / "CMakeCache.txt").exists():
        subprocess.run(["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                        "-DBUILD_TESTING=OFF", f"-DSRW64_APP_JSON_INCLUDE_DIR={JSON_INCLUDE}"], check=True)
    subprocess.run(["cmake", "--build", str(BUILD), "--target", "srw64-switch-prepare"], check=True)
    return BUILD / "srw64-switch-prepare"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/recomp/switch-data")
    args = parser.parse_args()
    tool = build()
    subprocess.run([str(tool), str(args.rom), str(args.out), str(ROOT / "content/dialogue")], check=True)


if __name__ == "__main__":
    main()
