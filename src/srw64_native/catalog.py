"""Stable text identities and checked Unicode overlays on the original ROM."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

from srw64_rom.baseline import load_baseline
from srw64_rom.glyphs import load_glyph_map
from srw64_rom.text import parse_entries

CONTROL = {0xFFFD: "<STOP>", 0xFFFE: "<BR>", 0xFFFF: "<END>"}
TOKEN = re.compile(r"<(BR|STOP|END|G:[0-9A-Fa-f]{1,4})>")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def text_key(table: int, index: int) -> str:
    if type(table) is not int or type(index) is not int or not 0 <= table < 100 or not 0 <= index < 65536:
        raise ValueError("TextKey outside supported table/index range")
    return f"base:t{table:02d}_{index:05d}"


def signature(text: str) -> list[list[int]]:
    """Each script segment owns its parameters; BR and text length are free."""
    if not isinstance(text, str) or not text.endswith("<END>"):
        raise ValueError("Message must end with <END>")
    if "<" in TOKEN.sub("", text) or "\x00" in text or any(c in text for c in "\r\n\f"):
        raise ValueError("Unsupported message token/control; use <BR>")
    result: list[list[int]] = [[]]
    for token in TOKEN.findall(text):
        if token in ("STOP", "END"):
            result[-1].append(0xFFFD if token == "STOP" else 0xFFFF)
            result.append([])
        elif token.startswith("G:"):
            # All special glyphs, including repeated dynamic-name slots, must
            # survive in the same segment. Literal Unicode remains editable.
            result[-1].append(int(token[2:], 16))
    if any(0xFFFF in segment for segment in result[:-2]):
        raise ValueError("Message contains an early <END>")
    return result


def source_catalog(root: Path, rom_path: Path) -> tuple[dict, dict, dict]:
    baseline = load_baseline(root / "config/srw64-jp-rev0.json")
    rom = rom_path.read_bytes()
    if sha(rom) != baseline.rom["sha256"]:
        raise ValueError("Native content requires the pinned original JP ROM")
    glyphs = {"0": " ", **{str(key): value for key, value in load_glyph_map(root).items()}}
    sources, hashes = {}, {}
    for entry in parse_entries(rom, baseline.text_layout):
        key = text_key(entry.table_id, entry.text_id)
        sources[key] = "".join(CONTROL.get(u, f"<G:{u:04X}>" if 0x124 <= u <= 0x12C
                                             else glyphs.get(str(u), f"<G:{u:04X}>")) for u in entry.units)
        hashes[key] = sha(entry.data)
    return sources, hashes, glyphs


def text_headers(root: Path, rom_path: Path) -> dict:
    """Raw 8-byte entry headers by TextKey; the script speaker rule reads their first digits."""
    baseline = load_baseline(root / "config/srw64-jp-rev0.json")
    rom = rom_path.read_bytes()
    if sha(rom) != baseline.rom["sha256"]:
        raise ValueError("Native content requires the pinned original JP ROM")
    return {text_key(entry.table_id, entry.text_id): entry.header
            for entry in parse_entries(rom, baseline.text_layout)}


def compile_locale(document: dict, sources: dict, hashes: dict) -> dict:
    if document.get("schema") != "srw64.locale.v1" or document.get("source_locale") != "ja":
        raise ValueError("Unsupported locale schema or source language")
    if not re.fullmatch(r"[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*", document.get("locale", "")):
        raise ValueError("Invalid locale tag")
    if not isinstance(document.get("font"), str) or not document["font"].strip():
        raise ValueError("Locale must declare a font PostScript name")
    selected = {}
    for row in document["entries"]:
        if row.get("review_status", "draft") not in ("draft", "reviewed"):
            raise ValueError("Unsupported translation review status")
        key = row["key"]
        if key not in sources or key in selected:
            raise ValueError(f"Unknown or duplicate TextKey: {key}")
        if row.get("source_sha256") != hashes[key]:
            raise ValueError(f"Translation source changed: {key}")
        if signature(row["target"]) != signature(sources[key]):
            raise ValueError(f"Translation changes script barriers or parameters: {key}")
        selected[key] = row["target"]
    return selected
