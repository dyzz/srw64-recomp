#!/usr/bin/env python3
"""Recover the verified SRW64 loader family and audit an optional RDRAM capture.

This is a bounded constant evaluator for the straight-line loader wrappers,
not a general MIPS emulator or a heuristic scan of arbitrary ROM data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
MAIN_DELTA = 0x80075610
ROM_READ = 0x8007F704
LOADER_START = 0x8007FD80
LOADER_END = 0x8008016C


class LayoutError(RuntimeError):
    pass


def step_constants(word: int, registers: list[int | None]) -> None:
    op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
    immediate = word & 0xFFFF
    signed = immediate if immediate < 0x8000 else immediate - 0x10000
    if word == 0:
        return
    if op == 15:  # lui
        registers[rt] = immediate << 16
    elif op in (9, 13):  # addiu, ori
        value = registers[rs]
        registers[rt] = None if value is None else ((value + signed) if op == 9 else (value | immediate)) & 0xFFFFFFFF
    elif op == 0 and word & 63 in (0x21, 0x23):  # addu, subu
        left, right = registers[rs], registers[rt]
        registers[rd] = None if left is None or right is None else (left + right if word & 63 == 0x21 else left - right) & 0xFFFFFFFF
    elif op == 0x23:  # lw: restored stack values are unknown
        registers[rt] = None
    elif op == 0x2B:  # sw
        pass
    else:
        raise LayoutError(f"unsupported instruction in verified loader family: {word:08x}")
    registers[0] = 0


def recover_loaders(rom: bytes) -> list[dict]:
    pc = LOADER_START
    registers: list[int | None] = [0] + [None] * 31
    wrapper = pc
    transfers: list[dict] = []
    while pc < LOADER_END:
        word, delay = struct.unpack_from(">II", rom, pc - MAIN_DELTA)
        if word >> 26 == 3:
            target = ((pc + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
            step_constants(delay, registers)
            if target == ROM_READ:
                source, destination, size = registers[4:7]
                if source is None or destination is None or size is None:
                    raise LayoutError(f"unresolved loader arguments at {pc:#x}")
                if source + size > len(rom) or not 0x80000000 <= destination <= 0x80800000 - size:
                    raise LayoutError(f"loader outside ROM/RDRAM at {pc:#x}")
                transfers.append({"wrapper": wrapper, "call_pc": pc, "rom_start": source,
                                  "rom_end": source + size, "vram": destination, "size": size})
            elif target != 0x8007FD20:
                raise LayoutError(f"unexpected call in loader wrapper: {target:#x}")
            for register in (*range(2, 16), 24, 25, 31):
                registers[register] = None
            pc += 8
        elif word == 0x03E00008:  # jr ra ends a wrapper
            step_constants(delay, registers)
            registers = [0] + [None] * 31
            pc += 8
            wrapper = pc
        else:
            step_constants(word, registers)
            pc += 4
    if pc != LOADER_END:
        raise LayoutError("loader family ends inside an instruction/delay slot")
    return transfers


def analyze(rom: bytes, capture: Path | None) -> dict:
    digest = hashlib.sha256(rom).hexdigest()
    baseline = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text())
    if digest != baseline["rom"]["sha256"]:
        raise LayoutError("ROM differs from the version whose loader boundaries were reviewed")
    transfers = recover_loaders(rom)
    segments: dict[tuple[int, int, int], dict] = {}
    for transfer in transfers:
        key = transfer["rom_start"], transfer["vram"], transfer["size"]
        if key not in segments:
            source, destination, size = key
            segments[key] = {"name": f"load_{source:08X}", "rom_start": source,
                             "rom_end": source + size, "vram": destination, "size": size,
                             "sha256": hashlib.sha256(rom[source:source + size]).hexdigest(),
                             "status": "static-loader-verified", "content_class": "unclassified",
                             "loader_calls": []}
        segments[key]["loader_calls"].append({"wrapper": transfer["wrapper"], "call_pc": transfer["call_pc"]})
    report: dict = {"schema": "srw64.recomp-layout.v1", "rom_sha256": digest,
                    "resident": {"rom_start": 0x1000, "rom_end": 0x5BC30, "vram": 0x80076610,
                                 "cpu_text_rom_end": 0x4DEA0, "bss_start": 0x800D1240, "bss_end": 0x8018DAC0},
                    "loader_family": {"start": LOADER_START, "end": LOADER_END,
                                      "sha256": hashlib.sha256(rom[LOADER_START - MAIN_DELTA:LOADER_END - MAIN_DELTA]).hexdigest()},
                    "transfers": transfers, "segments": sorted(segments.values(), key=lambda item: item["rom_start"])}
    if capture is not None:
        metadata = json.loads((capture / "capture.json").read_text())
        memory = (capture / "rdram.bin").read_bytes()
        if metadata["status"] != "runtime-observed" or metadata["rom_sha256"] != digest:
            raise LayoutError("capture status/ROM does not match")
        if hashlib.sha256(memory).hexdigest() != metadata["memory_sha256"] or len(memory) != metadata["memory_size"]:
            raise LayoutError("capture memory identity/size differs")
        report["capture"] = {"path": str(capture.resolve()), "sha256": metadata["memory_sha256"], "size": len(memory)}
        for segment in report["segments"]:
            offset = segment["vram"] & 0x1FFFFFFF
            size, source = segment["size"], segment["rom_start"]
            if size and offset + size <= len(memory):
                equal = memory[offset:offset + size] == rom[source:source + size]
                segment["capture_full_range_equal"] = equal
                if equal:
                    segment["status"] = "runtime-bytes-observed"
    external: dict[int, list[int]] = {}
    for offset in range(0x1000, 0x4DEA0, 4):
        word = struct.unpack_from(">I", rom, offset)[0]
        if word >> 26 == 3:
            target = 0x80000000 | ((word & 0x3FFFFFF) << 2)
            if not 0x80076610 <= target < 0x800C34B0:
                external.setdefault(target, []).append(offset + MAIN_DELTA)
    report["resident_external_jal_targets"] = [
        {"target": target, "call_sites": sites,
         "covering_segments": [segment["name"] for segment in report["segments"]
                               if segment["vram"] <= target < segment["vram"] + segment["size"]]}
        for target, sites in sorted(external.items())]
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/layout.json")
    args = parser.parse_args()
    report = analyze(args.rom.read_bytes(), args.capture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"transfers": len(report["transfers"]), "distinct_ranges": len(report["segments"]),
                      "resident_external_jal_targets": len(report["resident_external_jal_targets"]),
                      "runtime_matching_ranges": [segment["name"] for segment in report["segments"] if segment.get("capture_full_range_equal")]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
