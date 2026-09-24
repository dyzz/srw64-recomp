from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
import struct

from PIL import Image


RESOURCE_BASE = 0x00A20BD0
RING_SIZE = 0x400
RING_START = 0x3BE
MIN_MATCH = 3
MAX_MATCH = 0x42
FORMAT_I4 = 0x0005


class ResourceError(ValueError):
    """Raised when an SRW64 resource is malformed or cannot be patched safely."""


@dataclass(frozen=True)
class ResourceEntry:
    resource_id: int
    descriptor_offset: int
    relative_offset: int
    span_size: int
    data_offset: int
    decoded_size: int

    @property
    def compressed_offset(self) -> int:
        return self.data_offset + 4

    @property
    def compressed_capacity(self) -> int:
        return self.span_size - 4


class ResourceTable:
    def __init__(self, rom: bytes, base: int = RESOURCE_BASE):
        self.rom = rom
        self.base = base
        if base < 0 or base + 4 > len(rom):
            raise ResourceError(f"resource table base 0x{base:x} is outside ROM")
        self.count = struct.unpack_from(">I", rom, base)[0]
        if base + 4 + self.count * 8 > len(rom):
            raise ResourceError("resource descriptor table exceeds ROM")

    def entry(self, resource_id: int) -> ResourceEntry:
        if resource_id < 0 or resource_id >= self.count:
            raise ResourceError(f"resource id {resource_id} is outside 0..{self.count - 1}")
        descriptor_offset = self.base + 4 + resource_id * 8
        relative_offset, span_size = struct.unpack_from(">II", self.rom, descriptor_offset)
        data_offset = self.base + relative_offset
        if span_size < 4 or data_offset < 0 or data_offset + span_size > len(self.rom):
            raise ResourceError(
                f"resource {resource_id}: span 0x{data_offset:x}-0x{data_offset + span_size:x} is invalid"
            )
        decoded_size = struct.unpack_from(">I", self.rom, data_offset)[0]
        return ResourceEntry(
            resource_id=resource_id,
            descriptor_offset=descriptor_offset,
            relative_offset=relative_offset,
            span_size=span_size,
            data_offset=data_offset,
            decoded_size=decoded_size,
        )

    def extract(self, resource_id: int) -> tuple[bytes, int]:
        entry = self.entry(resource_id)
        source = self.rom[
            entry.compressed_offset : entry.compressed_offset + entry.compressed_capacity
        ]
        return lz_decode(source, entry.decoded_size)


def lz_decode(source: bytes, decoded_size: int) -> tuple[bytes, int]:
    if decoded_size < 0:
        raise ResourceError("decoded size must be nonnegative")
    ring = bytearray(RING_SIZE)
    ring_position = RING_START
    source_position = 0
    flags = 0
    output = bytearray()
    while len(output) < decoded_size:
        flags >>= 1
        current_flags = flags
        if flags & 0x100 == 0:
            if source_position >= len(source):
                raise ResourceError("compressed stream ends before a flag byte")
            current_flags = source[source_position]
            source_position += 1
            flags = current_flags | 0xFF00
        if current_flags & 1:
            if source_position >= len(source):
                raise ResourceError("compressed stream ends before a literal")
            value = source[source_position]
            source_position += 1
            output.append(value)
            ring[ring_position] = value
            ring_position = (ring_position + 1) & (RING_SIZE - 1)
        else:
            if source_position + 2 > len(source):
                raise ResourceError("compressed stream ends inside a match token")
            low = source[source_position]
            high_length = source[source_position + 1]
            source_position += 2
            match_position = low | ((high_length & 0xC0) << 2)
            match_length = (high_length & 0x3F) + MIN_MATCH
            for delta in range(match_length):
                value = ring[(match_position + delta) & (RING_SIZE - 1)]
                output.append(value)
                ring[ring_position] = value
                ring_position = (ring_position + 1) & (RING_SIZE - 1)
                if len(output) == decoded_size:
                    break
        flags &= 0xFFFF
    return bytes(output), source_position


