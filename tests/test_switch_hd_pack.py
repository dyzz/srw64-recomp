"""tools/release/switch/pack_hd.py: every file of an HD folder in one file, as
src/host/hd_pack.cpp reads it."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack_hd", ROOT / "tools/release/switch/pack_hd.py")
pack_hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack_hd)


class PackTest(unittest.TestCase):
    FILES = {"art/battle-sprites/a.mwtex": b"first", "art/portraits/portrait-1.mwtex": b"x" * 1000,
             "native-models/m.mwtex": b"", "art/srw64-units-hd.json": b"{}", "art/battle-sprites/a.png": b"png" * 400,
             "hd.json": b'{"version":"1.3"}', "art/units/deep/er/pose.jpg": bytes(range(256)) * 9}

    def write(self, folder: Path, files: dict[str, bytes]) -> None:
        for name, data in files.items():
            (folder / name).parent.mkdir(parents=True, exist_ok=True)
            (folder / name).write_bytes(data)

    def table(self, blob: bytes):
        magic, version, entries, names_size = pack_hd.HEADER.unpack_from(blob)
        rows = [pack_hd.ENTRY.unpack_from(blob, pack_hd.HEADER.size + i * pack_hd.ENTRY.size) for i in range(entries)]
        names = blob[pack_hd.HEADER.size + entries * pack_hd.ENTRY.size:][:names_size]
        return magic, version, rows, names

    def test_every_file_moves_into_the_pack(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.write(folder, self.FILES)
            count, _ = pack_hd.pack(folder)
            self.assertEqual(count, len(self.FILES))
            self.assertEqual(sorted(p.name for p in folder.rglob("*")), ["art.pack", "hd.json"],
                             "nothing else is left but the pack's own hd.json, no empty folder either")
            self.assertEqual((folder / "hd.json").read_bytes(), self.FILES["hd.json"])
            (folder / "hd.json").unlink()
            pack_hd.pack(folder)
            self.assertEqual((folder / "hd.json").read_bytes(), self.FILES["hd.json"], "a folder packed before gets it back")
            blob = (folder / "art.pack").read_bytes()
            magic, version, rows, names = self.table(blob)
            self.assertEqual((magic, version), (b"MWPK", 2))
            self.assertEqual(struct.unpack_from("<I", blob)[0], 0x4B50574D, "the magic hd_pack.cpp compares")
            self.assertEqual([e[0] for e in rows], sorted(e[0] for e in rows), "binary search needs them sorted")
            by_hash = {e[0]: e for e in rows}
            for name, data in self.FILES.items():
                _, offset, size, name_offset = by_hash[pack_hd.fnv1a(name)]
                self.assertEqual(blob[offset:offset + size], data)
                self.assertEqual(offset % pack_hd.ALIGN, 0)
                self.assertEqual(names[name_offset:names.index(b"\0", name_offset)].decode(), name)

    def test_a_version_1_pack_is_carried_over(self):
        """A folder packed before (pictures only, no names) packs again without preparing them again."""
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            pictures = {name: data for name, data in self.FILES.items() if name.endswith(".mwtex")}
            rest = {name: data for name, data in self.FILES.items() if not name.endswith(".mwtex")}
            # Version 1 as the script before wrote it.
            ordered = sorted((pack_hd.fnv1a(name), data) for name, data in pictures.items())
            offset = (pack_hd.HEADER_V1.size + pack_hd.ENTRY.size * len(ordered) + 511) // 512 * 512
            table, body = bytearray(pack_hd.HEADER_V1.pack(b"MWPK", 1, len(ordered))), bytearray()
            for value, data in ordered:
                table += pack_hd.ENTRY.pack(value, offset + len(body), len(data), 0)
                body += data + b"\0" * (-len(data) % 512)
            folder.mkdir(exist_ok=True)
            (folder / "art.pack").write_bytes(bytes(table).ljust(offset, b"\0") + bytes(body))
            self.write(folder, rest)
            count, _ = pack_hd.pack(folder)
            self.assertEqual(count, len(self.FILES))
            blob = (folder / "art.pack").read_bytes()
            _, version, rows, names = self.table(blob)
            self.assertEqual(version, 2)
            by_hash = {e[0]: e for e in rows}
            for name, data in self.FILES.items():
                _, offset, size, name_offset = by_hash[pack_hd.fnv1a(name)]
                self.assertEqual(blob[offset:offset + size], data)
                self.assertEqual(name_offset == pack_hd.NO_NAME, name in pictures, "a version 1 entry has no name to give")
            # And once more, now from version 2: the same pack.
            pack_hd.pack(folder)
            self.assertEqual((folder / "art.pack").read_bytes(), blob)

    def test_the_reader_reads_what_the_script_wrote(self):
        """src/host/hd_pack.cpp on a pack from here (tests/native_hd_pack.cpp), where a C++ compiler is at hand."""
        compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "hd"
            self.write(folder, self.FILES)
            listing = Path(temporary) / "list.txt"
            listing.write_text("".join(f"{name}\t{len(data)}\t{pack_hd.fnv1a_bytes(data):x}\n" for name, data in self.FILES.items()))
            pack_hd.pack(folder)
            program = Path(temporary) / "native_hd_pack"
            subprocess.run([compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", str(ROOT / "tests/native_hd_pack.cpp"),
                            str(ROOT / "src/host/hd_pack.cpp"), "-o", str(program)], check=True)
            result = subprocess.run([str(program), str(folder / "art.pack"), str(listing)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_hash_matches_the_reader(self):
        self.assertEqual(pack_hd.fnv1a(""), 0xCBF29CE484222325)
        self.assertEqual(pack_hd.fnv1a("a"), 0xAF63DC4C8601EC8C)
        reader = (ROOT / "src/host/hd_pack.cpp").read_text(encoding="utf-8")
        self.assertIn("0x100000001B3ull", reader)
        self.assertIn('"art.pack"', reader)


if __name__ == "__main__":
    unittest.main()
