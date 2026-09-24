from __future__ import annotations

import random
import unittest

from PIL import Image

import struct

from srw64_rom.resources import FORMAT_I4, decode_i4_texture, lz_decode, lz_encode


class ResourceCodecTests(unittest.TestCase):
    def test_lz_round_trip_for_representative_patterns(self) -> None:
        random_bytes = random.Random(64).randbytes(4096)
        samples = [
            b"",
            bytes(4096),
            bytes(range(256)) * 8,
            b"SRW64" * 1200,
            random_bytes,
        ]
        for sample in samples:
            with self.subTest(size=len(sample), prefix=sample[:8]):
                encoded = lz_encode(sample)
                decoded, consumed = lz_decode(encoded, len(sample))
                self.assertEqual(decoded, sample)
                self.assertEqual(consumed, len(encoded))

    def test_i4_texture_decodes(self) -> None:
        image = Image.new("P", (7, 3))
        image.putdata([index % 16 for index in range(21)])
        indices = image.tobytes() + b"\0"
        packed = bytes((indices[i] << 4) | indices[i + 1] for i in range(0, 21, 2))
        encoded = struct.pack(">HHHH", FORMAT_I4, 7, 3, 0x1234) + packed
        decoded, flags = decode_i4_texture(encoded)
        self.assertEqual(flags, 0x1234)
        self.assertEqual(decoded.size, image.size)
        self.assertEqual(decoded.tobytes(), image.tobytes())


if __name__ == "__main__":
    unittest.main()
