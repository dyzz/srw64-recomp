#!/usr/bin/env python3
"""Fetch the built-in RetroArch filters (docs/native/bezels-and-filters.md).

A short list of slang presets from libretro/slang-shaders at a pinned commit, each with
every file it reaches: its passes and textures (.slangp), the presets it #references,
and the shaders' #includes. They keep their paths in the repository, so the presets work
unchanged, and land in build/filters, which the packages ship as filters/ beside the
program (the settings list it as the built-in folder). The player's own presets go in
filters/ in their data folder; RetroArch's own folder is found too.

The files are the shader authors' work under their own licences (named in each file);
NOTICE.txt says where they come from."""
from __future__ import annotations

import argparse
from pathlib import PurePosixPath
from pathlib import Path
import re
import shutil
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "build/filters"
COMMIT = "1e0238f9fdd4668ce8212c31d80877af605d3b53"   # libretro/slang-shaders, 2026-10-05
RAW = f"https://raw.githubusercontent.com/libretro/slang-shaders/{COMMIT}/"
# Light enough for a Steam Deck first; the look in parentheses.
PRESETS = [
    "crt/crt-lottes.slangp",                    # CRT: curved, shadow mask
    "crt/crt-easymode.slangp",                  # CRT: flat, clean scanlines
    "crt/crt-geom.slangp",                      # CRT: curvature and corners
    "crt/zfast-crt.slangp",                     # CRT: very light, for handhelds
    "crt/crt-guest-advanced-fast.slangp",       # CRT: many options, heavier
    "scanlines/scanline.slangp",                # scanlines only
    "ntsc/ntsc-adaptive.slangp",                # composite video colour bleed
    "pixel-art-scaling/sharp-bilinear.slangp",  # sharp pixels at any size
    "edge-smoothing/xbrz/xbrz-freescale.slangp",  # smoothed pixel art
]


def fetch(path: str) -> bytes:
    try:
        with urllib.request.urlopen(RAW + path, timeout=60) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        raise SystemExit(f"filters: {path} is not at {COMMIT[:7]} ({error.code})") from error


def resolve(base: str, relative: str) -> str:
    parts: list[str] = []
    for part in (PurePosixPath(base).parent / relative.replace("\\", "/")).parts:
        if part == "..":
            if not parts:
                raise SystemExit(f"filters: {relative} from {base} leaves the repository")
            parts.pop()
        elif part != ".":
            parts.append(part)
    return "/".join(parts)


def references(path: str, data: bytes) -> list[str]:
    """The files `path` needs, as repository paths."""
    text = data.decode("utf-8", errors="replace")
    found = []
    if path.endswith(".slangp"):
        textures = []
        for line in text.splitlines():
            line = line.split("//")[0].strip()
            if line.startswith("#reference"):
                found.append(resolve(path, line.split(None, 1)[1].strip().strip('"')))
                continue
            key, sep, value = line.partition("=")
            if not sep:
                continue
            key, value = key.strip(), value.strip().strip('"')
            if re.fullmatch(r"shader\d+", key):
                found.append(resolve(path, value))
            elif key == "textures":
                textures = [t.strip() for t in value.split(";") if t.strip()]
        for line in text.splitlines():
            key, sep, value = line.partition("=")
            if sep and key.strip() in textures:
                found.append(resolve(path, value.strip().strip('"')))
    elif path.endswith((".slang", ".h", ".inc", ".glsl")):
        for match in re.finditer(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
            found.append(resolve(path, match.group(1)))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="fetch again even if this commit is in place")
    parser.add_argument("--dest", type=Path, default=DEST)
    args = parser.parse_args()
    dest = args.dest.resolve()
    stamp = dest / ".commit"
    if not args.force and stamp.exists() and stamp.read_text().strip() == COMMIT:
        print(f"filters at {COMMIT[:7]} already in {dest}")
        return 0
    if dest.exists():
        shutil.rmtree(dest)
    pending, seen = list(PRESETS), set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        data = fetch(path)
        target = dest / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        pending += references(path, data)
    (dest / "NOTICE.txt").write_text(
        "Built-in filters: RetroArch slang shader presets from https://github.com/libretro/slang-shaders\n"
        f"at commit {COMMIT}, unchanged, with every file they use. Each shader is its authors' work under\n"
        "the licence written at the top of its file.\n\n" + "\n".join(PRESETS) + "\n", encoding="utf-8")
    stamp.write_text(COMMIT + "\n")
    print(f"filters: {len(PRESETS)} presets, {len(seen)} files -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
