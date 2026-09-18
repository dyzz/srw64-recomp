#!/usr/bin/env python3
"""Check bottom-toolbar visibility during a 4:3 autoplay trace.

Read the panel and controls from completed GPU pixels, independently of the
guest's active-speaker state. --require-visible needs an interval wholly inside
visible dialogue; --require-matching-dialogue also checks scene closures.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def analyze(directory: Path, start: int = 0, end: int | None = None) -> dict:
    rows = [json.loads(line) for line in (directory / "frame-trace.jsonl").read_text().splitlines()]
    pixels = (directory / "frame-trace.rgb").read_bytes()
    stride = 160 * 120 * 3
    if len(rows) < 2 or len(pixels) != len(rows) * stride:
        raise ValueError("Need a complete trace of at least two completed frames")
    gaps, handoffs, mismatches = [], [], []
    matched_state_count = closed_count = 0
    visible_count = 0
    selected = []
    for i, row in enumerate(rows):
        if (row["width"], row["height"]) != (160, 120):
            raise ValueError("Unexpected trace sample dimensions")
        if i and (row["present"] != rows[i-1]["present"]+1 or row["vi"] < rows[i-1]["vi"]):
            raise ValueError("Temporal trace skipped a frame or reversed VI")
        if row["vi"] < start or (end is not None and row["vi"] > end):
            continue
        selected.append(row)
        # Logical y=230, x=100..308: inside the panel, above the controls glyphs.
        strip = pixels[i*stride + (115*160+50)*3:i*stride + (115*160+155)*3]
        dark_fraction = sum(max(strip[n:n+3]) < 40 for n in range(0, len(strip), 3)) / 105
        glyphs = pixels[i*stride + (117*160+50)*3:i*stride + (117*160+155)*3]
        bright_fraction = sum(max(glyphs[n:n+3]) > 80 for n in range(0, len(glyphs), 3)) / 105
        visible = dark_fraction >= .95 and bright_fraction >= .1
        visible_count += visible
        if not visible:
            if not gaps or gaps[-1]["last_present"] != row["present"]-1:
                gaps.append({"first_present": row["present"], "first_vi": row["vi"], "frames": 0})
            gaps[-1].update(last_present=row["present"], last_vi=row["vi"], frames=gaps[-1]["frames"]+1)
        state = row.get("dialogue")
        if "dialogue" in row:
            matched_state_count += 1
            expected = bool(state and state["visible_boxes"])
            closed_count += not expected
            if visible != expected:
                mismatches.append({"present": row["present"], "vi": row["vi"],
                                   "toolbar_visible": visible, "dialogue": state})
        if state and state["visible_boxes"] and not state["active_boxes"]:
            handoffs.append({"present": row["present"], "vi": row["vi"], "toolbar_visible": visible})
    if len(selected) < 2:
        raise ValueError("Need at least two frames in the selected interval")
    return {"schema": "srw64.autoplay-toolbar-continuity.v1", "directory": str(directory.resolve()),
            "scope": "4:3 standard dialogue; GPU toolbar pixels with optional workload-matched dialogue visibility",
            "frames": len(selected), "vi_range": [selected[0]["vi"], selected[-1]["vi"]],
            "toolbar_visible_frames": visible_count, "toolbar_missing_frames": len(selected)-visible_count,
            "missing_episodes": gaps, "inactive_dialogue_frames": handoffs,
            "dialogue_visibility": {"frames_with_state": matched_state_count,
                                    "visible_dialogue_frames": matched_state_count-closed_count,
                                    "closed_dialogue_frames": closed_count,
                                    "mismatches": mismatches},
            "pixel_check": {"sample_row": 115, "sample_columns": [50, 155],
                            "rgb_max_exclusive": 40, "minimum_dark_fraction": .95,
                            "glyph_row": 117, "glyph_rgb_max_min_exclusive": 80, "minimum_bright_fraction": .1}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--from-vi", type=int, default=0)
    parser.add_argument("--to-vi", type=int)
    parser.add_argument("--require-visible", action="store_true")
    parser.add_argument("--require-matching-dialogue", action="store_true",
                        help="allow scene closures, requiring GPU toolbar visibility to match the frame's dialogue visibility")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(args.directory, args.from_vi, args.to_vi)
    (args.output or args.directory / "toolbar-continuity.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k: result[k] for k in ("frames", "vi_range", "toolbar_visible_frames", "toolbar_missing_frames", "missing_episodes")}, indent=2))
    if args.require_visible and result["toolbar_missing_frames"]:
        raise SystemExit(1)
    if args.require_matching_dialogue and (result["dialogue_visibility"]["frames_with_state"] != result["frames"]
                                          or result["dialogue_visibility"]["mismatches"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
