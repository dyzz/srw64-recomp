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


def portrait_lookup(art_directory: Path, output: Path):
    """(image, palette) -> the whole HD portrait the native pages show, or None.

    The base palette uses the compiled image; the silhouette palette gets a copy
    filled with its grey, made once per image. Other palettes stay original."""
    spec_path = art_directory / "srw64-portraits-hd.json"
    if not spec_path.exists():
        return lambda image, palette: None
    spec = json.loads(spec_path.read_text())
    rows = {row["image"]: row for row in spec["images"]}
    silhouette, made = spec["silhouette"], {}

    def lookup(image: int, palette: int):
        row = rows.get(image)
        if row is None:
            return None
        if palette == row["palette"]:
            return str(art_directory / row["file"])
        if palette != silhouette["palette"]:
            return None
        if image not in made:
            from PIL import Image
            output.mkdir(parents=True, exist_ok=True)
            source = Image.open(art_directory / row["file"]).convert("RGBA")
            filled = Image.new("RGBA", source.size, tuple(silhouette["rgb"]) + (0,))
            filled.putalpha(source.getchannel("A"))
            path = output / f"portrait-{image}-silhouette.png"
            filled.save(path)
            made[image] = str(path)
        return made[image]
    return lookup


def whole_images(root: Path, spec: dict, index_name: str, schema: str, what: str):
    """A folder of whole images the host draws itself, checked against its index."""
    folder = inside(root, spec["path"])
    index_bytes = (folder / index_name).read_bytes()
    if sha(index_bytes) != spec["manifest_sha256"]:
        raise ValueError(f"{what} manifest changed")
    index = json.loads(index_bytes)
    if index.get("schema") != schema:
        raise ValueError(f"Unsupported {what.lower()} image set")
    files = []
    for row in index["images"]:
        path = inside(folder, row["file"])
        if sha(path.read_bytes()) != row["sha256"]:
            raise ValueError(f"{what} pixels changed: {row.get('image', row['file'])}")
        files.append((path, row))
    return index, files


def copy_whole_images(index: dict, files: list, output: Path, folder: str, runtime_name: str) -> None:
    (output / folder).mkdir()
    for path, row in files:
        shutil.copyfile(path, output / folder / row["file"])
    runtime = {**index, "images": [{**row, "file": f"{folder}/{row['file']}"} for _, row in files]}
    (output / runtime_name).write_text(json.dumps(runtime, indent=2) + "\n")


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
    # Whole-image portraits (sprite mode 7, native_portrait.cpp) and intermission
    # backgrounds (sprite mode 4, native_background.cpp).
    portrait_index, portrait_files = None, []
    if "portraits" in manifest:
        portrait_index, portrait_files = whole_images(root, manifest["portraits"], "portraits.json",
                                                      "srw64.portrait-images.v1", "Portrait")
    background_index, background_files = None, []
    if "backgrounds" in manifest:
        background_index, background_files = whole_images(root, manifest["backgrounds"], "backgrounds.json",
                                                          "srw64.background-images.v1", "Background")
    # No output is written until every input has passed validation.
    output.mkdir(parents=True, exist_ok=False)
    for path, name in files:
        shutil.copyfile(path, output / name)
    if portrait_index is not None:
        copy_whole_images(portrait_index, portrait_files, output, "portraits", "srw64-portraits-hd.json")
    if background_index is not None:
        copy_whole_images(background_index, background_files, output, "backgrounds", "srw64-backgrounds-hd.json")
    (output / "rt64.json").write_text(json.dumps({"configuration": database["configuration"], "textures": textures}, indent=2) + "\n")
    if "worldmap" in manifest:
        spec = manifest["worldmap"]
        (output / "srw64-worldmap-hd.json").write_text(json.dumps(spec, indent=2) + "\n")
    return {"path": str(output), "count": len(textures), "portraits": len(portrait_files), "backgrounds": len(background_files),
            "manifest_sha256": sha((output / "rt64.json").read_bytes())}
