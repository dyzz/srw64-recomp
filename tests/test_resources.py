from __future__ import annotations

import random
import unittest

from PIL import Image

from srw64_w0.resources import decode_i4_texture, encode_i4_texture, lz_decode, lz_encode


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

    def test_i4_texture_round_trip(self) -> None:
        image = Image.new("P", (7, 3))
        image.putdata([index % 16 for index in range(21)])
        encoded = encode_i4_texture(image, flags=0x1234)
        decoded, flags = decode_i4_texture(encoded)
        self.assertEqual(flags, 0x1234)
        self.assertEqual(decoded.size, image.size)
        self.assertEqual(decoded.tobytes(), image.tobytes())


if __name__ == "__main__":
    unittest.main()
