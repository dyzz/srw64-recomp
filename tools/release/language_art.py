#!/usr/bin/env python3
"""Copy the game's own pictures for a language into a package's art/ (the Korean title logo,
native_sprite.cpp): content/art/language-images.json and, per language, the files it names
that content/art/<locale>/ has. The launcher hands the folder to the host (SRW64_LANGUAGE_ART).

    tools/release/language_art.py OUTPUT_ART_DIR
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "content/art"


def copy_language_art(target: Path) -> int:
    index = SOURCE / "language-images.json"
    files = [row["file"] for row in json.loads(index.read_text(encoding="utf-8"))["images"]]
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(index, target / index.name)
    copied = 0
    for folder in sorted(p for p in SOURCE.iterdir() if p.is_dir()):
        for file in files:
            if (folder / file).is_file():
                (target / folder.name / file).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(folder / file, target / folder.name / file)
                copied += 1
    return copied


if __name__ == "__main__":
    print(f"language pictures: {copy_language_art(Path(sys.argv[1]))}")
