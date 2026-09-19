#!/usr/bin/env python3
"""Recheck n64sym candidates against complete relocation-masked signatures.

An exported n64sym label can be inferred from another function's relocation,
not a match of the labeled function itself. Keep those evidence classes apart.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
import zlib

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.toolchain.analyze_layout import MAIN_DELTA, ROOT, analyze


def read_signatures(path: Path) -> dict[str, list[dict]]:
    signatures: dict[str, list[dict]] = defaultdict(list)
    current: dict | None = None
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        if text.startswith("."):
            if current is None:
                raise RuntimeError("signature relocation without symbol")
            kind, name, *offsets = text.split()
            if kind not in (".targ26", ".hi16", ".lo16"):
                raise RuntimeError(f"unsupported signature relocation {kind}")
            current["relocations"].extend({"kind": kind, "symbol": name, "offset": int(offset, 0)} for offset in offsets)
        else:
            name, size, first_crc, full_crc = text.split()
            current = {"name": name, "size": int(size, 0), "first_crc": int(first_crc, 0),
                       "full_crc": int(full_crc, 0), "relocations": [], "line": line_number}
            signatures[name].append(current)
    return signatures


def matches(data: bytes, signature: dict) -> bool:
    if len(data) < signature["size"]:
        return False
    normalized = bytearray(data[:signature["size"]])
    for relocation in signature["relocations"]:
        offset = relocation["offset"]
        if offset & 3 or offset + 4 > len(normalized):
            raise RuntimeError("invalid signature relocation extent")
        if relocation["kind"] == ".targ26":
            normalized[offset] &= 0xFC
            normalized[offset + 1:offset + 4] = bytes(3)
        else:
            normalized[offset + 2:offset + 4] = bytes(2)
    return zlib.crc32(normalized[:8]) == signature["first_crc"] and zlib.crc32(normalized) == signature["full_crc"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbols", type=Path, default=ROOT / "config/recomp/n64sym-symbols.txt")
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/library-audit.json")
    args = parser.parse_args()
    rom = (ROOT / "rom.z64").read_bytes()
    analyze(rom, None)  # Exact ROM gate.
    signature_path = ROOT / "build/recomp/upstream/n64sym/src/builtin_signatures.sig"
    signatures = read_signatures(signature_path)
    with (ROOT / "build/recomp/cpu-scan/resident/functions.csv").open() as source:
        functions = {int(row["address"], 0): row for row in csv.DictReader(source)}
    symbol_lists = (ROOT / "build/recomp/upstream/N64Recomp/src/symbol_lists.cpp").read_text()
    reimplemented_text = symbol_lists.split("N64Recomp::reimplemented_funcs {", 1)[1].split("};", 1)[0]
    ignored_text = symbol_lists.split("N64Recomp::ignored_funcs {", 1)[1].split("};", 1)[0]
    reimplemented = set(re.findall(r'"([^"]+)"', reimplemented_text))
    ignored = set(re.findall(r'"([^"]+)"', ignored_text)) - reimplemented
    records: list[dict] = []
    for name, address_text in re.findall(r"^(\w+) = (0x[0-9A-Fa-f]+);$", args.symbols.read_text(), re.MULTILINE):
        address = int(address_text, 0)
        if address not in functions:
            continue
        function = functions[address]
        offset = address - MAIN_DELTA
        matching = [sig for sig in signatures.get(name, []) if matches(rom[offset:], sig)]
        record = {"name": name, "vram": address, "rom": offset, "candidate_size": int(function["length"], 0),
                  "evidence": "normalized-code-match" if matching else "label-only-unverified",
                  "runtime_action": "reimplemented" if name in reimplemented else "ignored-internal" if name in ignored else "retain-game-code",
                  "matches": []}
        for signature in matching:
            record["matches"].append({"signature_line": signature["line"], "size": signature["size"],
                                      "sha256": hashlib.sha256(rom[offset:offset + signature["size"]]).hexdigest(),
                                      "masked_crc32": f"{signature['full_crc']:08x}", "relocations": signature["relocations"]})
        records.append(record)
    report = {"schema": "srw64.recomp-library-audit.v1", "rom_sha256": hashlib.sha256(rom).hexdigest(),
              "signature_file_sha256": hashlib.sha256(signature_path.read_bytes()).hexdigest(),
              "source_symbols_sha256": hashlib.sha256(args.symbols.read_bytes()).hexdigest(), "symbols": records}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"candidate_function_labels": len(records), "full_signature_matches": sum(bool(row["matches"]) for row in records),
                      "runtime_replacements_with_full_matches": sum(bool(row["matches"]) and row["runtime_action"] == "reimplemented" for row in records)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
