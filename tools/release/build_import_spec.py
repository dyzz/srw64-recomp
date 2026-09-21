#!/usr/bin/env python3
"""Build-time metadata ONLY: no ROM, source text, portraits, fonts or compiler.

The native importer derives game data on the player's machine. This script
embeds the already tracked glyph mapping, locale overlays and layout metadata.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys


def build_spec(root: Path) -> dict:
    sys.path.insert(0, str(root / "src"))
    from srw64_rom.baseline import load_baseline
    from srw64_rom.glyphs import load_glyph_map
    from srw64_native.profile import UI_KEYS, load_profile
    from srw64_native.catalog import signature
    baseline = load_baseline(root / "config/srw64-jp-rev0.json")
    profile = load_profile(root / "config/recomp/profiles/play-profile.json", images="original")
    layout = baseline.text_layout
    if layout.allowed_control_words != frozenset((0xfffd, 0xfffe, 0xffff)):
        raise ValueError("Native importer control set must be updated")
    if baseline.rom["sha256"] != json.loads((root / "config/recomp/rom-variants.json").read_text(encoding="utf-8"))["variants"]["jp"]["sha256"]:
        raise ValueError("Game and importer baseline identities disagree")
    def source(relative):
        path = (root / relative).resolve(strict=True)
        if not path.is_relative_to(root.resolve()):
            raise ValueError("Locale source escapes repository")
        raw = path.read_bytes()
        return json.loads(raw), hashlib.sha256(raw).hexdigest()
    japanese, _ = source(profile["locales"]["ja"])
    locales = []
    for locale, relative in profile["locales"].items():
        document, digest = source(relative)
        if document["locale"] != locale or document["schema"] != "srw64.locale.v1" or document["source_locale"] != "ja":
            raise ValueError("Invalid locale registry")
        ui = {**japanese["ui"], **document["ui"]}
        if set(ui) != UI_KEYS or any(not isinstance(v, str) or not v for v in ui.values()):
            raise ValueError("Missing or invalid native UI labels")
        entries = []
        keys = set()
        for entry in document["entries"]:
            key = entry["key"]
            if key in keys:
                raise ValueError("Duplicate translation key")
            keys.add(key)
            signature(entry["target"])
            # Do not embed arbitrary author metadata, original text or paths.
            entries.append({name: entry[name] for name in ("key", "source_sha256", "target", "review_status") if name in entry})
        locales.append({"schema": "srw64.locale.v1", "source_locale": "ja", "locale": locale,
                        "display_name": document.get("display_name", locale), "font": document["font"],
                        "catalog_sha256": digest, "entries": entries, "ui": ui})
    p = profile["presentation"]
    return {"schema": "srw64.native-import-spec.v1", "baseline": "srw64-jp-rev0",
            "rom_sha256": baseline.rom["sha256"], "rom_size": baseline.rom["size"],
            "text_layout": {"pointer_table_offset": layout.pointer_table_offset,
                            "table_count": layout.table_count, "entry_header_size": layout.entry_header_size},
            "glyphs": {"0": " ", **{str(k): v for k, v in load_glyph_map(root).items()}},
            "locales": locales, "locale": p["locale"], "font_size": p["font_size"],
            "resolution_scale": p["resolution_scale"], "battle_art": True}


def write_changed(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    raw = (json.dumps(build_spec(args.root.resolve()), ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")
    write_changed(args.output, raw)
    if args.header:
        # A numeric array avoids MSVC string literal length limits and encoding.
        lines = ["#pragma once", "inline constexpr char srw64_import_spec[] = {"]
        lines += [",".join(str(v) for v in raw[i:i+64])+"," for i in range(0, len(raw), 64)]
        lines += ["0};", ""]
        write_changed(args.header, "\n".join(lines).encode("ascii"))

if __name__ == "__main__":
    main()
