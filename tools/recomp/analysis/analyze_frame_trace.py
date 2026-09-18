#!/usr/bin/env python3
"""Analyze completed-frame continuity in a stationary dialogue probe."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics


def analyze(directory: Path, start: int = 0) -> dict:
    rows = [json.loads(line) for line in (directory / "frame-trace.jsonl").read_text().splitlines()]
    if not rows or (directory / "frame-trace.rgb").stat().st_size != len(rows) * 160 * 120 * 3:
        raise ValueError("Frame trace is incomplete")
    rows = [row for row in rows if row["vi"] >= start]
    if len(rows) < 2:
        raise ValueError("Need at least two completed frames")
    jumps, intervals, capture_intervals, ordinary_intervals = [], [], [], []
    switches = []
    for a, b in zip(rows, rows[1:]):
        if b["present"] != a["present"] + 1 or b["vi"] < a["vi"]:
            raise ValueError("Temporal trace skipped a completed frame or reversed VI")
        interval = b["vi"] - a["vi"]
        intervals.append(interval)
        (capture_intervals if a["present"] % 60 == 0 else ordinary_intervals).append(interval)
        if a["image_mode"] != b["image_mode"]:
            switches.append(b)
        elif b["mean_delta"] > 2:
            jumps.append(b)

    def timing(values):
        return {"count": len(values), "mean_vi": statistics.mean(values), "max_vi": max(values)} if values else None

    return {"schema": "srw64.frame-continuity.v1", "directory": str(directory.resolve()),
            "scope": "stationary standard dialogue; marker animation allowed; intended image-mode changes excluded",
            "frames": len(rows), "vi_range": [rows[0]["vi"], rows[-1]["vi"]],
            "unexpected_visual_jumps": len(jumps), "jumps": jumps, "image_switches": switches,
            "largest_same_mode_delta": max(b["mean_delta"] for a, b in zip(rows, rows[1:]) if a["image_mode"] == b["image_mode"]),
            "intervals": timing(intervals), "after_every_60th_present": timing(capture_intervals),
            "other_present_intervals": timing(ordinary_intervals)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--from-vi", type=int, default=0)
    parser.add_argument("--require-stable", action="store_true")
    args = parser.parse_args()
    result = analyze(args.directory, args.from_vi)
    (args.directory / "frame-continuity.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("frames", "vi_range", "unexpected_visual_jumps", "largest_same_mode_delta", "intervals", "after_every_60th_present", "other_present_intervals")}, indent=2))
    if args.require_stable and result["unexpected_visual_jumps"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
