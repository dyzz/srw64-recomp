#!/usr/bin/env python3
"""Capture OSTask descriptors and microcode at the game's osSpTaskLoad boundary."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from rsp_capture import BufferedRSPClient, ROOT, sha256

FIELDS = (
    "type", "flags", "ucode_boot", "ucode_boot_size", "ucode", "ucode_size",
    "ucode_data", "ucode_data_size", "dram_stack", "dram_stack_size",
    "output_buffer", "output_buffer_size_pointer", "data", "data_size",
    "yield_data", "yield_data_size",
)


def ram_address(pointer: int, size: int, memory_size: int) -> int:
    physical = pointer & 0x1FFFFFFF
    if physical + size > memory_size:
        raise RuntimeError(f"task memory outside installed RDRAM: {pointer:#x}+{size:#x}")
    return physical | 0x80000000


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=24)
    parser.add_argument("--snapshot-first-type", type=int, choices=(1, 2), help="also save RDRAM at the first matching task submission")
    parser.add_argument("--snapshot-following-task", action="store_true", help="save RDRAM/SP status before the next task is loaded")
    parser.add_argument("--already-halted", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.count <= 1024:
        raise RuntimeError("task count outside 1..1024")
    if args.snapshot_following_task and args.snapshot_first_type is None:
        raise RuntimeError("following-task snapshot requires --snapshot-first-type")
    session = json.loads(args.session.read_text())
    rom = Path(session["rom_path"])
    expected_hash = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text())["rom"]["sha256"]
    if sha256(rom) != expected_hash or session["rom_sha256"] != expected_hash:
        raise RuntimeError("ROM identity mismatch")
    rom_bytes = rom.read_bytes()
    args.output.mkdir(parents=True, exist_ok=False)
    client = BufferedRSPClient(session["host"], session["port"], 30)
    report: dict = {"schema": "srw64.recomp-rsp-tasks.v1", "status": "incomplete", "rom_sha256": expected_hash, "ares_sha256": sha256(Path("/Applications/ares.app/Contents/MacOS/ares")), "boundary": 0x800B4F00, "tasks": []}
    active: set[int] = set()
    connected = False
    try:
        client.connect()
        connected = True
        client.handshake()
        if not args.already_halted:
            client.halt()
        memory_size = client.rdram_size()
        report["os_mem_size"] = memory_size
        for index in range(args.count):
            entry = report["boundary"]
            client.expect_ok(f"Z0,{entry:x},4")
            active.add(entry)
            registers, events = client.continue_to(entry)
            pointer = registers[4] & 0xFFFFFFFF
            descriptor = client.read_memory(ram_address(pointer, 64, memory_size), 64)
            task = dict(zip(FIELDS, struct.unpack(">16I", descriptor)))
            observation: dict = {"index": index, "descriptor_address": pointer, "descriptor": task, "intermediate_events": events, "blobs": {}}
            if args.snapshot_following_task and "rdram_snapshot" in report and "following_task_snapshot" not in report:
                memory = client.read_memory(0x80000000, memory_size)
                (args.output / "rdram-following.bin").write_bytes(memory)
                report["following_task_snapshot"] = {
                    "task_index": index, "path": "rdram-following.bin", "size": memory_size,
                    "sha256": hashlib.sha256(memory).hexdigest(),
                    "sp_status": int.from_bytes(client.read_memory(0xA4040010, 4), "big"),
                    "boundary": "next consecutive osSpTaskLoad entry, before task loading"}
            if task["type"] == args.snapshot_first_type and "rdram_snapshot" not in report:
                memory = client.read_memory(0x80000000, memory_size)
                (args.output / "rdram-before.bin").write_bytes(memory)
                report["rdram_snapshot"] = {"task_index": index, "path": "rdram-before.bin",
                                            "size": memory_size, "sha256": hashlib.sha256(memory).hexdigest()}
            for key in ("ucode_boot", "ucode", "ucode_data", "data"):
                size = task[key + "_size"]
                if key == "ucode":
                    # This observed boot program loads 0xF80 bytes into IMEM
                    # at 0x1080, ignoring OSTask.ucode_size (audio sets it to 0).
                    boot_hash = observation["blobs"].get("ucode_boot", {}).get("sha256")
                    if boot_hash != "5759e9bb21f2e504bfbb3e5b75173cb81aa50c60b19e77bcee1d0f6fc34e8fa4":
                        raise RuntimeError("unknown boot microcode; transfer length needs analysis")
                    size = 0xF80
                    observation["effective_ucode_size"] = size
                    observation["ucode_size_basis"] = "verified boot instructions at ROM 0x4DEA8..0x4DEBC"
                maximum = 0x100000 if key == "data" else 0x10000
                if size > maximum:
                    raise RuntimeError(f"unexpected {key} size {size:#x}")
                if size == 0:
                    continue
                blob = client.read_memory(ram_address(task[key], size, memory_size), size)
                digest = hashlib.sha256(blob).hexdigest()
                (args.output / (digest + ".bin")).write_bytes(blob)
                match = rom_bytes.find(blob) if key != "data" else -1
                observation["blobs"][key] = {"sha256": digest, "size": size, "exact_rom_offset": match if match >= 0 else None}
            report["tasks"].append(observation)
            client.expect_ok(f"z0,{entry:x},4")
            active.remove(entry)
            return_pc = registers[31] & 0xFFFFFFFF
            client.expect_ok(f"Z0,{return_pc:x},4")
            active.add(return_pc)
            after, observation["return_intermediate_events"] = client.continue_to(return_pc)
            if after[29] != registers[29]:
                raise RuntimeError("task function return stack differs")
            client.expect_ok(f"z0,{return_pc:x},4")
            active.remove(return_pc)
        report["status"] = "task-submissions-observed"
    except Exception as exc:
        report.update({"status": "failed", "error": str(exc)})
        raise
    finally:
        if connected:
            try:
                for address in active:
                    client.expect_ok(f"z0,{address:x},4")
                client.resume()
            except Exception as exc:
                report["cleanup_error"] = str(exc)
            finally:
                client.close()
        (args.output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    unique = sorted({(task["descriptor"]["type"], task["blobs"]["ucode"]["sha256"]) for task in report["tasks"]})
    print(json.dumps({"status": report["status"], "task_count": len(report["tasks"]), "unique_type_ucode": unique}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
