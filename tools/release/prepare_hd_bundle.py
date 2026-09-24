#!/usr/bin/env python3
"""Prepare the HD folder of a full-HD app (tools/release/package_macos.py --hd).

Compiles the art manifest (content/art/stage1-hd.json) from the local assets/:
the RT64 replacement textures (world-map surfaces, space objects, dialogue frame),
the whole HD portraits, intermission backgrounds and title images. Adds the HD
portraits the native pages show, indexed by (image, palette) with a silhouette
copy of each, and copies the world-map model pack and the 5600 marker pack after
validating them. The app's launcher starts in HD when it finds this folder.

The result holds AI-made art and, in the two model packs, reference bytes copied
from the ROM. It is for the player's own machine: do not distribute it.
The tactical-map sample (SRW64_HD_MAPS) is not included."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.assets import compile_art, portrait_lookup  # noqa: E402
from srw64_native.catalog import sha  # noqa: E402

ART = ROOT / "content/art/stage1-hd.json"
MARKER = ROOT / "build/recomp/native-marker/assets"
MODELS = ROOT / "build/recomp/native-models/assets"


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def page_portraits(art: Path) -> dict:
    """(image, palette) -> the whole HD portrait, like profile.py's portrait_lookup."""
    spec = json.loads((art / "srw64-portraits-hd.json").read_text())
    lookup = portrait_lookup(art, art / "page-portraits")
    index = {}
    for row in spec["images"]:
        for palette in (row["palette"], spec["silhouette"]["palette"]):
            if path := lookup(row["image"], palette):
                index[f"{row['image']}:{palette}"] = Path(path).relative_to(art).as_posix()
    document = {"schema": "srw64.page-portraits.v1", "portraits": index}
    (art / "srw64-page-portraits.json").write_text(json.dumps(document, indent=2) + "\n")
    return index


def prepare(output: Path, art_manifest: Path = ART, marker: Path = MARKER, models: Path = MODELS) -> dict:
    marker_check = load(ROOT / "tools/recomp/model5600/prepare_native_marker.py", "prepare_native_marker").validate(marker)
    models_check = load(ROOT / "tools/models/build_native_models.py", "build_native_models").validate(models)
    output.mkdir(parents=True, exist_ok=False)
    art = compile_art(ROOT, json.loads(art_manifest.read_text()), output / "art")
    pages = page_portraits(output / "art")
    shutil.copytree(marker, output / "native-marker")
    shutil.copytree(models, output / "native-models")
    report = {"schema": "srw64.hd-bundle.v1", "distribution": "local-only: AI art and ROM-derived reference bytes",
              "art_source_sha256": sha(art_manifest.read_bytes()),
              "art": {key: art[key] for key in ("count", "portraits", "backgrounds", "scene_images")},
              "page_portraits": len(pages),
              "native_marker": marker_check["manifest_sha256"], "native_models": models_check["manifest_sha256"]}
    (output / "hd.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True, help="new directory for the HD folder")
    parser.add_argument("--art", type=Path, default=ART)
    parser.add_argument("--native-marker", type=Path, default=MARKER)
    parser.add_argument("--native-models", type=Path, default=MODELS)
    args = parser.parse_args()
    print(json.dumps(prepare(args.output, args.art, args.native_marker, args.native_models), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
