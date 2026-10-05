#!/usr/bin/env python3
"""Fetch or build librashader, which runs RetroArch slang shader presets (docs/native/bezels-and-filters.md).

The host loads it at run time (post_filter.cpp), so a build without it still runs and
the settings say the filters are unavailable. macOS takes the project's published
release (Metal), checked by hash. Linux and the Steam Deck have no published build: there
it is compiled from the pinned commit with Cargo (the Linux container has Rust,
tools/release/linux/Dockerfile), with the Vulkan runtime. --from-source does the same on
any machine, e.g. a Vulkan build on a Mac to try the MoltenVK path. Either way the
library, its header and its licence (MPL 2.0) land in build/recomp/thirdparty/librashader/
<system> (darwin, linux) unless --dest says otherwise."""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[3]
# One folder per system: the Mac and the Linux container share this tree.
DEST = ROOT / "build/recomp/thirdparty/librashader" / platform.system().lower()
VERSION = "0.12.0"
COMMIT = "87e8a97b50516d997defeaa168173dcd185d4022"   # librashader-v0.12.0
REPOSITORY = "https://github.com/SnowflakePowered/librashader.git"
SOURCE = ROOT / "build/recomp/upstream/librashader"
BASE = f"https://github.com/SnowflakePowered/librashader/releases/download/librashader-v{VERSION}"
RELEASES = {
    ("Darwin", "arm64"): ("librashader-aarch64-macos-v0.12.0-optimized.zip",
                          "49808004a4904f6a99e0231092dcfdfe52b7b61f68430a4c9f1e165749c4c90e"),
    ("Darwin", "x86_64"): ("librashader-x86_64-macos-v0.12.0-optimized.zip",
                           "8b2a50cefacf4073e8fa4757bec30242a788068c4096a580d94430688c184767"),
}
LICENSE = f"https://raw.githubusercontent.com/SnowflakePowered/librashader/librashader-v{VERSION}/LICENSE.md"
LIBRARY = {"Darwin": "librashader.dylib", "Linux": "librashader.so"}
BUILT = {"Darwin": "liblibrashader_capi.dylib", "Linux": "liblibrashader_capi.so"}
# The runtimes RT64 can need here: Vulkan everywhere, Metal on a Mac.
FEATURES = {"Darwin": "runtime-vulkan runtime-metal", "Linux": "runtime-vulkan"}


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def from_release(system: str, name: str, digest: str, dest: Path) -> None:
    data = fetch(f"{BASE}/{name}")
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit(f"librashader: {name} does not match its pinned SHA-256")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in (LIBRARY[system], "librashader.h"):
            (dest / member).write_bytes(archive.read(member))
    (dest / "LICENSE.md").write_bytes(fetch(LICENSE))


def from_source(system: str, dest: Path) -> None:
    if not (SOURCE / ".git").exists():
        subprocess.run(["git", "clone", "--quiet", REPOSITORY, str(SOURCE)], check=True)
    # The Linux container runs as another user on the same checkout.
    git = ["git", "-c", "safe.directory=*", "-C", str(SOURCE)]
    if subprocess.run([*git, "cat-file", "-e", COMMIT + "^{commit}"], capture_output=True).returncode:
        subprocess.run([*git, "fetch", "--quiet", "origin", COMMIT], check=True)
    subprocess.run([*git, "checkout", "--quiet", COMMIT], check=True)
    head = subprocess.run([*git, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    if head != COMMIT:
        raise SystemExit(f"librashader: checkout is {head}, not the pinned {COMMIT}")
    # A target folder per system and machine: the Mac and the Linux container share the tree.
    target = ROOT / f"build/recomp/thirdparty/librashader-target/{system.lower()}-{platform.machine()}"
    env = dict(os.environ, CARGO_TARGET_DIR=str(target))
    subprocess.run(["cargo", "build", "--locked", "--profile", "optimized", "-p", "librashader-capi",
                    "--no-default-features", "--features", FEATURES[system]], cwd=SOURCE, env=env, check=True)
    shutil.copyfile(target / "optimized" / BUILT[system], dest / LIBRARY[system])
    shutil.copyfile(SOURCE / "include/librashader.h", dest / "librashader.h")
    shutil.copyfile(SOURCE / "LICENSE.md", dest / "LICENSE.md")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="fetch or build again even if the version is in place")
    parser.add_argument("--from-source", action="store_true", help="build with Cargo even where a release is pinned")
    parser.add_argument("--dest", type=Path, default=DEST)
    args = parser.parse_args()
    system = platform.system()
    if system not in LIBRARY:
        print(f"librashader: nothing for {system} yet; filters stay off in this build", file=sys.stderr)
        return 0
    release = RELEASES.get((system, platform.machine()))
    source = args.from_source or release is None
    stamp_text = f"{VERSION} {'source' if source else 'release'}"
    dest = args.dest.resolve()
    stamp = dest / "VERSION"
    if not args.force and stamp.exists() and stamp.read_text().strip() == stamp_text and (dest / LIBRARY[system]).exists():
        print(f"librashader {stamp_text} already in {dest}")
        return 0
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    if source:
        from_source(system, dest)
    else:
        from_release(system, *release, dest)
    stamp.write_text(stamp_text + "\n")
    print(f"librashader {stamp_text} -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
