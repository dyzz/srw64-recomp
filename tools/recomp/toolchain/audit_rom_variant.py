#!/usr/bin/env python3
"""Verify that a pinned resource variant can reuse the reviewed native code."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.toolchain.analyze_layout import ROOT


class VariantError(RuntimeError):
    pass


def audit(baseline: bytes, candidate: bytes, expected_sha256: str, sections: dict) -> dict:
    if sections.get("schema") != "srw64.recomp-code-sections.v1":
        raise VariantError("unrecognized reviewed code section schema")
    baseline_hash = hashlib.sha256(baseline).hexdigest()
    candidate_hash = hashlib.sha256(candidate).hexdigest()
    if baseline_hash != sections["rom_sha256"]:
        raise VariantError("baseline differs from reviewed code section ROM")
    if candidate_hash != expected_sha256:
        raise VariantError("candidate differs from pinned resource variant")
    if len(baseline) < 0x100000 or len(candidate) != len(baseline):
        raise VariantError("candidate ROM size differs from baseline")
    if candidate[:0x100000] != baseline[:0x100000]:
        raise VariantError("initial 1 MiB load differs from baseline")
    verified: list[dict] = []
    for section in sections["sections"]:
        start, end = section["rom_start"], section["rom_end"]
        if not 0 <= start < end <= len(baseline):
            raise VariantError("reviewed loader range outside ROM")
        section_hash = hashlib.sha256(baseline[start:end]).hexdigest()
        if section_hash != section["sha256"]:
            raise VariantError(f"reviewed section hash differs: {section['name']}")
        if candidate[start:end] != baseline[start:end]:
            raise VariantError(f"candidate changes a full loader section: {section['name']}")
        verified.append({"name": section["name"], "rom_start": start, "rom_end": end,
                         "sha256": section_hash})
    if not verified:
        raise VariantError("reviewed loader section list is empty")
    return {"schema": "srw64.recomp-rom-compatibility.v1", "status": "static-compatible",
            "baseline_sha256": baseline_hash, "candidate_sha256": candidate_hash,
            "first_mib_identical": True, "full_loader_sections_identical": verified,
            "scope": "code and initialized loader data reuse; game execution and save compatibility remain separate"}


def load_variant(name: str) -> tuple[Path, dict, dict]:
    lock = json.loads((ROOT / "config/recomp/rom-variants.json").read_text())
    if lock.get("schema") != "srw64.recomp-rom-variants.v1":
        raise VariantError("unrecognized native ROM variant lock")
    if name not in lock["variants"]:
        raise VariantError("unknown native ROM variant")
    variant = lock["variants"][name]
    path = ROOT / variant["path"]
    baseline = ROOT / lock["variants"]["jp"]["path"]
    sections = json.loads((ROOT / "config/recomp/code-sections.json").read_text())
    report = audit(baseline.read_bytes(), path.read_bytes(), variant["sha256"], sections)
    return path, variant, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("jp", "model5600"), default="jp")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    path, variant, report = load_variant(args.variant)
    report.update({"variant": args.variant, "path": str(path), "runtime_xxh3_64": variant["xxh3_64"]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as target:
        target.write(json.dumps(report, indent=2) + "\n")
    print(f"{args.variant}: {len(report['full_loader_sections_identical'])} full loader sections and initial 1 MiB identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
