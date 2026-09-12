#!/usr/bin/env python3
"""Replay a captured audio task through recompiled RSP code; no audio oracle yet."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tomllib

from analyze_layout import ROOT


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/recomp/audio-probe")
    args = parser.parse_args()
    capture = args.capture.resolve()
    report_in = json.loads((capture / "report.json").read_text())
    baseline = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text())
    rom = (ROOT / "rom.z64").read_bytes()
    if digest(rom) != baseline["rom"]["sha256"] or report_in["rom_sha256"] != digest(rom):
        raise RuntimeError("ROM/capture identity differs")
    if report_in["schema"] != "srw64.recomp-rsp-tasks.v1" or report_in["status"] != "task-submissions-observed":
        raise RuntimeError("unsupported/incomplete task capture")
    snapshot = report_in["rdram_snapshot"]
    memory = (capture / snapshot["path"]).read_bytes()
    if digest(memory) != snapshot["sha256"] or len(memory) != 0x800000:
        raise RuntimeError("expected a verified 8 MiB task snapshot")
    observation = report_in["tasks"][snapshot["task_index"]]
    task = observation["descriptor"]
    if task["type"] != 2:
        raise RuntimeError("expected audio task")
    from capture_rsp_tasks import FIELDS
    descriptor_offset = observation["descriptor_address"] & 0x1FFFFFFF
    if descriptor_offset + 64 > len(memory) or dict(zip(FIELDS, struct.unpack_from(">16I", memory, descriptor_offset))) != task:
        raise RuntimeError("snapshot task descriptor differs from recorded observation")
    config_path = ROOT / "config/recomp/audio-probe.toml"
    config = tomllib.loads(config_path.read_text())
    code_start = task["ucode"] & 0x1FFFFFFF
    text_size, text_offset = config["text_size"], config["text_offset"]
    if memory[code_start:code_start + text_size] != rom[text_offset:text_offset + text_size]:
        raise RuntimeError("runtime audio executable prefix differs from recomp input")
    data_start = task["ucode_data"] & 0x1FFFFFFF
    targets = list(struct.unpack_from(">16H", memory, data_start + 0x10))
    if targets != config["extra_indirect_branch_targets"]:
        raise RuntimeError("audio command dispatch differs from recomp config")
    runtime = ROOT / "build/recomp/upstream/N64ModernRuntime"
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=runtime, text=True).strip()
    if revision != lock["sources"]["N64ModernRuntime"]["commit"]:
        raise RuntimeError("runtime checkout differs from lock")
    subprocess.run(["git", "diff", "--quiet", "HEAD"], cwd=runtime, check=True)
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    # Config's output path is fixed under build/recomp/audio-probe; replay
    # reports and binaries may be kept separately for independent captures.
    generated = ROOT / "build/recomp/audio-probe/audio.cpp"
    generated.parent.mkdir(parents=True, exist_ok=True)
    report: dict = {"schema": "srw64.recomp-audio-probe.v1", "status": "incomplete",
                    "evidence_scope": "isolated-native-rsp-task-without-output-oracle",
                    "rom_sha256": digest(rom), "runtime_commit": revision,
                    "capture_report_sha256": digest((capture / "report.json").read_bytes()),
                    "input_sha256": digest(memory), "task_index": snapshot["task_index"],
                    "descriptor_address": observation["descriptor_address"],
                    "executable_prefix_sha256": digest(memory[code_start:code_start + text_size]),
                    "executable_prefix_size": text_size, "dispatch_targets": targets,
                    "config_sha256": digest(config_path.read_bytes()), "address_sanitizer": True}
    try:
        with (work / "generate.log").open("w") as log:
            subprocess.run([str(ROOT / "build/recomp/tool-build/RSPRecomp"), str(config_path)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        original = generated.read_text()
        marker = '#include "librecomp/rsp_vu_impl.hpp"\n'
        if original.count(marker) != 1:
            raise RuntimeError("unexpected generated RSP source; cannot instrument writes")
        instrumented = work / "audio-instrumented.cpp"
        instrumented.write_text(original.replace(marker, marker +
            'void srw64_probe_dma_write(uint8_t*, uint32_t, uint32_t, uint32_t);\n'
            '#undef DO_DMA_WRITE\n'
            '#define DO_DMA_WRITE(length) srw64_probe_dma_write(rdram, dma_mem_address, dma_dram_address, (length))\n'))
        binary = work / "audio-probe"
        includes = [runtime / "librecomp/include", runtime / "librecomp/include/librecomp", runtime / "ultramodern/include",
                    runtime / "thirdparty/sse2neon", ROOT / "build/recomp/upstream/N64Recomp/include"]
        command = ["clang++", "-std=c++20", "-O2", "-fsanitize=address", "-fno-omit-frame-pointer",
                   *["-I" + str(path) for path in includes], str(instrumented), str(runtime / "librecomp/src/rsp.cpp"),
                   str(ROOT / "tools/recomp/audio_probe.cpp"), "-o", str(binary)]
        with (work / "compile.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        result_path = work / "rdram-after-native.bin"
        with (work / "native.log").open("w") as log:
            subprocess.run([str(binary), str(capture / snapshot["path"]), hex(observation["descriptor_address"]), str(result_path), str(work / "dma-writes.json")],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=20)
        result = result_path.read_bytes()
        if len(result) != len(memory):
            raise RuntimeError("native output memory size differs")
        changes: list[dict] = []
        for index, (before, after) in enumerate(zip(memory, result)):
            if before != after:
                if changes and changes[-1]["end"] == index:
                    changes[-1]["end"] += 1
                else:
                    changes.append({"start": index, "end": index + 1})
        report.update({"status": "native-task-reached-break", "output_sha256": digest(result),
                       "binary_sha256": digest(binary.read_bytes()), "generated_sha256": digest(generated.read_bytes()),
                       "instrumented_sha256": digest(instrumented.read_bytes()),
                       "harness_sha256": digest((ROOT / "tools/recomp/audio_probe.cpp").read_bytes()),
                       "recompiler_sha256": digest((ROOT / "build/recomp/tool-build/RSPRecomp").read_bytes()),
                       "changed_byte_count": sum(item["end"] - item["start"] for item in changes), "changed_ranges": changes})
        if "following_task_snapshot" in report_in:
            following = report_in["following_task_snapshot"]
            reference = (capture / following["path"]).read_bytes()
            if len(reference) != len(result) or digest(reference) != following["sha256"]:
                raise RuntimeError("following-task snapshot identity differs")
            if following["task_index"] != snapshot["task_index"] + 1 or following["sp_status"] & 0x203 != 0x203:
                raise RuntimeError("expected consecutive task boundary with SP HALT/BROKE/SIG2 set")
            writes = json.loads((work / "dma-writes.json").read_text())["writes"]
            if not writes:
                raise RuntimeError("audio task produced no recorded DMA writes")
            failures = [item for item in writes if result[item["start"]:item["end"]] != reference[item["start"]:item["end"]]]
            report["oracle"] = {"scope": "all recorded RSP DMA destination ranges at next task submission",
                                "reference_sha256": digest(reference), "sp_status": following["sp_status"],
                                "write_count": len(writes), "mismatching_ranges": failures}
            if failures:
                raise RuntimeError(f"{len(failures)} RSP destination ranges differ from emulator")
            report["status"] = "native-task-output-byte-identical"
            report["evidence_scope"] = "captured-audio-task-all-DMA-destinations-match-next-emulator-task-boundary"
    except Exception as exc:
        report.update({"status": "failed", "error": str(exc)})
        raise
    finally:
        (work / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({key: value for key, value in report.items() if key != "changed_ranges"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
