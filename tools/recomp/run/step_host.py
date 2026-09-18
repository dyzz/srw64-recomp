#!/usr/bin/env python3
"""Send a short native input sequence and locate a completed post-input GPU frame."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.run.native_inputs import BUTTONS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--buttons", nargs="+", choices=BUTTONS, default=[])
    args = parser.parse_args()
    if len(args.buttons) > 32:
        parser.error("at most 32 sequential button pulses")
    output = args.output.resolve()
    applied: list[dict] = []
    for button in args.buttons:
        command = [sys.executable, str(Path(__file__).with_name("control_host.py")),
                   str(output), "--buttons", button, "--duration", "6"]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        event = json.loads(result.stdout)["applied"]
        applied.append({"button": button, **event})
        time.sleep(0.4)
    required_vi = applied[-1]["vi"] + applied[-1]["duration"] + 60 if applied else 0
    deadline = time.monotonic() + 5
    frame: Path | None = None
    metadata: dict = {}
    while time.monotonic() < deadline:
        candidates = sorted(output.glob("present-*.png"), key=lambda p: int(p.stem.split("-")[1]))
        for candidate in reversed(candidates):
            sidecar = candidate.with_suffix(".json")
            if not sidecar.exists():
                continue
            try:
                data = json.loads(sidecar.read_text())
            except json.JSONDecodeError:
                continue
            if (data.get("schema") == "srw64.native-gpu-frame.v1" and
                    data.get("GPU_completion") == "completed" and
                    data.get("native_vi_at_draw", -1) >= required_vi):
                frame, metadata = candidate, data
                break
        if frame is not None:
            break
        time.sleep(0.05)
    if frame is None:
        raise RuntimeError("no completed native GPU frame after the requested input")
    report = {"schema": "srw64.native-input-step.v1", "applied": applied,
              "frame": str(frame), "frame_metadata": metadata,
              "state": json.loads((output / "live-state.json").read_text())}
    audio = output / "audio-live.json"
    if audio.exists():
        report["audio"] = json.loads(audio.read_text())
    if applied:
        (output / f"step-{time.time_ns()}.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
