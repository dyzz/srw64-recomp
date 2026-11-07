"""tools/release/switch/pack_hd.py: the .mwtex files of an HD folder in one file, as
src/host/hd_texture.cpp reads it."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack_hd", ROOT / "tools/release/switch/pack_hd.py")
pack_hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack_hd)


class PackTest(unittest.TestCase):
    def test_pictures_move_into_the_pack(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            files = {"art/battle-sprites/a.mwtex": b"first", "art/portraits/portrait-1.mwtex": b"x" * 1000,
                     "native-models/m.mwtex": b""}
            for name, data in files.items():
                (folder / name).parent.mkdir(parents=True, exist_ok=True)
                (folder / name).write_bytes(data)
            (folder / "art/srw64-units-hd.json").write_text("{}")
            (folder / "art/battle-sprites/a.png").write_bytes(b"png")
            count, _ = pack_hd.pack(folder)
            self.assertEqual(count, 3)
            self.assertEqual(sorted(p.name for p in folder.rglob("*.mwtex")), [])
            self.assertTrue((folder / "art/srw64-units-hd.json").exists(), "only the pictures move")
            self.assertTrue((folder / "art/battle-sprites/a.png").exists())
            blob = (folder / "art.pack").read_bytes()
            magic, version, entries = pack_hd.HEADER.unpack_from(blob)
            self.assertEqual((magic, version, entries), (b"MWPK", 1, 3))
            self.assertEqual(struct.unpack_from("<I", blob)[0], 0x4B50574D, "the magic hd_texture.cpp compares")
            table = [pack_hd.ENTRY.unpack_from(blob, pack_hd.HEADER.size + i * pack_hd.ENTRY.size) for i in range(3)]
            self.assertEqual([e[0] for e in table], sorted(e[0] for e in table), "binary search needs them sorted")
            by_hash = {e[0]: e for e in table}
            for name, data in files.items():
                _, offset, size, _ = by_hash[pack_hd.fnv1a(name)]
                self.assertEqual(blob[offset:offset + size], data)
                self.assertEqual(offset % pack_hd.ALIGN, 0)

    def test_hash_matches_the_reader(self):
        self.assertEqual(pack_hd.fnv1a(""), 0xCBF29CE484222325)
        self.assertEqual(pack_hd.fnv1a("a"), 0xAF63DC4C8601EC8C)
        reader = (ROOT / "src/host/hd_texture.cpp").read_text(encoding="utf-8")
        self.assertIn("0x100000001B3ull", reader)
        self.assertIn('"art.pack"', reader)


if __name__ == "__main__":
    unittest.main()
