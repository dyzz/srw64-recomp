from __future__ import annotations

from collections import deque
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

path = Path(__file__).resolve().parents[1] / "tools/recomp/probes/rsp_capture.py"
spec = importlib.util.spec_from_file_location("recomp_capture", path)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeSocket:
    def __init__(self, chunks: list[bytes]) -> None:
        self.chunks = deque(chunks)
        self.sent: list[bytes] = []

    def recv(self, size: int) -> bytes:
        return self.chunks.popleft() if self.chunks else b""

    def sendall(self, value: bytes) -> None:
        self.sent.append(value)


class CaptureTransportTests(unittest.TestCase):
    def test_fragmented_checksum_and_coalesced_packets(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        sock = FakeSocket([b"+$T", b"05#b", b"9$0102#c3"])
        client.sock = sock
        self.assertEqual(client.receive(), "T05")
        self.assertEqual(client.receive(), "0102")
        self.assertEqual(sock.sent, [b"+", b"+"])

    def test_corrupt_memory_reply_is_rejected(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        client.sock = FakeSocket([b"$0102#00"])
        with self.assertRaises(module.CaptureError):
            client.receive()

    def test_incomplete_packet_is_rejected(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        client.sock = FakeSocket([b"$0102#"])
        with self.assertRaises(EOFError):
            client.receive()

    def test_memory_detection_refuses_uninitialized_size(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        client.read_memory = Mock(return_value=bytes.fromhex("00800000"))
        self.assertEqual(client.rdram_size(), 0x800000)
        client.read_memory.assert_called_once_with(0x80000318, 4)
        client.read_memory.return_value = bytes(4)
        with self.assertRaises(module.CaptureError):
            client.rdram_size()

    def test_lazy_fpu_trap_resumes_original_handler(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        first, final = [0] * 38, [0] * 38
        first[37], final[37] = 0x80001000, 0x80002000
        client.continue_until_stop = Mock(side_effect=[SimpleNamespace(payload="S10"), SimpleNamespace(payload="T05")])
        client.registers = Mock(side_effect=[(first, None), (final, None)])
        client.read_memory = Mock(return_value=bytes.fromhex("44810000"))
        registers, events = client.continue_to(0x80002000)
        self.assertEqual(registers, final)
        self.assertEqual(events, [{"stop": "S10", "pc": 0x80001000, "instruction": "44810000"}])

    def test_non_fpu_exception_is_not_silently_resumed(self) -> None:
        client = module.BufferedRSPClient("::1", 1, 1)
        registers = [0] * 38
        registers[37] = 0x80001000
        client.continue_until_stop = Mock(return_value=SimpleNamespace(payload="S10"))
        client.registers = Mock(return_value=(registers, None))
        client.read_memory = Mock(return_value=bytes.fromhex("8c820000"))
        with self.assertRaises(module.CaptureError):
            client.continue_to(0x80002000)
        self.assertEqual(client.continue_until_stop.call_count, 1)
