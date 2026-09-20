#!/usr/bin/env python3
"""Export LOCAL-ONLY Original content for the native --play entry.

Input is a prepare_profile/compile_profile output directory. Original dialogue
and portraits are derived from the user's ROM; this output must NOT be uploaded
as a public release asset. Python is used at preparation time, never by --play.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def export_content(prepared: Path, output: Path) -> dict:
    prepared = prepared.resolve(strict=True)
    output = output.absolute()
    profile = json.loads((prepared / "profile.json").read_text(encoding="utf-8"))
    if profile.get("schema") != "srw64.prepared-profile.v1":
        raise ValueError("Expected a prepared profile directory")
    presentation = profile["profile"]["presentation"]
    if presentation["images"] != "original":
        raise ValueError("Standalone content currently supports Original mode only")
    scale = presentation["resolution_scale"]
    if type(scale) is not int or not 1 <= scale <= 8:
        raise ValueError("Resolution scale must be in 1..8")
    raw = (prepared / "dialogue.json").read_bytes()
    if digest(raw) != profile["dialogue"]["sha256"]:
        raise ValueError("Prepared dialogue changed")
    data = json.loads(raw)
    if data.get("schema") != "srw64.native-dialogue-data.v2":
        raise ValueError("Expected v2 native dialogue data")
    if data["rom_sha256"] != profile["rom_sha256"]:
        raise ValueError("Prepared ROM identities differ")
    if profile["profile"].get("baseline") != "srw64-jp-rev0":
        raise ValueError("Unsupported baseline")
    # Validate every input before creating the destination. No source files or
    # unrelated assets are discovered/copied by walking the repository.
    payloads: dict[str, bytes] = {}
    data = copy.deepcopy(data)
    assets = data["name_entry_assets"]
    if assets.get("schema") != "srw64.name-entry-assets.v1":
        raise ValueError("Unsupported name-entry assets")
    for face, portrait in assets["portraits"].items():
        if not re.fullmatch(r"[0-9]+", face):
            raise ValueError("Invalid portrait id")
        source = Path(portrait["original"]).resolve(strict=True)
        if not source.is_relative_to(prepared) or not source.is_file():
            raise ValueError("Original portrait must be inside the prepared directory")
        pixels = source.read_bytes()
        if digest(pixels) != portrait["original_sha256"]:
            raise ValueError(f"Prepared portrait changed: {face}")
        relative = f"name-entry/face-{face}.png"
        payloads[relative] = pixels
        portrait["original"] = relative
        portrait.pop("hd", None)
        portrait.pop("hd_sha256", None)
    # An optional HD source manifest is not a runtime dependency of Original.
    assets["source_sha256"] = None
    payloads["dialogue.json"] = (json.dumps(data, ensure_ascii=False) + "\n").encode("utf-8")
    manifest = {
        "schema": "srw64.standalone-content.v1",
        "baseline": "srw64-jp-rev0",
        "rom_sha256": data["rom_sha256"],
        "dialogue": "dialogue.json",
        "resolution_scale": scale,
        "files": {name: digest(value) for name, value in sorted(payloads.items())},
        "distribution": "local-only-rom-derived-content",
    }
    # Refuse an existing destination rather than destroying or merging its data.
    output.mkdir(parents=True, exist_ok=False)
    try:
        for relative, value in payloads.items():
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(value)
        # Written last: an interrupted export is not a valid standalone pack.
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    except BaseException:
        shutil.rmtree(output)
        raise
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        manifest = export_content(args.prepared, args.output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Content export failed: {error}\n")
    print(f"Exported {len(manifest['files'])} files to {args.output}; local use only, do not redistribute.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
