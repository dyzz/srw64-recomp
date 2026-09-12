"""Compile an explicit, language-neutral art allowlist into RT64's format."""
from __future__ import annotations

import json
from pathlib import Path
import re
import shutil

from .catalog import sha


def inside(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"Content path escapes its root: {relative}")
    return path


def compile_art(root: Path, manifest: dict, output: Path) -> dict:
    if manifest.get("schema") != "srw64.art-pack.v1" or manifest.get("locale") != "neutral":
        raise ValueError("Image toggle accepts only a language-neutral art pack")
    source = inside(root, manifest["source"]["path"])
    if sha((source / "rt64.json").read_bytes()) != manifest["source"]["manifest_sha256"]:
        raise ValueError("Art source manifest changed")
    database = json.loads((source / "rt64.json").read_text())
    originals = {entry["hashes"]["rt64"]: entry for entry in database["textures"]}
    textures, files, seen = [], [], set()
    for row in manifest["textures"]:
        digest = row["hash"]
        if not re.fullmatch(r"[0-9a-f]{16}", digest) or digest in seen:
            raise ValueError("Invalid or conflicting art texture identity")
        seen.add(digest)
        if row["kind"] not in ("worldmap", "portrait", "frame"):
            raise ValueError("Unreviewed/language-dependent image category")
        entry = originals[digest]
        path = inside(source, entry["path"])
        if sha(path.read_bytes()) != row["sha256"]:
            raise ValueError(f"Art pixels changed: {digest}")
        name = f"{digest}{path.suffix}"
        textures.append({**entry, "path": name})
        files.append((path, name))
    if "worldmap" in manifest:
        spec = manifest["worldmap"]
        if set(spec["hashes"]) != {r["hash"] for r in manifest["textures"] if r["kind"] == "worldmap"}:
            raise ValueError("Worldmap audit identities differ from art manifest")
    # No output is written until every input has passed validation.
    output.mkdir(parents=True, exist_ok=False)
    for path, name in files:
        shutil.copyfile(path, output / name)
    (output / "rt64.json").write_text(json.dumps({"configuration": database["configuration"], "textures": textures}, indent=2) + "\n")
    if "worldmap" in manifest:
        spec = manifest["worldmap"]
        (output / "srw64-worldmap-hd.json").write_text(json.dumps(spec, indent=2) + "\n")
    return {"path": str(output), "count": len(textures), "manifest_sha256": sha((output / "rt64.json").read_bytes())}
