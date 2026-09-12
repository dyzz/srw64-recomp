#!/usr/bin/env python3
"""Load an immutable native SRAM copy through the pinned reference emulator.

The reference core exposes EEPROM, four controller paks, SRAM and FlashRAM in
one buffer. Its 98c1b0d libretro_memory.h puts SRAM at 0x20800; cart/sram.c
uses the host's S8 lane XOR for both DMA endpoints. On little endian hosts,
each native big endian SRAM word must therefore be reversed before import.
No CPU state or game data is patched by this probe.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.recomp.libretro_runner import LibretroFrontend, _compile_events, _load_script
from audit_rom_variant import load_variant

CORE_SHA256 = "8cd7541261b06b89c18189d7621b825e4e6f906b64f4449056a40d0647a6f58d"
SRAM_OFFSET = 0x20800
SRAM_SIZE = 0x8000
SAVE_MEMORY_SIZE = 0x48800
SOURCE_URL = "https://github.com/libretro/mupen64plus-libretro-nx/blob/98c1b0d/"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def host_sram(data: bytes) -> bytes:
    if len(data) != SRAM_SIZE:
        raise RuntimeError("SRW64 SRAM must be exactly 32 KiB")
    if sys.byteorder == "big":
        return data
    return b"".join(data[index:index + 4][::-1] for index in range(0, len(data), 4))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("jp", "w1"), default="jp")
    parser.add_argument("--save-from", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=3000)
    args = parser.parse_args()
    if not 1 <= args.frames <= 12000:
        parser.error("frames must be in 1..12000")
    core = ROOT / "build/libretro/cores/mupen64plus_next_libretro.dylib"
    if digest(core.read_bytes()) != CORE_SHA256:
        raise RuntimeError("reference core differs from the reviewed binary")
    rom, variant, compatibility = load_variant(args.variant)
    source = args.save_from.read_bytes()
    native_hash = digest(source)
    imported = host_sram(source)
    script = _load_script(args.input)
    events = _compile_events(script, args.frames)
    frames = set(script.get("screenshots", []))
    if any(type(frame) is not int or not 0 <= frame < args.frames for frame in frames):
        raise RuntimeError("reference capture frame outside run")
    args.output.mkdir(parents=True, exist_ok=False)
    frontend = LibretroFrontend(core, rom, args.output, {
        "mupen64plus-rdp-plugin": "angrylion",
        "mupen64plus-rsp-plugin": "cxd4",
        "mupen64plus-cpucore": "dynamic_recompiler",
        "mupen64plus-angrylion-multithread": "all threads",
    })
    started = time.monotonic()
    report = {"schema": "srw64.reference-native-save.v1", "status": "incomplete",
              "evidence_scope": "reference-emulator-loading-native-SRAM",
              "native_sram_sha256": native_hash, "rom_variant": args.variant,
              "code_compatibility": compatibility,
              "rom_sha256": variant["sha256"], "core_sha256": CORE_SHA256,
              "script_sha256": digest(args.input.read_bytes()), "requested_frames": args.frames,
              "layout_sources": [SOURCE_URL + "libretro/libretro_memory.h",
                                 SOURCE_URL + "mupen64plus-core/src/device/cart/sram.c"],
              "sram_offset": SRAM_OFFSET, "sram_size": SRAM_SIZE,
              "host_byteorder": sys.byteorder, "captures": []}
    try:
        report["core"] = frontend.initialize()
        frontend.core.retro_get_memory_size.argtypes = [ctypes.c_uint]
        frontend.core.retro_get_memory_size.restype = ctypes.c_size_t
        frontend.core.retro_get_memory_data.argtypes = [ctypes.c_uint]
        frontend.core.retro_get_memory_data.restype = ctypes.c_void_p
        size = frontend.core.retro_get_memory_size(0)
        pointer = frontend.core.retro_get_memory_data(0)
        if size != SAVE_MEMORY_SIZE or not pointer:
            raise RuntimeError(f"unexpected reference save buffer: {size}")
        before = ctypes.string_at(pointer, size)
        ctypes.memmove(pointer + SRAM_OFFSET, imported, SRAM_SIZE)
        after = ctypes.string_at(pointer, size)
        if (after[:SRAM_OFFSET] != before[:SRAM_OFFSET] or
                after[SRAM_OFFSET + SRAM_SIZE:] != before[SRAM_OFFSET + SRAM_SIZE:] or
                after[SRAM_OFFSET:SRAM_OFFSET + SRAM_SIZE] != imported):
            raise RuntimeError("reference SRAM import readback differs")
        report["import_readback"] = "exact-SRAM-only"
        for frame in range(args.frames):
            frontend.run_frame(events.get(frame, set()))
            if frame in frames and frontend.last_frame is not None:
                path = args.output / f"frame-{frame:06d}.png"
                frontend.frame_image().save(path)
                report["captures"].append({"frame": frame, "path": str(path.resolve()),
                                            "sha256": digest(path.read_bytes())})
            if frontend.shutdown_requested:
                raise RuntimeError(f"reference requested shutdown at frame {frame}")
        final_sram = host_sram(ctypes.string_at(pointer + SRAM_OFFSET, SRAM_SIZE))
        (args.output / "final.sram").write_bytes(final_sram)
        report["final_sram_sha256"] = digest(final_sram)
        report["state"] = frontend.save_state(args.output / "final.state")
        report["diagnostics"] = frontend.diagnostics()
        if digest(args.save_from.read_bytes()) != native_hash:
            raise RuntimeError("source SRAM changed during reference run")
        report["status"] = "reference-run-completed-visual-review-required"
    finally:
        frontend.close()
        report["elapsed_seconds"] = time.monotonic() - started
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("status", "elapsed_seconds", "captures")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
