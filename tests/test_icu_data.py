import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/release"))
import icu_data  # noqa: E402


def header() -> bytes:
    info = struct.pack("<HHBBBB", 20, 0, 0, 0, 2, 0) + b"CmnD" + bytes([1, 0, 0, 0]) + bytes(4)
    return struct.pack("<HBB", 32, 0xDA, 0x27) + info + bytes(32 - 4 - len(info))


ITEMS = [("icudt78l/af.res", b"A" * 20), ("icudt78l/brkitr/char.brk", b"C" * 33),
         ("icudt78l/brkitr/cjdict.dict", b"D" * 50), ("icudt78l/brkitr/line.brk", b"L" * 7),
         ("icudt78l/brkitr/root.res", b"R" * 16), ("icudt78l/res_index.res", b"I" * 3),
         ("icudt78l/root.res", b"O" * 5), ("icudt78l/zone/Asia.res", b"Z" * 9)]


def padded(items):
    # As read back: each item runs to the next one, its 16-byte padding included.
    data = icu_data.pack(header(), items)
    return icu_data.items(data)[1]


class IcuDataTests(unittest.TestCase):
    def test_pack_reads_back_and_aligns_items(self):
        data = icu_data.pack(header(), ITEMS)
        _, found = icu_data.items(data)
        self.assertEqual([name for name, _ in found], [name for name, _ in ITEMS])
        for (_, blob), (_, original) in zip(found, ITEMS):
            self.assertTrue(blob.startswith(original))
        count, = struct.unpack_from("<I", data, 32)
        for i in range(count):
            _, offset = struct.unpack_from("<II", data, 32 + 4 + 8 * i)
            self.assertEqual((32 + offset) % 16, 0)

    def test_subset_keeps_break_rules_and_their_bundles(self):
        small = icu_data.subset(icu_data.pack(header(), padded(ITEMS)))
        names = [name for name, _ in icu_data.items(small)[1]]
        self.assertEqual(names, ["icudt78l/brkitr/char.brk", "icudt78l/brkitr/line.brk", "icudt78l/brkitr/root.res",
                                 "icudt78l/res_index.res", "icudt78l/root.res"])

    def test_subset_refuses_data_without_line_rules(self):
        items = [item for item in padded(ITEMS) if not item[0].endswith("line.brk")]
        with self.assertRaises(ValueError):
            icu_data.subset(icu_data.pack(header(), items))

    def test_not_common_data_is_refused(self):
        data = bytearray(icu_data.pack(header(), ITEMS))
        data[12:16] = b"ResB"
        with self.assertRaises(ValueError):
            icu_data.items(bytes(data))

    def test_c_source_names_the_symbol_and_holds_every_byte(self):
        source = icu_data.c_source(bytes([0, 1, 255]), "icudt78_dat")
        self.assertIn("icudt78_dat = { 0.0, {", source)
        self.assertIn("uint8_t bytes[3]", source)
        self.assertIn("0,1,255,", source)


if __name__ == "__main__":
    unittest.main()
