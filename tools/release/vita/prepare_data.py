#!/usr/bin/env python3
"""Prepare the PS Vita's data from the player's ROM (docs/design/psv-port.md).

Builds srw64-vita-prepare (tools/release/vita/prepare_data.cpp) for this computer and runs
it: the output folder goes into ux0:data/Marchwind64 on the console as it is (VitaShell's
USB mode, or FTP). The console then checks, imports and rewrites nothing at start.

  tools/release/vita/prepare_data.py ROM [--out DIR] [--vita3k]

--vita3k also copies the result into Vita3K's ux0:data/Marchwind64 (macOS), replacing the
game/ folder there; the player's saves and settings stay. The output is derived from the
ROM: it is the player's own and never a release asset.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build/recomp/vita-prepare"
JSON_INCLUDE = ROOT / "build/recomp/upstream/RT64/src/contrib"
VITA3K = Path.home() / "Library/Application Support/Vita3K/Vita3K/fs/ux0/data/Marchwind64"


def build() -> Path:
    if not (BUILD / "CMakeCache.txt").exists():
        subprocess.run(["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                        "-DBUILD_TESTING=OFF", f"-DSRW64_APP_JSON_INCLUDE_DIR={JSON_INCLUDE}"], check=True)
    subprocess.run(["cmake", "--build", str(BUILD), "--target", "srw64-vita-prepare"], check=True)
    return BUILD / "srw64-vita-prepare"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build/recomp/vita-data")
    parser.add_argument("--vita3k", action="store_true", help="install into Vita3K's ux0:data/Marchwind64")
    args = parser.parse_args()
    tool = build()
    subprocess.run([str(tool), str(args.rom), str(args.out), str(ROOT / "content/dialogue")], check=True)
    if args.vita3k:
        shutil.rmtree(VITA3K / "game", ignore_errors=True)
        shutil.copytree(args.out, VITA3K, dirs_exist_ok=True)
        print(f"installed into {VITA3K}")


if __name__ == "__main__":
    main()
