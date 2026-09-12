from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest
import zlib

TOOLS = Path(__file__).resolve().parents[1] / "tools/recomp"
sys.path.insert(0, str(TOOLS))
from audit_library_symbols import matches
from generate_cpu import bind_overlay_calls, bind_native_hooks, NATIVE_HOOKS
from native_inputs import compile_input


class SignatureTests(unittest.TestCase):
    def test_relocation_masks_preserve_opcodes_and_non_relocated_bytes(self) -> None:
        original = struct.pack(">4I", 0x0C000000, 0x3C040000, 0x24840000, 0x03E00008)
        signature = {"size": 16, "first_crc": zlib.crc32(original[:8]),
                     "full_crc": zlib.crc32(original),
                     "relocations": [{"kind": ".targ26", "offset": 0},
                                     {"kind": ".hi16", "offset": 4},
                                     {"kind": ".lo16", "offset": 8}]}
        relocated = struct.pack(">4I", 0x0C02CACC, 0x3C04800D, 0x2484EBD0, 0x03E00008)
        self.assertTrue(matches(relocated, signature))
        changed_opcode = struct.pack(">I", 0x0802CACC) + relocated[4:]
        self.assertFalse(matches(changed_opcode, signature))
        self.assertFalse(matches(relocated[:-4] + bytes(4), signature))
        self.assertFalse(matches(relocated[:-1], signature))

    def test_invalid_signature_extent_fails(self) -> None:
        with self.assertRaises(RuntimeError):
            matches(bytes(8), {"size": 8, "relocations": [{"kind": ".targ26", "offset": 6}]})


class OverlayCallTests(unittest.TestCase):
    def test_native_hooks_preserve_call_sites_and_only_rename_definitions(self) -> None:
        for original, replacement in NATIVE_HOOKS.items():
            source = (f"RECOMP_FUNC void {original}(uint8_t* rdram, recomp_context* ctx) {{\n"
                      f"    {original}(rdram, ctx);\n}}\n")
            adapted = bind_native_hooks(source)
            self.assertIn(f"RECOMP_FUNC void {replacement}(", adapted)
            self.assertIn(f"    {original}(rdram, ctx);", adapted)
            self.assertNotIn(f"RECOMP_FUNC void {original}(", adapted)

    def test_shared_vram_uses_runtime_lookup_without_renaming_definitions(self) -> None:
        source = ("RECOMP_FUNC void load_000AB160_func_801FD020(uint8_t* rdram, recomp_context* ctx) {\n"
                  "    load_000AB160_func_801FD020(rdram, ctx);\n"
                  "    resident_func_8007F704(rdram, ctx);\n}\n")
        adapted, count = bind_overlay_calls(source)
        self.assertEqual(count, 1)
        self.assertIn("LOOKUP_FUNC(0x801FD020)(rdram, ctx);", adapted)
        self.assertIn("RECOMP_FUNC void load_000AB160_func_801FD020(", adapted)
        self.assertIn("resident_func_8007F704(rdram, ctx);", adapted)


class NativeInputTests(unittest.TestCase):
    def test_native_buttons_and_overlapping_holds(self) -> None:
        compiled = compile_input({"schema": "srw64.recomp-input.v1", "events": [
            {"vi": 2, "duration": 3, "button": "start"}, {"vi": 3, "duration": 1, "button": "a"}]}, 6)
        self.assertEqual(compiled[:8], b"SRWI\x00\x00\x00\x07")
        self.assertEqual(struct.unpack(">7H", compiled[8:]), (0, 0, 0x1000, 0x9000, 0x1000, 0, 0))

    def test_foreign_clock_and_out_of_range_events_are_rejected(self) -> None:
        with self.assertRaises(RuntimeError):
            compile_input({"schema": "srw64.libretro-input.v1", "events": []}, 10)
        with self.assertRaises(RuntimeError):
            compile_input({"schema": "srw64.recomp-input.v1", "events": [{"vi": 9, "duration": 3, "button": "start"}]}, 10)
