#!/usr/bin/env python3
"""Fetch librashader, which runs RetroArch slang shader presets (docs/native/bezels-and-filters.md).

The host loads it at run time (post_filter.cpp), so a build without it still runs and
the settings say the filters are unavailable. This takes the project's published
release for this machine, checks its hash and puts the library, its header and its
licence (MPL 2.0) in build/recomp/thirdparty/librashader. Only macOS has a pinned
release so far; other platforms build it from source later."""
from __future__ import annotations

import argparse
import hashlib
import io
import platform
from pathlib import Path
import shutil
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[3]
DEST = ROOT / "build/recomp/thirdparty/librashader"
VERSION = "0.12.0"
BASE = f"https://github.com/SnowflakePowered/librashader/releases/download/librashader-v{VERSION}"
RELEASES = {
    ("Darwin", "arm64"): ("librashader-aarch64-macos-v0.12.0-optimized.zip",
                          "49808004a4904f6a99e0231092dcfdfe52b7b61f68430a4c9f1e165749c4c90e"),
    ("Darwin", "x86_64"): ("librashader-x86_64-macos-v0.12.0-optimized.zip",
                           "8b2a50cefacf4073e8fa4757bec30242a788068c4096a580d94430688c184767"),
}
LICENSE = f"https://raw.githubusercontent.com/SnowflakePowered/librashader/librashader-v{VERSION}/LICENSE.md"
LIBRARY = {"Darwin": "librashader.dylib"}


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="download again even if the version is in place")
    args = parser.parse_args()
    key = (platform.system(), platform.machine())
    if key not in RELEASES:
        print(f"librashader: no pinned release for {key[0]} {key[1]}; filters stay off in this build", file=sys.stderr)
        return 0
    stamp = DEST / "VERSION"
    if not args.force and stamp.exists() and stamp.read_text().strip() == VERSION and (DEST / LIBRARY[key[0]]).exists():
        print(f"librashader {VERSION} already in {DEST}")
        return 0
    name, digest = RELEASES[key]
    data = fetch(f"{BASE}/{name}")
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit(f"librashader: {name} does not match its pinned SHA-256")
    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in (LIBRARY[key[0]], "librashader.h"):
            (DEST / member).write_bytes(archive.read(member))
    (DEST / "LICENSE.md").write_bytes(fetch(LICENSE))
    stamp.write_text(VERSION + "\n")
    print(f"librashader {VERSION} -> {DEST}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
