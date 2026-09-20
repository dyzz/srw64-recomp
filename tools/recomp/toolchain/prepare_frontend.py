#!/usr/bin/env python3
"""Prepare the pinned, optional RecompFrontend renderer without changing upstream."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "build/recomp/upstream/RecompFrontend"
OUTPUT = ROOT / "build/recomp/frontend-adapter"


def git(*args: str, cwd: Path = SOURCE) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def write_changed(path: Path, data: bytes) -> None:
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare(fetch: bool = False) -> dict:
    lock = json.loads((ROOT / "config/recomp/frontend.json").read_text())
    if lock.get("schema") != "srw64.frontend-source.v1":
        raise ValueError("Invalid frontend lock")
    if not SOURCE.exists():
        if not fetch:
            raise RuntimeError("Run tools/recomp/toolchain/prepare_frontend.py --fetch first")
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--no-checkout", lock["url"], str(SOURCE)], check=True)
        git("checkout", "--detach", lock["commit"])
    if git("rev-parse", "HEAD") != lock["commit"]:
        raise RuntimeError("Frontend checkout differs from lock; existing checkout left untouched")
    if fetch:
        git("submodule", "update", "--init", "recompui/lib/RmlUi")
    rml = SOURCE / "recompui/lib/RmlUi"
    if git("rev-parse", "HEAD", cwd=rml) != lock["rmlui_commit"]:
        raise RuntimeError("RmlUi checkout differs from lock")
    for directory in (SOURCE, rml):
        if git("status", "--porcelain", cwd=directory):
            raise RuntimeError(f"Dirty upstream checkout: {directory}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    renderer = SOURCE / "recompui/src/renderer"
    header = (renderer / "ui_renderer.h").read_text()
    # The implementation only needs RmlUi and Plume. Avoid pulling the launcher's
    # mod/config/input headers through its umbrella header. No renderer edits.
    before = '#include "recompui.h"'
    if header.count(before) != 1:
        raise RuntimeError("Pinned renderer header adapter no longer matches")
    header = header.replace(before, '#include <string>\n#include <vector>\n#include "common/rt64_plume.h"\n#include <RmlUi/Core.h>')
    source = (renderer / "ui_renderer.cpp").read_bytes()
    write_changed(OUTPUT / "ui_renderer.h", header.encode())
    write_changed(OUTPUT / "ui_renderer.cpp", source)
    report = {"schema": "srw64.frontend-adapter.v1", "commit": lock["commit"],
              "rmlui_commit": lock["rmlui_commit"],
              "renderer_sha256": hashlib.sha256(source).hexdigest(),
              "adapted_header_sha256": hashlib.sha256(header.encode()).hexdigest(),
              "scope": lock["scope"]}
    write_changed(OUTPUT / "source.json", (json.dumps(report, indent=2) + "\n").encode())
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    print(json.dumps(prepare(parser.parse_args().fetch), indent=2))
