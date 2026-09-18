#!/usr/bin/env python3
"""Observe an original in-game LZ call, including its return and decoded bytes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.probes.rsp_capture import BufferedRSPClient, ROOT, sha256

sys.path.insert(0, str(ROOT / "src"))
from srw64_rom.resources import ResourceTable, lz_decode  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--already-halted", action="store_true")
    args = parser.parse_args()
    session = json.loads(args.session.read_text())
    rom = Path(session["rom_path"])
    expected_hash = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text())["rom"]["sha256"]
    if sha256(rom) != expected_hash or session["rom_sha256"] != expected_hash:
        raise RuntimeError("ROM identity mismatch")
    args.output.mkdir(parents=True, exist_ok=False)
    client = BufferedRSPClient(session["host"], session["port"], 30)
    report: dict = {"schema": "srw64.recomp-lz-runtime.v1", "status": "incomplete", "rom_sha256": expected_hash, "ares_sha256": sha256(Path("/Applications/ares.app/Contents/MacOS/ares"))}
    active_breakpoints: set[int] = set()
    connected = False
    try:
        client.connect()
        connected = True
        client.handshake()
        if not args.already_halted:
            client.halt()
        entry = 0x800897AC
        client.expect_ok(f"Z0,{entry:x},4")
        active_breakpoints.add(entry)
        before, report["entry_intermediate_events"] = client.continue_to(entry)
        report["observed_entry_pc"] = before[37] & 0xFFFFFFFF
        if before[37] & 0xFFFFFFFF != entry:
            raise RuntimeError(f"unexpected function entry stop: {before[37]:#x}")
        source, destination, size = (value & 0xFFFFFFFF for value in before[4:7])
        return_pc = before[31] & 0xFFFFFFFF
        memory_size = client.rdram_size()
        report["os_mem_size"] = memory_size
        if not 0x80000000 <= destination <= 0x80000000 + memory_size - size:
            raise RuntimeError("unexpected LZ destination range")
        report.update({"entry_pc": entry, "return_pc": return_pc, "source_rom_offset": source, "destination": destination, "decoded_size": size, "entry_registers_u64": [f"{x:016x}" for x in before]})
        client.expect_ok(f"z0,{entry:x},4")
        active_breakpoints.remove(entry)
        client.expect_ok(f"Z0,{return_pc:x},4")
        active_breakpoints.add(return_pc)
        after, report["return_intermediate_events"] = client.continue_to(return_pc)
        if after[37] & 0xFFFFFFFF != return_pc or after[29] != before[29]:
            raise RuntimeError("unexpected return PC/stack")
        result = client.read_memory(destination, size)
        expected, consumed = lz_decode(rom.read_bytes()[source:], size)
        if result != expected:
            raise RuntimeError("original game output differs from independent decoder")
        (args.output / "decoded.bin").write_bytes(result)
        table = ResourceTable(rom.read_bytes())
        resource_ids = [i for i in range(table.count) if table.entry(i).compressed_offset == source]
        report.update({"status": "runtime-byte-identical", "resource_ids": resource_ids, "consumed": consumed, "output_sha256": hashlib.sha256(result).hexdigest(), "return_registers_u64": [f"{x:016x}" for x in after]})
    except Exception as exc:
        report.update({"status": "failed", "error": str(exc)})
        raise
    finally:
        if connected:
            try:
                for address in active_breakpoints:
                    client.expect_ok(f"z0,{address:x},4")
                client.resume()
            except Exception as exc:
                report["cleanup_error"] = str(exc)
            finally:
                client.close()
        (args.output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({key: value for key, value in report.items() if "registers" not in key}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
