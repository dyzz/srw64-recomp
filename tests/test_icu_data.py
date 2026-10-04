import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FULL = ROOT / "build/macos-deps/sources/icu/icu/source/data/in/icudt78l.dat.full"


def load():
    spec = importlib.util.spec_from_file_location("icu_data", ROOT / "tools/release/icu_data.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def common(items: dict[str, bytes], package: str = "icudt78l") -> bytes:
    """A small ICU common data package in icupkg's layout."""
    header = bytearray(32)
    struct.pack_into("<HBBHHBBBB4s4s4s", header, 0, 32, 0xDA, 0x27, 20, 0, 0, 0, 2, 0, b"CmnD", bytes([1, 0, 0, 0]), bytes(4))
    return load().write_common(bytes(header), package, items)


class IcuDataTests(unittest.TestCase):
    """tools/release/icu_data.py keeps the break rules the game reads (config/recomp/icu-data.txt)."""

    def test_trim_keeps_listed_items_byte_for_byte(self):
        icu = load()
        items = {"brkitr/char.brk": b"c" * 37, "brkitr/line.brk": b"l" * 16, "zone/x.res": b"z" * 5, "curr/y.res": b"y" * 300}
        blob = common(items)
        trimmed, package = icu.trim(blob, ["brkitr/line.brk", "brkitr/char.brk"])
        self.assertEqual(package, "icudt78l")
        header, again, kept = icu.read_common(trimmed)
        self.assertEqual(header, blob[:32])
        self.assertEqual(kept, {"brkitr/char.brk": b"c" * 37 + b"\0" * 11, "brkitr/line.brk": b"l" * 16})
        count = struct.unpack_from("<I", trimmed, 32)[0]
        starts = [struct.unpack_from("<II", trimmed, 36 + 8 * i)[1] for i in range(count)]
        self.assertTrue(all(start % 16 == 0 for start in starts))
        with self.assertRaises(ValueError):
            icu.trim(blob, ["brkitr/word.brk"])

    def test_common_data_is_found_inside_a_library(self):
        icu = load()
        blob = common({"brkitr/char.brk": b"abc", "brkitr/root.res": b"r" * 20})
        library = b"MZ" + b"\x90" * 1000 + blob + b"\0" * 64
        self.assertEqual(icu.trim(library, ["brkitr/char.brk"])[0], icu.trim(blob, ["brkitr/char.brk"])[0])

    def test_c_source_defines_the_data_symbol(self):
        icu = load()
        dat = icu.trim(common({"brkitr/char.brk": b"abc"}), ["brkitr/char.brk"])[0]
        source = icu.c_source(dat, "icudt78_dat")
        self.assertIn(f"const unsigned char icudt78_dat[{len(dat)}]", source)
        self.assertIn("__declspec(dllexport)", source)
        self.assertEqual(len(re.findall(r"\d+", source.split("= {", 1)[1])), len(dat))

    def test_keep_list_has_the_rules_portable_text_opens(self):
        keep = load().keep_list()
        self.assertEqual(len(keep), len(set(keep)))
        for name in ("brkitr/res_index.res", "brkitr/root.res", "brkitr/char.brk", "brkitr/line.brk",
                     "brkitr/line_cj.brk", "brkitr/ja.res", "brkitr/zh.res", "brkitr/en.res"):
            self.assertIn(name, keep)
        self.assertFalse([name for name in keep if name.endswith(".dict")])

    def test_prepare_source_is_repeatable(self):
        if not FULL.is_file():
            self.skipTest("no local ICU source data")
        icu = load()
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "icu/source/data/in"
            data.mkdir(parents=True)
            (data / "icudt78l.dat").write_bytes(FULL.read_bytes())
            makefile = data.parent / "Makefile.in"
            makefile.write_text("\t$(INVOKE) $(TOOLBINDIR)/icupkg -t$(ICUDATA_CHAR) a b\n"
                                "\t$(INVOKE) $(TOOLBINDIR)/icupkg -d $(BUILDDIR) --list -x \\* $(ICUDATA_SOURCE_ARCHIVE) -o $@\n")
            first = icu.prepare_source(Path(tmp) / "icu").read_bytes()
            second = icu.prepare_source(Path(tmp) / "icu").read_bytes()
            self.assertEqual(first, second)
            self.assertEqual(makefile.read_text().count("icupkg --ignore-deps "), 2)
            self.assertLess(len(first), 1 << 20)
            self.assertEqual((data / "icudt78l.dat.full").read_bytes(), FULL.read_bytes())
            self.assertEqual(sorted(icu.read_common(first)[2]), sorted(icu.keep_list()))

    def test_every_build_cuts_and_checks_the_data(self):
        for path in ("tools/release/build_macos_dependencies.py", "tools/release/build_linux.py",
                     "tools/release/android/build_dependencies.py"):
            self.assertIn("icu_data.prepare_source(", (ROOT / path).read_text(), path)
        for path in ("tools/release/build_macos_dependencies.py", "tools/release/build_linux.py"):
            self.assertIn("icu_check.cpp", (ROOT / path).read_text(), path)
        workflow = (ROOT / ".github/workflows/build.yml").read_text()
        self.assertIn("tools/release/icu_data.py trim", workflow)
        self.assertIn("icu_check.exe", workflow)
        cases = (ROOT / "tools/release/icu_check_cases.inc").read_text()
        self.assertGreaterEqual(cases.count('{"'), 12)
        for tag in ("ja", "zh-Hans", "en"):
            self.assertIn(f'{{"{tag}", ', cases)


if __name__ == "__main__":
    unittest.main()
