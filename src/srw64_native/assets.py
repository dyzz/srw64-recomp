"""Compile an explicit, language-neutral art allowlist into RT64's format."""
from __future__ import annotations

import json
import os
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


def unit_lookup(art_directory: Path):
    """(scene, atlas, palette) -> the whole HD unit pose the native pages show, or None."""
    spec_path = art_directory / "srw64-units-hd.json"
    if not spec_path.exists():
        return lambda scene, atlas, palette: None
    rows = {(row["scene"], row["atlas"], row["palette"]): row for row in json.loads(spec_path.read_text())["images"]}

    def lookup(scene: int, atlas: int, palette: int):
        row = rows.get((scene, atlas, palette))
        return str(art_directory / row["file"]) if row else None
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
    # `scenes`: the battle viewer's scene thumbnails beside the battle sprites.
    for row in [*index["images"], *index.get("scenes", [])]:
        path = inside(folder, row["file"])
        if sha(path.read_bytes()) != row["sha256"]:
            raise ValueError(f"{what} pixels changed: {row.get('image', row['file'])}")
        files.append((path, row))
    return index, files


def place(source: Path, target: Path, link: bool) -> None:
    """source at target: a hard link when link (development runs: the same volume, no
    space or time spent, and nothing writes the files), else or failing that a copy."""
    if link:
        try:
            os.link(source, target)
            return
        except OSError:
            pass
    shutil.copyfile(source, target)


def copy_whole_images(index: dict, files: list, output: Path, folder: str, runtime_name: str, link: bool = False) -> None:
    (output / folder).mkdir()
    for path, row in files:
        place(path, output / folder / row["file"], link)
    runtime = {**index, **{key: [{**row, "file": f"{folder}/{row['file']}"} for row in index[key]] for key in ("images", "scenes") if key in index}}
    (output / runtime_name).write_text(json.dumps(runtime, indent=2) + "\n")


def tactical_maps(root: Path, spec: dict) -> tuple[Path, dict, list]:
    """The HD tactical map pack (docs/design/tactical-map-hd-kit.md), checked file by file."""
    folder = inside(root, spec["path"])
    index_bytes = (folder / "tactical-maps.json").read_bytes()
    if sha(index_bytes) != spec["manifest_sha256"]:
        raise ValueError("Tactical map manifest changed")
    index = json.loads(index_bytes)
    if index.get("schema") != "srw64.tactical-maps.v1":
        raise ValueError("Unsupported tactical map pack")
    files = []
    for row in [*index["maps"], *index["colony_frames"]]:
        for name, digest in row["files"].items():
            path = inside(folder, f"{row['folder']}/{name}")
            if sha(path.read_bytes()) != digest:
                raise ValueError(f"Tactical map pixels changed: {row['folder']}/{name}")
            files.append((path, f"{row['folder']}/{name}"))
    return folder, index, files


