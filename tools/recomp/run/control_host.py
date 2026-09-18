#!/usr/bin/env python3
"""Inspect a native probe or atomically send a short N64 button pulse."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.run.native_inputs import BUTTONS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--buttons", nargs="+", choices=BUTTONS)
    action.add_argument("--quit", action="store_true", help="request a normal runtime shutdown")
    parser.add_argument("--duration", type=int, default=6, help="native VI ticks, at most 600")
    args = parser.parse_args()
    if not 1 <= args.duration <= 600:
        parser.error("duration must be in 1..600")
    output = args.output.resolve()
    state = json.loads((output / "live-state.json").read_text())
    if state["schema"] != "srw64.native-live-state.v1":
        raise RuntimeError("foreign live-state schema")
    if args.buttons or args.quit:
        if (output / "native-counters.json").exists() or (state["max_vis"] and state["vi"] >= state["max_vis"]):
            raise RuntimeError("native host already finished")
        mask = 0
        for name in args.buttons or []:
            mask |= BUTTONS[name]
        sequence = time.time_ns()
        temporary = output / f"control-{sequence}.tmp"
        magic = "SRWQ1" if args.quit else "SRWC1"
        temporary.write_text(f"{magic} {sequence} {mask} {args.duration}\n")
        temporary.replace(output / "control.txt")
        state["submitted"] = {"sequence": sequence, "mask": mask, "duration": args.duration}
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            events = output / "control-events.jsonl"
            if events.exists():
                for line in events.read_text().splitlines():
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue  # The host can be completing its last appended line.
                    if event["sequence"] == sequence:
                        state["applied"] = event
                        break
            if "applied" in state:
                break
            time.sleep(0.02)
        if "applied" not in state:
            raise RuntimeError("native host did not acknowledge the control within two seconds")
    print(json.dumps(state, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
