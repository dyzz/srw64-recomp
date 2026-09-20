"""Native importer vs the existing Python codecs. Fixtures contain no game data.

Run by CTest with --probe. Optional SRW64_TEST_ROM enables real-ROM parity on a
trusted local runner; public CI deliberately skips that single integration test.
"""
from __future__ import annotations
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
PROBE = os.environ.get("SRW64_IMPORT_PROBE")
if "--probe" in sys.argv:
    pos = sys.argv.index("--probe")
    PROBE = sys.argv[pos + 1]
    del sys.argv[pos:pos + 2]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture():
    # Same descriptor/layout formats and portrait coordinates, fabricated bytes.
    from srw64_rom.resources import lz_encode, RESOURCE_BASE
    rom = bytearray(RESOURCE_BASE + 0x10000)
    def u32(p, *values):
        struct.pack_into(">" + "I" * len(values), rom, p, *values)
    u32(0x100, 0x200, 0x300)
    units = (0, 1, 0x124, 0xfffe, 2, 0xfffd, 0x12c, 0x555, 0xffff)
    record = b"12345678" + struct.pack(">" + "H" * len(units), *units)
    u32(0x200, 1, 16, len(record)); rom[0x210:0x210 + len(record)] = record
    u32(0x300, 1, 16, 12); rom[0x310:0x31c] = b"87654321" + struct.pack(">2H", 1, 0xffff)
    faces = (27, 28, 25, 26, 31, 32, 29, 30)
    linked = (41, 230, 133, 131, 132, 152, 151, 153)
    struct.pack_into(">8H", rom, 0x1090a0 + 0x801c6bf0 - 0x801c2600, *faces)
    palette = bytes.fromhex("0003008000000000") + b"".join(struct.pack(">H", (i * 521) & 0xffff) for i in range(128))
    image96 = struct.pack(">4H", 6, 96, 96, 0) + bytes(i % 128 for i in range(96 * 96))
    image97 = struct.pack(">4H", 15, 97, 97, 0) + bytes((i * 13) % 128 for i in range(97 * 97))
    u32(RESOURCE_BASE, 3)
    offset = 32
    for i, raw in enumerate((image96, palette, image97)):
        encoded = struct.pack(">I", len(raw)) + lz_encode(raw)
        u32(RESOURCE_BASE + 4 + i * 8, offset, len(encoded))
        rom[RESOURCE_BASE + offset:RESOURCE_BASE + offset + len(encoded)] = encoded
        offset += len(encoded)
    for face in faces + linked:
        struct.pack_into(">2H", rom, 0x84220 + face * 4, 2 if face in (41, 230) else 0, 1)
    locales = [{"schema": "srw64.locale.v1", "source_locale": "ja", "locale": loc,
                "font": "test-font", "display_name": loc, "catalog_sha256": sha(loc.encode()),
                "entries": [], "ui": {"manual": "manual"}} for loc in ("ja", "en")]
    locales[1]["entries"] = [{"key": "base:t00_00000", "source_sha256": sha(record),
                               "target": "English<G:0124><BR><STOP><G:012C><G:0555><END>", "review_status": "draft"}]
    spec = {"schema": "srw64.native-import-spec.v1", "baseline": "srw64-jp-rev0",
            "rom_sha256": sha(rom), "rom_size": len(rom),
            "text_layout": {"pointer_table_offset": 0x100, "table_count": 2, "entry_header_size": 8},
            "glyphs": {"0": " ", "1": "日", "2": "本", "292": "not a literal name"},
            "locales": locales, "locale": "ja", "font_size": 14, "resolution_scale": 2}
    return bytes(rom), spec