def compile_art(root: Path, manifest: dict, output: Path, maps_in_place: bool = False) -> dict:
    """maps_in_place: point the runtime at the tactical map pack where it is instead of
    copying its gigabyte, and hard-link the other pictures (development profiles; bundles
    copy)."""
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
        if row["kind"] not in ("worldmap", "frame", "space", "icon", "battle"):
            raise ValueError("Unreviewed/language-dependent image category")
        entry = originals[digest]
        path = inside(source, entry["path"])
        if sha(path.read_bytes()) != row["sha256"]:
            raise ValueError(f"Art pixels changed: {digest}")
        name = f"{digest}{path.suffix}"
        # RT64 ignores `kind`; it names the texture's family (worldmap, space, frame, icon, battle).
        textures.append({**entry, "path": name, "kind": row["kind"]})
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
    # Whole unit poses the pages draw in place of battle_assets.units (docs/design/unit-pose-hd.md).
    unit_index, unit_files = None, []
    if "units" in manifest:
        unit_index, unit_files = whole_images(root, manifest["units"], "units.json", "srw64.unit-images.v1", "Unit pose")
    # Frames of a unit's other battle images derived from its whole HD pose (native_sprite.cpp).
    extra_index, extra_files = None, []
    if "unit_extras" in manifest:
        extra_index, extra_files = whole_images(root, manifest["unit_extras"], "unit-extras.json",
                                                "srw64.unit-extra-images.v1", "Unit extra")
    # Other battle sprites drawn by the same scene-sprite replacement: the cut-ins
    # (tools/hd_ai/cutin_hd.py).
    sprite_index, sprite_files = None, []
    if "battle_sprites" in manifest:
        sprite_index, sprite_files = whole_images(root, manifest["battle_sprites"], "battle-sprites.json",
                                                  "srw64.unit-extra-images.v1", "Battle sprite")
    background_index, background_files = None, []
    if "backgrounds" in manifest:
        background_index, background_files = whole_images(root, manifest["backgrounds"], "backgrounds.json",
                                                          "srw64.background-images.v1", "Background")
    # Whole scene frames for the host's scene-sprite replacement (native_sprite.cpp): the title.
    scene_index, scene_files = None, []
    if "scene_images" in manifest:
        folder = inside(root, manifest["scene_images"]["path"])
        index_bytes = (folder / "scene-images.json").read_bytes()
        if sha(index_bytes) != manifest["scene_images"]["manifest_sha256"]:
            raise ValueError("Scene image manifest changed")
        scene_index = json.loads(index_bytes)
        if scene_index.get("schema") != "srw64.scene-images.v1":
            raise ValueError("Unsupported scene image set")
        for row in scene_index["images"]:
            path = inside(folder, row["file"])
            if sha(path.read_bytes()) != row["sha256"]:
                raise ValueError(f"Scene image pixels changed: {row['file']}")
            scene_files.append((path, row))
    maps_folder, maps_index, map_files = None, None, []
    if "tactical_maps" in manifest:
        maps_folder, maps_index, map_files = tactical_maps(root, manifest["tactical_maps"])
    # No output is written until every input has passed validation.
    output.mkdir(parents=True, exist_ok=False)
    for path, name in files:
        place(path, output / name, maps_in_place)
    if portrait_index is not None:
        copy_whole_images(portrait_index, portrait_files, output, "portraits", "srw64-portraits-hd.json", maps_in_place)
    if unit_index is not None:
        copy_whole_images(unit_index, unit_files, output, "units", "srw64-units-hd.json", maps_in_place)
    if extra_index is not None:
        copy_whole_images(extra_index, extra_files, output, "unit-extras", "srw64-unit-extras-hd.json", maps_in_place)
    if sprite_index is not None:
        copy_whole_images(sprite_index, sprite_files, output, "battle-sprites", "srw64-battle-sprites-hd.json", maps_in_place)
    if background_index is not None:
        copy_whole_images(background_index, background_files, output, "backgrounds", "srw64-backgrounds-hd.json", maps_in_place)
    if scene_index is not None:
        (output / "scene-images").mkdir()
        for path, row in scene_files:
            place(path, output / "scene-images" / row["file"], maps_in_place)
        runtime = {**scene_index, "images": [{**row, "file": f"scene-images/{row['file']}"} for _, row in scene_files]}
        (output / "srw64-scene-images.json").write_text(json.dumps(runtime, indent=2) + "\n")
    (output / "rt64.json").write_text(json.dumps({"configuration": database["configuration"], "textures": textures}, indent=2) + "\n")
    if "worldmap" in manifest:
        spec = manifest["worldmap"]
        (output / "srw64-worldmap-hd.json").write_text(json.dumps(spec, indent=2) + "\n")
    if maps_index is not None:
        # The host (native_map.cpp) loads every map folder under "root", relative to this file.
        if maps_in_place:
            root_name = str(maps_folder)
        else:
            root_name = "maps"
            for path, name in map_files:
                (output / root_name / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, output / root_name / name)
            shutil.copyfile(maps_folder / "tactical-maps.json", output / root_name / "tactical-maps.json")
        (output / "srw64-tactical-maps.json").write_text(json.dumps(
            {"schema": "srw64.tactical-maps-runtime.v1", "root": root_name, "maps": len(maps_index["maps"]),
             "colony_frames": len(maps_index["colony_frames"])}, indent=2) + "\n")
    return {"path": str(output), "count": len(textures), "portraits": len(portrait_files), "units": len(unit_files), "unit_extras": len(extra_files), "battle_sprites": len(sprite_files), "backgrounds": len(background_files), "scene_images": len(scene_files),
            "tactical_maps": len(maps_index["maps"]) if maps_index else 0,
            "manifest_sha256": sha((output / "rt64.json").read_bytes())}
