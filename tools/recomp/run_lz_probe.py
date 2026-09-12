#!/usr/bin/env python3
"""Recompile original SRW64 LZ code and compare it with the independent decoder."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_rom.resources import ResourceTable  # noqa: E402


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit", type=int, default=64, help="0 means all resources")
    args = parser.parse_args()
    rom_path = ROOT / "rom.z64"
    baseline = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text())
    rom_hash = digest(rom_path)
    if rom_hash != baseline["rom"]["sha256"]:
        raise RuntimeError("ROM hash differs from baseline")
    work = ROOT / "build/recomp/lz-probe"
    work.mkdir(parents=True, exist_ok=True)
    recomp = ROOT / "build/recomp/tool-build/N64Recomp"
    subprocess.run([str(recomp), "config/recomp/lz-probe.toml"], cwd=ROOT, check=True)
    binary = work / "lz-probe"
    subprocess.run([
        "clang", "-std=c17", "-O2", "-fsanitize=address", "-fno-omit-frame-pointer",
        "-I" + str(ROOT / "build/recomp/upstream/N64Recomp/include"),
        "-I" + str(ROOT / "tools/recomp"), "-I" + str(work / "generated"),
        str(work / "generated/funcs_0.c"), str(ROOT / "tools/recomp/lz_probe.c"),
        "-o", str(binary),
    ], cwd=ROOT, check=True)
    rom = rom_path.read_bytes()
    resources = ResourceTable(rom)
    symbols = tomllib.loads((ROOT / "config/recomp/lz-probe.syms.toml").read_text())
    report: dict = {
        "schema": "srw64.recomp-lz-probe.v1", "status": "running",
        "evidence_scope": "isolated-native-functions-with-explicit-allocation-and-ROM-read-bindings",
        "rom_sha256": rom_hash, "recompiler_sha256": digest(recomp),
        "binary_sha256": digest(binary), "address_sanitizer": True,
        "symbols_sha256": digest(ROOT / "config/recomp/lz-probe.syms.toml"),
        "generated_c_sha256": digest(work / "generated/funcs_0.c"),
        "bindings_sha256": digest(ROOT / "tools/recomp/lz_probe.c"),
        "reference_decoder_sha256": digest(ROOT / "src/srw64_rom/resources.py"),
        "functions": [], "cases": [],
    }
    for function in symbols["section"][0]["functions"]:
        offset = function["vram"] - 0x80075610
        report["functions"].append({**function, "rom": offset, "sha256": hashlib.sha256(rom[offset:offset + function["size"]]).hexdigest()})
    ids = list(range(resources.count))
    if args.limit and args.limit < len(ids):
        ids = sorted(set(range(min(16, args.limit))) | {round(i * (resources.count - 1) / max(1, args.limit - 17)) for i in range(max(0, args.limit - 16))})
    started = time.monotonic()
    error_log = work / "native-stderr.log"
    with error_log.open("wb") as stderr:
        process = subprocess.Popen([str(binary), str(rom_path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr)
        assert process.stdin is not None and process.stdout is not None
        try:
            for resource_id in ids:
                entry = resources.entry(resource_id)
                expected, consumed = resources.extract(resource_id)
                process.stdin.write(f"{entry.compressed_offset:x} {entry.decoded_size}\n".encode())
                process.stdin.flush()
                header = process.stdout.readline().decode().split()
                if len(header) != 4:
                    raise RuntimeError(f"native probe failed at resource {resource_id}; see {error_log}")
                length, allocations, frees, reads = map(int, header)
                if length != len(expected):
                    raise RuntimeError("native output length mismatch")
                actual = process.stdout.read(length)
                if actual != expected:
                    raise RuntimeError(f"native/independent decoder mismatch at resource {resource_id}")
                report["cases"].append({"resource_id": resource_id, "decoded_size": length, "consumed": consumed, "rom_transfers": reads, "allocations": allocations, "frees": frees, "output_sha256": hashlib.sha256(actual).hexdigest(), "status": "byte-identical"})
                if len(report["cases"]) % 128 == 0:
                    print(f"verified {len(report['cases'])}/{len(ids)} resources", flush=True)
            process.stdin.close()
            if process.wait(timeout=10) != 0:
                raise RuntimeError("native probe exited unsuccessfully")
            report["status"] = "native-function-passed"
        except Exception as exc:
            report["status"] = "failed"
            report["error"] = str(exc)
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()
            report["elapsed_seconds"] = round(time.monotonic() - started, 3)
            report["case_count"] = len(report["cases"])
            (work / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({key: report[key] for key in ["status", "case_count", "elapsed_seconds", "evidence_scope"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
