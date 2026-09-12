"""Load the original Japanese glyph map with its independent source lock."""
from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path


def glyph_map_path(root: Path) -> Path:
    lock = json.loads((root / "config/data/original-glyph-map.json").read_text())
    if lock.get("schema") != "srw64.original-glyph-map.v1":
        raise ValueError("Unsupported original glyph map lock")
    path = root / lock["path"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != lock["sha256"]:
        raise ValueError("Original glyph map differs from lock")
    return path


def load_glyph_map(root: Path) -> dict[int, str]:
    mapping: dict[int, str] = {}
    with glyph_map_path(root).open(encoding="utf-8-sig", newline="") as source:
        for row in csv.DictReader(source):
            if not row.get("char"):
                continue
            glyph = int(row["glyph_id"], 0)
            if glyph in mapping:
                raise ValueError(f"Duplicate original glyph ID: {glyph}")
            mapping[glyph] = row["char"]
    if not mapping:
        raise ValueError("Original glyph map is empty")
    return mapping