@unittest.skipUnless(PROBE, "CMake-built SRW64_IMPORT_PROBE required")
class NativeImporterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom_bytes, cls.spec_value = fixture()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rom = self.root / "synthetic.z64"; self.rom.write_bytes(self.rom_bytes)
        self.spec = copy.deepcopy(self.spec_value)
        self.spec_path = self.root / "spec.json"
        self.cache = self.root / "imported"

    def run_probe(self, mode="import", destination=None, okay=True, env=None):
        self.spec_path.write_text(json.dumps(self.spec), encoding="utf-8")
        result = subprocess.run([PROBE, mode, str(self.rom), str(self.spec_path), str(destination or self.cache)],
                                capture_output=True, text=True, encoding="utf-8", timeout=60, env=env)
        if okay:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_text_and_translation_parity(self):
        from srw64_rom.baseline import TextLayout
        from srw64_rom.text import parse_entries
        from srw64_native.catalog import CONTROL, compile_locale, text_key
        path = Path(self.run_probe().stdout.strip())
        data = json.loads((path / "dialogue.json").read_text(encoding="utf-8"))
        records = list(parse_entries(self.rom_bytes, TextLayout(0x100, 2, 8, frozenset(CONTROL))))
        sources, hashes = {}, {}
        for entry in records:
            key = text_key(entry.table_id, entry.text_id)
            sources[key] = "".join(CONTROL.get(u, f"<G:{u:04X}>" if 0x124 <= u <= 0x12c else
                                               self.spec["glyphs"].get(str(u), f"<G:{u:04X}>")) for u in entry.units)
            hashes[key] = sha(entry.data)
        self.assertEqual(data["source_entries"], sources)
        for locale in self.spec["locales"]:
            self.assertEqual(data["locale_catalogs"][locale["locale"]]["entries"], compile_locale(locale, sources, hashes))
        self.assertEqual(data["glyphs"], self.spec["glyphs"])
        self.assertEqual(data["name_entry_assets"]["route_faces"], [[27,28,25,26],[31,32,29,30]])
        self.assertEqual(data["name_entry_assets"]["link_faces"], [[41,230],[133,131,132],[152,151,153]])

    def test_portrait_pixel_parity(self):
        from PIL import Image
        from srw64_rom.resources import ResourceTable
        path = Path(self.run_probe().stdout.strip())
        data = json.loads((path / "dialogue.json").read_text(encoding="utf-8"))
        table = ResourceTable(self.rom_bytes)
        self.assertEqual(len(data["name_entry_assets"]["portraits"]), 16)
        for row in data["name_entry_assets"]["portraits"].values():
            image, _ = table.extract(row["resource_id"]); palette, _ = table.extract(row["palette_id"])
            colors = [tuple(round(((v >> s) & 31) * 255 / 31) for s in (11,6,1)) + (255 * (v & 1),)
                      for (v,) in struct.iter_unpack(">H", palette[8:])]
            expected = bytes(c for i in image[8:] for c in colors[i])
            with Image.open(path / row["original"]) as png:
                self.assertEqual(png.convert("RGBA").tobytes(), expected)
            self.assertEqual(sha((path / row["original"]).read_bytes()), row["original_sha256"])

    def test_cache_reuse_and_versioned_metadata(self):
        first = Path(self.run_probe().stdout.strip())
        original = (first / "manifest.json").read_bytes()
        reused = self.run_probe(); self.assertEqual(Path(reused.stdout.strip()), first)
        self.assertNotIn("SRW64_ROM_IMPORT_BEGIN", reused.stderr)
        self.spec["font_size"] = 15
        second = Path(self.run_probe().stdout.strip()); self.assertNotEqual(first, second)
        self.assertEqual((first / "manifest.json").read_bytes(), original)

    def test_corrupt_cache_fails_closed(self):
        first = Path(self.run_probe().stdout.strip())
        (first / "name-entry/face-27.png").write_bytes(b"broken")
        failed = self.run_probe(okay=False)
        self.assertIn("Cached import is invalid", failed.stderr)
        self.assertEqual((first / "name-entry/face-27.png").read_bytes(), b"broken")

    def test_bad_translation_and_failed_import_keep_previous(self):
        good = Path(self.run_probe().stdout.strip()); before = (good / "manifest.json").read_bytes()
        cases = ["<G:0124><G:012C><G:0555><END>", "<G:0124><STOP><G:012C><END>",
                 "bad\n<G:0124><STOP><G:012C><G:0555><END>", "<END><G:0124><STOP><G:012C><G:0555><END>"]
        for target in cases:
            self.spec["locales"][1]["entries"][0]["target"] = target
            self.run_probe(okay=False)
            self.assertEqual((good / "manifest.json").read_bytes(), before)
            self.assertFalse(list((self.cache / "cache").glob("*.tmp-*")))
        self.spec = copy.deepcopy(self.spec_value)
        self.spec["locales"][1]["entries"][0]["source_sha256"] = "0" * 64
        self.assertIn("Translation source changed", self.run_probe(okay=False).stderr)

    def test_wrong_rom_rejected(self):
        changed = bytearray(self.rom_bytes); changed[-1] ^= 1; self.rom.write_bytes(changed)
        self.assertIn("ROM identity", self.run_probe(okay=False).stderr)
        self.assertFalse((self.cache / "cache").exists())

    def test_native_first_boot_and_resume(self):
        user = self.root / "user data"
        first = json.loads(self.run_probe("bootstrap", user).stdout)
        self.assertEqual(first, {"argc": 5, "texts": 2, "locale": "ja", "portraits": 16})
        second = self.run_probe("bootstrap", user)
        self.assertEqual(json.loads(second.stdout)["argc"], 6)
        self.assertNotIn("SRW64_ROM_IMPORT_BEGIN", second.stderr)
        pointer = (user / "last-session.txt").read_bytes()
        self.spec["locales"][1]["entries"][0]["target"] = "bad<END>"
        self.run_probe("bootstrap", user, okay=False)
        self.assertEqual((user / "last-session.txt").read_bytes(), pointer)

    def test_first_boot_without_developer_path(self):
        env = dict(os.environ)
        env["PATH"] = str(self.root / "no-developer-tools")
        result = self.run_probe("bootstrap", self.root / "clean-user", env=env)
        self.assertEqual(json.loads(result.stdout)["argc"], 5)

    def test_bad_resource_leaves_no_partial_cache(self):
        damaged = bytearray(self.rom_bytes)
        struct.pack_into(">I", damaged, 0xa20bd0 + 4, 0xfffffff0)
        self.rom.write_bytes(damaged); self.spec["rom_sha256"] = sha(damaged)
        self.run_probe(okay=False)
        self.assertFalse(list((self.cache / "cache").iterdir()))

    def test_lz_oracle_patterns(self):
        from srw64_rom.resources import lz_encode, lz_decode
        rng = random.Random(3155)
        patterns = [b"", b"a", b"\0" * 5000, b"abc" * 2000, bytes(range(256)) * 20]
        patterns += [rng.randbytes(n) for n in (2,3,7,8,9,65,66,67,1023,1024,1025,8192)]
        for raw in patterns:
            encoded = lz_encode(raw); src = self.root / "lz.bin"; dst = self.root / "decoded.bin"
            src.write_bytes(encoded)
            result = subprocess.run([PROBE, "lz", str(src), str(len(raw)), str(dst)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            oracle, consumed = lz_decode(encoded, len(raw))
            self.assertEqual(dst.read_bytes(), oracle); self.assertEqual(int(result.stdout), consumed)

    @unittest.skipUnless(os.environ.get("SRW64_TEST_ROM"), "Trusted local matching ROM required; not public CI")
    def test_real_rom_python_oracle(self):
        from srw64_native.catalog import source_catalog, compile_locale
        from srw64_native.name_assets import prepare_name_assets
        from PIL import Image
        self.rom = Path(os.environ["SRW64_TEST_ROM"]).resolve()
        subprocess.run([PROBE, "metadata", str(self.spec_path)], check=True)
        self.spec = json.loads(self.spec_path.read_text(encoding="utf-8"))
        native = Path(self.run_probe().stdout.strip())
        data = json.loads((native / "dialogue.json").read_text(encoding="utf-8"))
        sources, hashes, glyphs = source_catalog(ROOT, self.rom)
        self.assertEqual(data["source_entries"], sources); self.assertEqual(data["glyphs"], glyphs)
        for locale in self.spec["locales"]:
            self.assertEqual(data["locale_catalogs"][locale["locale"]]["entries"], compile_locale(locale, sources, hashes))
        original = prepare_name_assets(ROOT, self.rom.read_bytes(), self.root / "python-portraits", include_hd=False)
        for face, row in original["portraits"].items():
            with Image.open(row["original"]) as a, Image.open(native / data["name_entry_assets"]["portraits"][face]["original"]) as b:
                self.assertEqual(a.convert("RGBA").tobytes(), b.convert("RGBA").tobytes())

class ImportMetadataTests(unittest.TestCase):
    def test_repository_metadata_is_rom_free_and_deterministic(self):
        spec = importlib.util.spec_from_file_location("import_spec", ROOT / "tools/release/build_import_spec.py")
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        a = module.build_spec(ROOT); b = module.build_spec(ROOT)
        self.assertEqual(a, b)
        self.assertNotIn("source_entries", a); self.assertNotIn("name_entry_assets", a)
        self.assertEqual(a["text_layout"]["table_count"], 20)
        self.assertEqual([v["locale"] for v in a["locales"]], ["ja", "zh-Hans", "en"])
        for locale in a["locales"]:
            for entry in locale["entries"]:
                self.assertLessEqual(set(entry), {"key", "target", "source_sha256", "review_status"})

if __name__ == "__main__":
    unittest.main(verbosity=2)
