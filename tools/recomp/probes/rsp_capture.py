#!/usr/bin/env python3
"""Capture a ROM-gated, stopped CPU/RDRAM snapshot through ares GDB RSP."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.probes.ares_rsp_probe import AresRSPClient  # noqa: E402


class CaptureError(RuntimeError):
    pass


class BufferedRSPClient(AresRSPClient):
    """Handle fragmented/coalesced TCP replies without a recv per hex byte."""

    def __init__(self, host: str, port: int, timeout: float) -> None:
        super().__init__(host, port, timeout)
        self.buffer = bytearray()

    def _fill(self) -> None:
        chunk = self._require_socket().recv(65536)
        if not chunk:
            raise EOFError("ares closed the GDB connection")
        self.buffer.extend(chunk)

    def receive(self) -> str:
        while True:
            start = self.buffer.find(b"$")
            if start < 0:
                self.buffer.clear()
                self._fill()
                continue
            if start:
                del self.buffer[:start]
            end = self.buffer.find(b"#", 1)
            if end < 0 or len(self.buffer) < end + 3:
                self._fill()
                continue
            payload = bytes(self.buffer[1:end])
            checksum = bytes(self.buffer[end + 1:end + 3])
            del self.buffer[:end + 3]
            if checksum.lower() != f"{sum(payload) & 255:02x}".encode():
                raise CaptureError("RSP checksum mismatch; refusing snapshot")
            self._require_socket().sendall(b"+")
            return payload.decode("ascii")

    def read_memory(self, address: int, size: int, chunk_size: int = 0x700) -> bytes:
        result = bytearray()
        for offset in range(0, size, chunk_size):
            length = min(chunk_size, size - offset)
            reply, _ = self.memory(address + offset, length)
            block = bytes.fromhex(reply)
            if len(block) != length:
                raise CaptureError("short memory reply")
            result.extend(block)
        return bytes(result)

    def rdram_size(self) -> int:
        """Read the boot-established osMemSize, refusing an unknown layout."""
        size = int.from_bytes(self.read_memory(0x80000318, 4), "big")
        if size not in (0x400000, 0x800000):
            raise CaptureError(f"unsupported/uninitialized osMemSize: {size:#x}")
        return size

    def continue_to(self, address: int) -> tuple[list[int], list[dict]]:
        """Record lazy COP1 traps and let the original exception handler run.

        ares v148 reports CPU exception 11 as GDB S10 (URG). Its PC override
        points at the faulting instruction although CPU control enters the
        original exception vector. Other unexpected stops remain failures.
        """
        events: list[dict] = []
        deadline = time.monotonic() + self.timeout
        for _ in range(64):
            stop = self.continue_until_stop().payload
            registers, _ = self.registers()
            pc = registers[37] & 0xFFFFFFFF
            if pc == address and (stop.startswith("S05") or stop.startswith("T05")):
                return registers, events
            word = int.from_bytes(self.read_memory(pc, 4), "big")
            event = {"stop": stop, "pc": pc, "instruction": f"{word:08x}"}
            # COP1 arithmetic/transfers and COP1 loads/stores may lazily enable
            # FPU state through libultra's exception handler.
            if stop != "S10" or word >> 26 not in (0x11, 0x31, 0x35, 0x39, 0x3D):
                raise CaptureError(f"unexpected stop before {address:#x}: {event}")
            events.append(event)
            if time.monotonic() >= deadline:
                break
        raise CaptureError(f"breakpoint {address:#x} not reached; intermediate events: {events}")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(args: argparse.Namespace) -> dict:
    session = json.loads(args.session.read_text())
    baseline = json.loads(args.baseline.read_text())
    if session.get("schema") != "srw64.ares-session.v1":
        raise CaptureError("unsupported ares session")
    rom = Path(session["rom_path"])
    rom_hash = sha256(rom)
    if rom_hash != baseline["rom"]["sha256"] or rom_hash != session["rom_sha256"]:
        raise CaptureError("ROM/session/baseline hashes differ")
    args.output.mkdir(parents=True, exist_ok=False)
    client = BufferedRSPClient(session["host"], session["port"], args.timeout)
    report: dict = {
        "schema": "srw64.recomp-rsp-capture.v1",
        "status": "incomplete",
        "rom_sha256": rom_hash,
        "session_sha256": sha256(args.session),
        "ares_sha256": sha256(Path("/Applications/ares.app/Contents/MacOS/ares")),
        "breakpoint": args.breakpoint,
        "memory_base": 0x80000000,
        "requested_memory_size": args.ram_size,
    }
    breakpoint_added = False
    connected = False
    try:
        client.connect()
        connected = True
        capabilities, initial_status = client.handshake()
        report["capabilities"] = capabilities.payload
        report["initial_status"] = initial_status.payload
        if not args.already_halted:
            client.halt()
        if args.breakpoint is not None:
            client.expect_ok(f"Z0,{args.breakpoint:08x},4")
            breakpoint_added = True
            _, report["intermediate_events"] = client.continue_to(args.breakpoint)
        registers, _ = client.registers()
        report["registers_u64"] = [f"{value:016x}" for value in registers]
        report["pc"] = registers[37] & 0xFFFFFFFF
        if args.breakpoint is not None and report["pc"] != args.breakpoint:
            raise CaptureError(f"unexpected stop at {report['pc']:#x}")
        if breakpoint_added:
            client.expect_ok(f"z0,{args.breakpoint:08x},4")
            breakpoint_added = False
        installed_size = client.rdram_size()
        memory_size = args.ram_size or installed_size
        if memory_size > installed_size:
            raise CaptureError("requested dump exceeds installed RDRAM")
        report.update({"memory_size": memory_size, "os_mem_size": installed_size,
                       "complete_rdram": memory_size == installed_size})
        data = client.read_memory(0x80000000, memory_size)
        if data[:64] != client.read_memory(0xA0000000, 64):
            raise CaptureError("cached/uncached RDRAM aliases differ")
        (args.output / "rdram.bin").write_bytes(data)
        report["memory_sha256"] = hashlib.sha256(data).hexdigest()
        report["status"] = "runtime-observed"
        return report
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        if connected:
            try:
                if breakpoint_added:
                    client.expect_ok(f"z0,{args.breakpoint:08x},4")
                client.resume()
                report["resumed"] = True
            except Exception as exc:
                report["cleanup_error"] = str(exc)
            client.close()
        (args.output / "capture.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, default=ROOT / "config/srw64-jp-rev0.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--breakpoint", type=lambda value: int(value, 0))
    parser.add_argument("--ram-size", type=lambda value: int(value, 0), choices=(0x400000, 0x800000), help="default: boot-established osMemSize")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--already-halted", action="store_true")
    args = parser.parse_args()
    try:
        result = capture(args)
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({key: value for key, value in result.items() if key != "registers_u64"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