def _best_match(data: bytes, position: int, positions: dict[bytes, deque[int]]) -> tuple[int, int]:
    remaining = len(data) - position
    if remaining < MIN_MATCH:
        return 0, 0
    maximum = min(MAX_MATCH, remaining)
    best_source = 0
    best_length = 0

    if position < MAX_MATCH and data[position : position + MIN_MATCH] == b"\0\0\0":
        length = 0
        while length < maximum and data[position + length] == 0:
            length += 1
        best_source = 0
        best_length = length

    key = data[position : position + MIN_MATCH]
    candidates = positions.get(key)
    if candidates is None:
        return best_source, best_length
    while candidates and position - candidates[0] > RING_SIZE:
        candidates.popleft()
    for candidate in reversed(candidates):
        distance = position - candidate
        if distance <= 0 or distance > RING_SIZE:
            continue
        length = 0
        while length < maximum and data[candidate + length] == data[position + length]:
            length += 1
        if length > best_length:
            best_source = (RING_START + candidate) & (RING_SIZE - 1)
            best_length = length
            if length == maximum:
                break
    return best_source, best_length


def _index_position(data: bytes, position: int, positions: dict[bytes, deque[int]]) -> None:
    if position + MIN_MATCH > len(data):
        return
    bucket = positions[data[position : position + MIN_MATCH]]
    bucket.append(position)
    while bucket and position - bucket[0] > RING_SIZE:
        bucket.popleft()


def lz_encode(data: bytes) -> bytes:
    positions: dict[bytes, deque[int]] = defaultdict(deque)
    output = bytearray()
    position = 0
    while position < len(data):
        flag_offset = len(output)
        output.append(0)
        for bit in range(8):
            if position >= len(data):
                break
            match_source, match_length = _best_match(data, position, positions)
            if match_length >= MIN_MATCH:
                output.append(match_source & 0xFF)
                output.append(((match_source >> 2) & 0xC0) | (match_length - MIN_MATCH))
                for indexed in range(position, position + match_length):
                    _index_position(data, indexed, positions)
                position += match_length
            else:
                output[flag_offset] |= 1 << bit
                output.append(data[position])
                _index_position(data, position, positions)
                position += 1
    return bytes(output)


def decode_i4_texture(decoded: bytes) -> tuple[Image.Image, int]:
    if len(decoded) < 8:
        raise ResourceError("texture resource is shorter than its header")
    texture_format, width, height, flags = struct.unpack_from(">HHHH", decoded, 0)
    if texture_format != FORMAT_I4:
        raise ResourceError(f"expected I4 texture format 0005, got {texture_format:04X}")
    pixel_count = width * height
    expected_size = 8 + (pixel_count + 1) // 2
    if len(decoded) != expected_size:
        raise ResourceError(
            f"I4 texture is {len(decoded)} bytes; expected {expected_size} for {width}x{height}"
        )
    indices = bytearray()
    for packed in decoded[8:]:
        indices.append(packed >> 4)
        if len(indices) < pixel_count:
            indices.append(packed & 0x0F)
    image = Image.frombytes("P", (width, height), bytes(indices))
    palette: list[int] = []
    for index in range(256):
        value = index * 17 if index < 16 else 0
        palette.extend((value, value, value))
    image.putpalette(palette)
    return image, flags


def patch_resource_to_pool(
    rom: bytes,
    resource_id: int,
    decoded: bytes,
    pool_offset: int,
    pool_span: int,
) -> tuple[bytes, dict[str, int]]:
    if pool_offset % 4:
        raise ResourceError("resource pool offset must be four-byte aligned")
    table = ResourceTable(rom)
    entry = table.entry(resource_id)
    encoded = lz_encode(decoded)
    payload = struct.pack(">I", len(decoded)) + encoded
    pool_end = pool_offset + pool_span
    if len(payload) > pool_span:
        raise ResourceError(
            f"resource {resource_id} payload {len(payload)} exceeds pool span {pool_span}"
        )
    if pool_offset < 0 or pool_end > len(rom):
        raise ResourceError("resource pool exceeds ROM")
    if any(byte not in (0x00, 0xFF) for byte in rom[pool_offset:pool_end]):
        raise ResourceError("resource pool overlaps non-padding data")
    relative_offset = pool_offset - RESOURCE_BASE
    if relative_offset < 0:
        raise ResourceError("resource pool precedes resource table")
    patched = bytearray(rom)
    struct.pack_into(">II", patched, entry.descriptor_offset, relative_offset, pool_span)
    patched[pool_offset:pool_end] = payload + bytes(pool_span - len(payload))
    decoded_check, consumed = ResourceTable(bytes(patched)).extract(resource_id)
    if decoded_check != decoded:
        raise ResourceError("patched resource failed decode round-trip")
    return bytes(patched), {
        "resource_id": resource_id,
        "descriptor_offset": entry.descriptor_offset,
        "pool_offset": pool_offset,
        "pool_span": pool_span,
        "decoded_size": len(decoded),
        "encoded_size": len(encoded),
        "encoded_consumed": consumed,
    }
