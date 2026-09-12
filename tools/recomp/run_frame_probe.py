#!/usr/bin/env python3
"""Run a provenance-recorded RT64 replay of a captured SRW64 graphics task."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font-pack", type=Path)
    parser.add_argument("--native-marker", type=Path)
    parser.add_argument("--dump-textures", action="store_true")
    parser.add_argument("--native-resolution", action="store_true")
    parser.add_argument("--resolution-scale", type=int, choices=range(1, 9))
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.exists():
        raise RuntimeError("output directory must be new")
    inputs = {name: digest(source / name) for name in ("latest-gfx-rdram.bin", "latest-gfx-task.bin")}
    environment = dict(os.environ)
    for key in ("SRW64_FONT_PACK", "SRW64_TEXTURE_DUMP", "SRW64_NATIVE_RESOLUTION", "SRW64_AUDIO_OUTPUT", "SRW64_RESOLUTION_SCALE", "SRW64_NATIVE_MARKER"):
        environment.pop(key, None)
    environment["SRW64_NATIVE_RESOLUTION"] = "1" if args.native_resolution else "0"
    native_marker = None
    if args.native_marker:
        from prepare_native_marker import validate
        native_marker = validate(args.native_marker)
        environment["SRW64_NATIVE_MARKER"] = native_marker["path"]
    if args.resolution_scale:
        environment["SRW64_RESOLUTION_SCALE"] = str(args.resolution_scale)
    pack = None
    if args.font_pack:
        args.font_pack = args.font_pack.resolve()
        if not (args.font_pack / "rt64.json").is_file():
            raise RuntimeError("font pack manifest missing")
        environment["SRW64_FONT_PACK"] = str(args.font_pack)
        pack = {str(p.relative_to(args.font_pack)): digest(p) for p in sorted(args.font_pack.rglob("*")) if p.is_file()}
    if args.dump_textures:
        environment["SRW64_TEXTURE_DUMP"] = str(output / "textures")
    build = ROOT / "build/recomp/gfx-build"
    output.parent.mkdir(parents=True, exist_ok=True)
    with (output.parent / (output.name + ".build.log")).open("x") as log:
        subprocess.run(["cmake", "--build", str(build), "--target", "srw64-frame-host", "-j", "6"],
                       check=True, stdout=log, stderr=subprocess.STDOUT)
    binary = build / "srw64-frame-host"
    command = [str(binary), str(source), str(output)]
    log_path = output.parent / (output.name + ".log")
    with log_path.open("x") as log:
        result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    output.mkdir(exist_ok=True)
    frames = [{"path": p.name, "sha256": digest(p), "metadata": json.loads(p.with_suffix(".json").read_text())}
              for p in sorted(output.glob("present-*.png"))]
    report = {"schema": "srw64.native-frame-probe.v1", "evidence_scope": "captured-task-renderer-replay",
              "source": str(source), "input_sha256": inputs, "binary_sha256": digest(binary), "command": command,
              "source_sha256": {p.name: digest(p) for p in (ROOT / "tools/recomp/native-host").iterdir() if p.is_file()},
              "font_pack": str(args.font_pack) if args.font_pack else None, "font_pack_sha256": pack,
              "native_marker": native_marker,
              "native_resolution": args.native_resolution, "resolution_scale": args.resolution_scale, "exit_code": result.returncode,
              "native_log_sha256": digest(log_path), "frames": frames,
              "status": "GPU-frame-captured" if result.returncode == 0 and frames else "failed"}
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": report["status"], "frames": len(frames), "output": str(output)}))
    return 0 if report["status"] == "GPU-frame-captured" else 1


if __name__ == "__main__":
    raise SystemExit(main())
