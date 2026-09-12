"""Pinned JP Rev 0 save layout, derived from 800924D8 / 80093278.

The original checksum is weak and does not cover all tactical bytes. This
module never substitutes it for the complete collection SHA-256 checks.
"""
from __future__ import annotations

import json

SLOTS = {'intermission-1': (0x10, 0x1F00), 'intermission-2': (0x1F10, 0x1F00), 'tactical': (0x3E10, 0x3AE0)}


def checksum(payload: bytes, *, tactical: bool) -> int:
    size = 0x3AE0 if tactical else 0x1F00
    if len(payload) != size:
        raise ValueError('Unexpected original save payload size')
    # Intermission sums 0x1F00 bytes starting at buffer+2. The final two
    # bytes are 0000 at the start of pinned overlay 0008F4B0 (801C4500).
    # Tactical deliberately uses the same 0x1F00-byte sum over a larger save.
    return 0x8000 | (sum((payload + b'\0\0')[2:2 + 0x1F00]) & 255)


def inspect_sram(data: bytes) -> dict:
    if len(data) != 0x8000 or data[:7] != b'SRW64V3':
        raise ValueError('Not a pinned SRW64 SRAM image')
    slots = {}
    for name, (offset, size) in SLOTS.items():
        payload = data[offset:offset + size]
        stored = int.from_bytes(payload[:2], 'big')
        slots[name] = {'present': bool(stored & 0x8000), 'checksum_matches': stored == checksum(payload, tactical=name == 'tactical'),
                       'payload_size': size, 'checksum_coverage_bytes': 0x1F00 if name == 'tactical' else size - 2}
    return {'schema': 'srw64.original-sram-inspection.v1', 'slots': slots,
            'scope': 'Original format/checksum only; no full restore or object-reference validation'}


def candidate_sram(payload: bytes, *, tactical: bool) -> bytes:
    """Build an isolated experiment, not a user checkpoint (records not copied)."""
    checksum_value = checksum(payload, tactical=tactical)
    data = bytearray(0x8000)
    data[:7] = b'SRW64V3'
    offset = 0x3E10 if tactical else 0x10
    data[offset:offset + len(payload)] = payload
    data[offset:offset + 2] = checksum_value.to_bytes(2, 'big')
    return bytes(data)


def rng_extensions(observation: dict) -> bytes:
    region = observation['regions']['game_rng']
    if region['address'] != 0x800D49D0 or region['size'] != 0x834:
        raise ValueError('Unsupported RNG state observation')
    data = bytes.fromhex(region['bytes'])
    if len(data) != 0x834:
        raise ValueError('Truncated RNG state')
    return (json.dumps({'schema': 'srw64.checkpoint-extensions.v1', 'rng_index': int.from_bytes(data[:4], 'big'),
                       'rng_table': data[16:].hex(), 'read_state': {}}, sort_keys=True) + '\n').encode()
