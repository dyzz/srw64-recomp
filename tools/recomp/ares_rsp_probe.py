#!/usr/bin/env python3
"""Smoke-test ares' N64 GDB Remote Serial Protocol server.

The probe uses only Python's standard library.  It deliberately handles
asynchronous stop packets because ares may report several watchpoint hits
before the emulation thread has fully halted.
"""

from __future__ import annotations

import argparse
import json
import socket
import string
from collections.abc import Callable
from dataclasses import dataclass, field


HEX_DIGITS = frozenset(string.hexdigits)


def is_hex_payload(payload: str, expected_chars: int | None = None) -> bool:
    if expected_chars is not None and len(payload) != expected_chars:
        return False
    return bool(payload) and all(character in HEX_DIGITS for character in payload)


@dataclass
class PacketResult:
    payload: str
    skipped: list[str] = field(default_factory=list)


class AresRSPClient:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        family = socket.AF_INET6 if ":" in self.host else socket.AF_INET
        self.sock = socket.socket(family, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        # ares requires '+' to be the first byte from a newly connected client.
        self.sock.sendall(b"+")

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def _require_socket(self) -> socket.socket:
        if self.sock is None:
            raise RuntimeError("RSP client is not connected")
        return self.sock

    @staticmethod
    def _frame(payload: str) -> bytes:
        raw = payload.encode("ascii")
        checksum = f"{sum(raw) & 0xff:02x}".encode("ascii")
        return b"$" + raw + b"#" + checksum

    def send(self, payload: str) -> None:
        self._require_socket().sendall(self._frame(payload))

    def receive(self) -> str:
        sock = self._require_socket()
        while True:
            current = sock.recv(1)
            if not current:
                raise EOFError("ares closed the connection before a packet")
            if current == b"$":
                break

        payload = bytearray()
        while True:
            current = sock.recv(1)
            if not current:
                raise EOFError("ares closed the connection inside a packet")
            if current == b"#":
                break
            payload.extend(current)

        wire_checksum = sock.recv(2)
        expected_checksum = f"{sum(payload) & 0xff:02x}".encode("ascii")
        if wire_checksum.lower() != expected_checksum:
            raise ValueError(
                f"bad RSP checksum: got {wire_checksum!r}, expected {expected_checksum!r}"
            )
        sock.sendall(b"+")
        return payload.decode("ascii", "replace")

    def receive_until(
        self, accept: Callable[[str], bool], limit: int = 128
    ) -> PacketResult:
        skipped: list[str] = []
        for _ in range(limit):
            payload = self.receive()
            if accept(payload):
                return PacketResult(payload=payload, skipped=skipped)
            skipped.append(payload)
        raise RuntimeError(f"expected RSP response not found; skipped={skipped!r}")

    def request(
        self, payload: str, accept: Callable[[str], bool] | None = None
    ) -> PacketResult:
        self.send(payload)
        return self.receive_until(accept or (lambda _payload: True))

    def handshake(self) -> tuple[PacketResult, PacketResult]:
        supported = self.request(
            "qSupported:multiprocess+",
            lambda payload: payload.startswith("PacketSize="),
        )
        status = self.request("?", lambda payload: payload.startswith(("S", "T")))
        return supported, status

    def halt(self) -> PacketResult:
        self._require_socket().sendall(b"\x03")
        return self.receive_until(lambda payload: payload.startswith(("S", "T")))

    def resume(self) -> None:
        self.send("c")

    def continue_until_stop(self) -> PacketResult:
        self.send("c")
        return self.receive_until(lambda payload: payload.startswith(("S", "T")))

    def step(self) -> PacketResult:
        self.send("s")
        return self.receive_until(lambda payload: payload.startswith(("S", "T")))

    def registers(self) -> tuple[list[int], PacketResult]:
        result = self.request(
            "g",
            lambda payload: len(payload) >= 38 * 16
            and len(payload) % 16 == 0
            and is_hex_payload(payload),
        )
        registers = [
            int(result.payload[offset : offset + 16], 16)
            for offset in range(0, len(result.payload), 16)
        ]
        return registers, result

    def memory(self, address: int, size: int) -> tuple[str, PacketResult]:
        result = self.request(
            f"m{address:08x},{size:x}",
            lambda payload: is_hex_payload(payload, size * 2),
        )
        return result.payload, result

    def expect_ok(self, command: str) -> PacketResult:
        return self.request(command, lambda payload: payload == "OK")


def low32(value: int) -> int:
    return value & 0xFFFFFFFF


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="::1")
    parser.add_argument("--port", type=int, default=9123)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--step-attempts", type=int, default=8)
    parser.add_argument("--skip-watchpoint", action="store_true")
    parser.add_argument(
        "--already-halted",
        action="store_true",
        help="use when ares was launched with Boot/AwaitGDBClient=true",
    )
    args = parser.parse_args()

    client = AresRSPClient(args.host, args.port, args.timeout)
    breakpoint_address: int | None = None
    watchpoint_active = False
    halted = False
    phase = "connect"

    try:
        client.connect()
        phase = "handshake"
        supported, initial_status = client.handshake()

        phase = "halt"
        if args.already_halted:
            ctrl_c = PacketResult(
                payload=initial_status.payload,
                skipped=["ares launched with Boot/AwaitGDBClient=true"],
            )
        else:
            try:
                ctrl_c = client.halt()
            except socket.timeout:
                # ares may already be halted when a new client attaches (for
                # example after a previous probe disconnected during Ctrl-C).
                # In that state it accepts the interrupt but emits no second
                # stop packet. Reuse the handshake stop reason and continue so
                # the finally block can always resume the emulator.
                ctrl_c = PacketResult(
                    payload=initial_status.payload,
                    skipped=[
                        "no additional stop packet after Ctrl-C; reused initial status"
                    ],
                )
        halted = True
        phase = "initial register read"
        initial_registers, initial_register_packet = client.registers()
        initial_pc = initial_registers[37]

        phase = "cached memory read"
        cached_memory, cached_packet = client.memory(0x80000000, 64)
        phase = "uncached memory read"
        uncached_memory, uncached_packet = client.memory(0xA0000000, 64)

        step_stops: list[str] = []
        step_pcs = [initial_pc]
        step_changed_pc = False
        for _ in range(args.step_attempts):
            phase = "single step"
            step_stops.append(client.step().payload)
            phase = "post-step register read"
            registers_after_step, _ = client.registers()
            step_pcs.append(registers_after_step[37])
            if registers_after_step[37] != initial_pc:
                step_changed_pc = True
                break

        breakpoint_address = low32(step_pcs[-1])
        phase = "breakpoint add"
        breakpoint_add = client.expect_ok(f"Z0,{breakpoint_address:08x},4")
        phase = "breakpoint continue"
        breakpoint_stop = client.continue_until_stop()
        phase = "breakpoint register read"
        breakpoint_registers, breakpoint_register_packet = client.registers()
        breakpoint_pc = breakpoint_registers[37]
        phase = "breakpoint remove"
        breakpoint_remove = client.expect_ok(f"z0,{breakpoint_address:08x},4")
        breakpoint_address = None

        watchpoint_result: dict[str, object] | None = None
        if not args.skip_watchpoint:
            phase = "watchpoint add"
            watchpoint_add = client.expect_ok("Z3,80000000,400000")
            watchpoint_active = True
            client.send("c")
            halted = False
            phase = "watchpoint wait"
            watchpoint_stop = client.receive_until(lambda payload: "watch:" in payload)
            halted = True
            phase = "watchpoint remove"
            watchpoint_remove = client.expect_ok("z3,80000000,400000")
            watchpoint_active = False
            watchpoint_result = {
                "range": "0x80000000-0x803fffff",
                "add": watchpoint_add.payload,
                "trigger": watchpoint_stop.payload,
                "packets_before_trigger": watchpoint_stop.skipped,
                "remove": watchpoint_remove.payload,
                "queued_stop_packets_before_remove_ack": watchpoint_remove.skipped,
            }

        phase = "resume"
        client.resume()
        halted = False

        output = {
            "endpoint": f"[{args.host}]:{args.port}" if ":" in args.host else f"{args.host}:{args.port}",
            "qSupported": supported.payload,
            "initial_status": initial_status.payload,
            "ctrl_c_stop": ctrl_c.payload,
            "register_count": len(initial_registers),
            "initial_pc": f"0x{initial_pc:016x}",
            "initial_sp": f"0x{initial_registers[29]:016x}",
            "rdram_80000000_64": cached_memory,
            "rdram_a0000000_64": uncached_memory,
            "cached_uncached_equal": cached_memory == uncached_memory,
            "step": {
                "stop_packets": step_stops,
                "pcs": [f"0x{pc:016x}" for pc in step_pcs],
                "changed_pc": step_changed_pc,
            },
            "breakpoint": {
                "address": f"0x{low32(breakpoint_pc):08x}",
                "add": breakpoint_add.payload,
                "trigger": breakpoint_stop.payload,
                "pc_at_stop": f"0x{breakpoint_pc:016x}",
                "hit_exact": low32(breakpoint_pc) == low32(step_pcs[-1]),
                "remove": breakpoint_remove.payload,
                "queued_packets_before_register_reply": breakpoint_register_packet.skipped,
            },
            "read_watchpoint": watchpoint_result,
            "queued_packets": {
                "initial_register_read": initial_register_packet.skipped,
                "cached_memory_read": cached_packet.skipped,
                "uncached_memory_read": uncached_packet.skipped,
            },
            "resumed_before_disconnect": True,
        }
        print(json.dumps(output, indent=2))
        return 0
    except TimeoutError as error:
        raise TimeoutError(f"{phase}: {error}") from error
    finally:
        if client.sock is not None:
            try:
                if breakpoint_address is not None:
                    client.expect_ok(f"z0,{breakpoint_address:08x},4")
                if watchpoint_active:
                    client.expect_ok("z3,80000000,400000")
                if halted:
                    client.resume()
            except (EOFError, OSError, RuntimeError, socket.timeout):
                pass
            client.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TimeoutError as error:
        print(
            json.dumps(
                {
                    "error": "timed out waiting for ares RSP",
                    "detail": str(error),
                    "recovery": (
                        "Quit and reopen ares, load the N64 ROM once, then rerun "
                        "the probe. This clears stale duplicate v148 listeners."
                    ),
                },
                indent=2,
            )
        )
        raise SystemExit(2) from None
