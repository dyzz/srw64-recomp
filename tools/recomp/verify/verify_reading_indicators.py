#!/usr/bin/env python3
"""Verify 4:3 completed GPU samples against their reading-indicator snapshots."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def verify(directory: Path) -> dict:
    rows = [json.loads(line) for line in (directory / "frame-trace.jsonl").read_text().splitlines()]
    pixels = (directory / "frame-trace.rgb").read_bytes()
    stride = 160*120*3
    if len(rows) < 2 or len(pixels) != len(rows)*stride:
        raise ValueError("Incomplete completed-frame trace")
    failures = []
    counts = {key: 0 for key in ("toolbar", "focus", "handoff", "progress", "revealing", "waiting", "history", "manual", "pagination")}
    levels, slots = set(), set()
    progress_states = []
    for i, row in enumerate(rows):
        if i and row["present"] != rows[i-1]["present"]+1:
            raise ValueError("Trace skipped a completed frame")
        state = row.get("dialogue")
        if not state or not state["visible_boxes"]:
            continue
        sample = pixels[i*stride:(i+1)*stride]

        def rgb(x, y):
            offset = (int(y)*160+int(x))*3
            return tuple(sample[offset:offset+3])

        def close(actual, target):
            return max(abs(a-b) for a, b in zip(actual, target)) <= 20

        def fail(kind, **details):
            failures.append({"present": row["present"], "vi": row["vi"], "kind": kind, **details})

        expected_level = state["speed"] if state["automatic"] else 0
        maximum = state.get("speed_maximum", 8) # Earlier recordings used eight steps.
        pitch = 28 / maximum
        actual_level = 0
        for bar in range(maximum):
            color = rgb(math.floor((38+bar*pitch+(pitch-1)/2)/2+.5), 117)
            lit = color[0] > 200 and color[1] > 100 and color[2] < 110
            actual_level += lit
            if lit != (bar < expected_level):
                fail("speed_step", step=bar, rgb=color, expected_level=expected_level)
        counts["toolbar"] += 1
        levels.add(expected_level)
        counts["manual"] += not state["automatic"]
        counts["history"] += state["history_open"]
        focus = state["focus"]
        if not focus:
            continue
        counts["pagination"] += focus["pages"] > 1
        slots.add(focus["slot"])
        if not focus["active"]:
            counts["handoff"] += 1
            if focus["event"] != state["reading_event"]:
                fail("handoff_owner")
        if state["history_open"]:
            # History covers the dialogue panels, but the bottom speed stays.
            continue
        counts["focus"] += 1
        color = rgb(math.floor((focus["x"]-3)/2+.5), math.floor((focus["y"]-10)/2+.5))
        if not close(color, (105, 212, 255)):
            fail("speaker_marker", rgb=color, slot=focus["slot"])
        advance = state["advance"]
        if not advance["visible"] or focus["event"] != state["reading_event"]:
            continue
        counts["progress"] += 1
        counts["waiting"] += advance["waiting"]
        counts["revealing"] += not advance["waiting"]
        left, top, width = focus["x"]+116, focus["y"]-20, 61
        expected_color = (255, 163, 59) if advance["waiting"] else (105, 212, 255)
        xs = list(range(math.ceil(left/2), math.ceil((left+width)/2)))
        actual = sum(close(rgb(x, math.floor(top/2+.5)), expected_color) for x in xs)
        expected = sum(x*2 < left+width*advance["permille"]/1000 for x in xs)
        # One downsampled pixel may straddle the antialiased fill endpoint.
        if abs(actual-expected) > 1:
            fail("progress_fill", actual_pixels=actual, expected_pixels=expected, progress=advance)
        progress_states.append({"present": row["present"], "vi": row["vi"], "event": focus["event"],
                                "slot": focus["slot"], "page": focus["page"], **advance})
    return {"schema": "srw64.reading-indicators-check.v1", "frames": len(rows),
            "scope": "4:3 completed GPU samples; speed steps, speaker triangle and progress fill; history occlusion excluded",
            "checked": counts, "levels": sorted(levels), "slots": sorted(slots),
            "failures": failures, "progress_states": progress_states}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    result = verify(args.directory)
    (args.directory / "reading-indicators.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k: result[k] for k in ("frames", "checked", "levels", "slots")}, indent=2))
    print(json.dumps({"failures": result["failures"][:10], "failure_count": len(result["failures"])}, indent=2))
    if result["failures"] or not result["checked"]["progress"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
