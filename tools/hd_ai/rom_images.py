"""Decode ROM palette and indexed-texture bytes for the HD generators."""
from __future__ import annotations

import struct

from PIL import Image


def rgba16(raw: bytes) -> list[tuple[int, int, int, int]]:
    """RGBA5551 palette entries as 8-bit RGBA tuples."""
    return [tuple(round(((v >> shift) & 31) * 255 / 31) for shift in (11, 6, 1))
            + (255 * (v & 1),) for (v,) in struct.iter_unpack(">H", raw)]


def indexed(raw: bytes, size: tuple[int, int], palette: list[tuple[int, int, int, int]], bits: int) -> Image.Image:
    """A 4- or 8-bit colour-indexed texture (high nibble first) as an RGBA image."""
    indices = raw if bits == 8 else [v for b in raw for v in (b >> 4, b & 15)]
    assert len(indices) == size[0] * size[1]
    return Image.frombytes("RGBA", size, bytes(c for v in indices for c in palette[v]))
