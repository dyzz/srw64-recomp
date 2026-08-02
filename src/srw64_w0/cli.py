from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from . import __version__
from .baseline import BaselineError, inspect_rom, load_baseline, verify_rom
from .text_ir import (
    MANIFEST_SCHEMA,
    TextIRError,
    build_noop_rom,
    extract_ir,
    read_manifest,
    validate_ir,
    verify_manifest,
    write_json,
)


DEFAULT_BASELINE = Path("config/srw64-jp-rev0.json")


def _load_inputs(rom_path: Path, baseline_path: Path):
    baseline = load_baseline(baseline_path)
    rom = rom_path.read_bytes()
    identity = verify_rom(rom, baseline)
    return baseline, rom, identity


def _print(document: dict) -> None:
    print(json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True))


def _require_distinct_output(input_path: Path, output_path: Path) -> None:
    if input_path.resolve() == output_path.resolve():
        raise TextIRError("output ROM path must not overwrite the input ROM")


def _add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="srw64-w0",
        description="SRW64 W0 ROM gate and lossless text-table baseline",
    )
    parser.add_argument("--version", action="version", version=__version__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    gate = subparsers.add_parser("gate", help="verify the exact supported ROM")
    _add_common(gate)

    extract = subparsers.add_parser("extract", help="write the canonical lossless text IR")
    _add_common(extract)
    extract.add_argument("--ir", type=Path, required=True)
    extract.add_argument("--manifest", type=Path, required=True)

    validate = subparsers.add_parser("validate", help="compare every IR record with the ROM")
    _add_common(validate)
    validate.add_argument("--ir", type=Path, required=True)
    validate.add_argument("--manifest", type=Path, required=True)

    rebuild = subparsers.add_parser("rebuild-noop", help="rebuild an unchanged ROM from the IR")
    _add_common(rebuild)
    rebuild.add_argument("--ir", type=Path, required=True)
    rebuild.add_argument("--manifest", type=Path, required=True)
    rebuild.add_argument("--output", type=Path, required=True)

    accept = subparsers.add_parser("accept", help="run the complete W0 acceptance flow")
    _add_common(accept)
    accept.add_argument("--work-dir", type=Path, default=Path("build/w0"))
    return parser


def _manifest(baseline, identity: dict, ir_stats: dict) -> dict:
    return {
        "schema": MANIFEST_SCHEMA,
        "tool_version": __version__,
        "rom": identity,
        "text_layout": {
            "pointer_table_offset": baseline.text_layout.pointer_table_offset,
            "table_count": baseline.text_layout.table_count,
            "entry_header_size": baseline.text_layout.entry_header_size,
            "allowed_control_words": [
                f"{value:04X}" for value in sorted(baseline.text_layout.allowed_control_words)
            ],
        },
        "text_ir": ir_stats,
        "reference": baseline.reference,
    }


def run(args: argparse.Namespace) -> dict:
    baseline, rom, identity = _load_inputs(args.rom, args.baseline)
    if args.command == "gate":
        return {"gate": "passed", "rom": identity}

    if args.command == "extract":
        stats = extract_ir(rom, baseline.text_layout, args.ir)
        manifest = _manifest(baseline, identity, stats)
        write_json(args.manifest, manifest)
        return {
            "gate": "passed",
            "ir": str(args.ir),
            "manifest": str(args.manifest),
            "text_ir": stats,
        }

    manifest = read_manifest(args.manifest)
    verify_manifest(manifest, identity["sha256"], args.ir)
    validation = validate_ir(rom, baseline.text_layout, args.ir)
    if args.command == "validate":
        return {"gate": "passed", "validation": validation}
    if args.command == "rebuild-noop":
        _require_distinct_output(args.rom, args.output)
        rebuild = build_noop_rom(rom, baseline.text_layout, args.ir, args.output)
        return {"gate": "passed", "validation": validation, "rebuild": rebuild}
    if args.command == "accept":
        raise AssertionError("accept is handled before manifest loading")
    raise AssertionError(args.command)


def run_accept(args: argparse.Namespace) -> dict:
    baseline, rom, identity = _load_inputs(args.rom, args.baseline)
    work_dir = args.work_dir
    ir_path = work_dir / "text-ir.jsonl"
    manifest_path = work_dir / "manifest.json"
    output_path = work_dir / "rom.noop.z64"
    report_path = work_dir / "acceptance.json"
    _require_distinct_output(args.rom, output_path)

    stats = extract_ir(rom, baseline.text_layout, ir_path)
    manifest = _manifest(baseline, identity, stats)
    write_json(manifest_path, manifest)
    verify_manifest(manifest, identity["sha256"], ir_path)
    validation = validate_ir(rom, baseline.text_layout, ir_path)
    rebuild = build_noop_rom(rom, baseline.text_layout, ir_path, output_path)
    report = {
        "schema": "srw64.w0-acceptance.v1",
        "status": "passed",
        "rom_gate": identity,
        "text_ir": stats,
        "validation": validation,
        "rebuild": rebuild,
        "artifacts": {
            "ir": ir_path.name,
            "manifest": manifest_path.name,
            "noop_rom": output_path.name,
        },
    }
    write_json(report_path, report)
    return {**report, "report": str(report_path)}


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        result = run_accept(args) if args.command == "accept" else run(args)
    except (BaselineError, TextIRError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    _print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
