#!/usr/bin/env python3
"""Confirm only an exact, previously reviewed counterattack menu text mask.

This bounded probe does not recognize arbitrary menus or decide battle tactics.
It stops input when the captured 'counterattack start' label is absent.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from PIL import Image

LABEL_BOX = (380, 452, 576, 494)


def label_mask(path: Path) -> tuple[bool, ...]:
    with Image.open(path) as frame:
        if frame.size != (960, 720):
            raise RuntimeError("counterattack template requires a 960x720 GPU frame")
        tile = frame.convert("RGB").crop(LABEL_BOX)
        return tuple(min(pixel) > 150 for pixel in tile.get_flattened_data())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--max-actions", type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 300 or not 1 <= args.max_actions <= 16:
        parser.error("seconds must be 1..300 and max-actions 1..16")
    reference = label_mask(args.reference)
    if sum(reference) != 2679:
        raise RuntimeError("reference differs from the reviewed counterattack text mask")
    output = args.output.resolve()
    deadline = time.monotonic() + args.seconds
    last_present = -1
    last_action_vi = -120
    actions: list[dict] = []
    trace = output / f"auto-counter-{time.time_ns()}.jsonl"
    with trace.open("x") as log:
        while time.monotonic() < deadline and len(actions) < args.max_actions:
            if (output / "report.json").exists() or (output / "native-counters.json").exists():
                break
            frames = list(output.glob("present-*.png"))
            if not frames:
                time.sleep(0.1)
                continue
            frame = max(frames, key=lambda p: int(p.stem.split("-")[1]))
            sidecar = frame.with_suffix(".json")
            if not sidecar.exists():
                time.sleep(0.1)
                continue
            try:
                metadata = json.loads(sidecar.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                time.sleep(0.05)
                continue
            present = metadata["present"]
            if (present <= last_present or metadata.get("GPU_completion") != "completed" or
                    metadata.get("native_vi_at_draw", -1) < last_action_vi + 120):
                time.sleep(0.1)
                continue
            last_present = present
            if label_mask(frame) != reference:
                time.sleep(0.1)
                continue
            command = [sys.executable, str(Path(__file__).with_name("control_host.py")),
                       str(output), "--buttons", "a", "--duration", "6"]
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            event = json.loads(result.stdout)["applied"]
            last_action_vi = event["vi"]
            record = {"schema": "srw64.native-auto-counter-event.v1", "frame": str(frame),
                      "frame_sha256": hashlib.sha256(frame.read_bytes()).hexdigest(),
                      "metadata": metadata, "event": event}
            actions.append(record)
            log.write(json.dumps(record) + "\n")
            log.flush()
            print(json.dumps({"action": len(actions), "frame_vi": metadata["native_vi_at_draw"],
                              "applied_vi": last_action_vi}), flush=True)
    print(json.dumps({"schema": "srw64.native-auto-counter-run.v1", "actions": len(actions),
                      "trace": str(trace), "reference_sha256": hashlib.sha256(args.reference.read_bytes()).hexdigest()}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
