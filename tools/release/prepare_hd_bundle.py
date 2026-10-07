#!/usr/bin/env python3
"""Prepare the HD folder of a full-HD app (tools/release/package_macos.py --hd).

Compiles the art manifest (content/art/stage1-hd.json) from the local assets/:
the RT64 replacement textures (world-map surfaces, space objects, dialogue frame),
the whole HD portraits, intermission backgrounds and title images. Stores the
portraits and backgrounds as JPEG (compress_hd.py), indexes the HD portraits the
native pages show by (image, palette), the silhouette being each portrait's alpha
file, and copies the world-map model pack and the 5600 marker pack after
validating them. The app's launcher starts in HD when it finds this folder.

hd.json records content_sha256, a digest of every file in the folder (paths and
contents): build_release.py gives the pack a new HD version only when it changes.

The result is a public download: AI-generated art (Alibaba Cloud Qwen image models;
the world map with OpenAI's image model via Codex image_gen) and no ROM data;
build_release.py adds NOTICE.txt.
The HD tactical maps come with the art pack (art/maps); build_release.py leaves them out
of the public pack as ROM-derived."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.assets import compile_art  # noqa: E402
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
    """(image, palette) -> the whole HD portrait, like profile.py's portrait_lookup.

    After compress_hd, a portrait's alpha file is a grey+alpha PNG in the
    silhouette grey, so it is also the silhouette portrait."""
    spec = json.loads((art / "srw64-portraits-hd.json").read_text())
    index = {}
    for row in spec["images"]:
        index[f"{row['image']}:{row['palette']}"] = row["file"]
        index[f"{row['image']}:{spec['silhouette']['palette']}"] = row["alpha"]
    document = {"schema": "srw64.page-portraits.v1", "portraits": index}
    (art / "srw64-page-portraits.json").write_text(json.dumps(document, indent=2) + "\n")
    return index


def prepare(output: Path, art_manifest: Path = ART, marker: Path = MARKER, models: Path = MODELS) -> dict:
    marker_check = load(ROOT / "tools/recomp/model5600/prepare_native_marker.py", "prepare_native_marker").validate(marker)
    models_check = load(ROOT / "tools/models/build_native_models.py", "build_native_models").validate(models)
    output.mkdir(parents=True, exist_ok=False)
    art = compile_art(ROOT, json.loads(art_manifest.read_text()), output / "art")
    compressed = load(ROOT / "tools/release/compress_hd.py", "compress_hd").compress(output / "art")
    pages = page_portraits(output / "art")
    shutil.copytree(marker, output / "native-marker")
    shutil.copytree(models, output / "native-models")
    report = {"schema": "srw64.hd-bundle.v1", "distribution": "public: AI-generated art (Alibaba Cloud Qwen and Wanx image models; world map, tactical maps and some battle cut-ins and extra unit images: OpenAI image model via Codex) and images redrawn from the original graphics (unit poses, map unit icons, battle cut-ins, props and ground textures, the tactical maps' palette-index maps); see NOTICE.txt",
              "art_source_sha256": sha(art_manifest.read_bytes()),
              "art": {key: art[key] for key in ("count", "portraits", "units", "unit_extras", "battle_sprites", "backgrounds", "scene_images", "tactical_maps")},
              "page_portraits": len(pages), "compressed": compressed,
              "native_marker": marker_check["manifest_sha256"], "native_models": models_check["manifest_sha256"],
              "content_sha256": content_sha256(output)}
    (output / "hd.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def content_sha256(folder: Path) -> str:
    """The pack's contents, apart from hd.json and NOTICE.txt: each file's path and sha256."""
    digest = hashlib.sha256()
    for path in sorted(p for p in folder.rglob("*") if p.is_file()):
        name = path.relative_to(folder).as_posix()
        if name not in ("hd.json", "NOTICE.txt"):
            digest.update(f"{name}\0{sha(path.read_bytes())}\n".encode())
    return digest.hexdigest()


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
