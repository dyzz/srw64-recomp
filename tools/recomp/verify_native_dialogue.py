#!/usr/bin/env python3
"""Exercise native dialogue through controller input and completed GPU captures.

Run against a fresh JP profile host with the original name entry and opt-in
SRW64_WINDOW_CONTROL=1. No guest RAM, event flags, or saves are edited.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--resume-final-checks", action="store_true",
                        help="retain captured layout checks and repeat fast-release / skip / first-map checks")
    args = parser.parse_args()
    output = args.output.resolve()
    evidence = output / "ui-checks"
    deadline = time.monotonic() + 60
    while not output.is_dir() and time.monotonic() < deadline:
        time.sleep(.1)
    evidence.mkdir(exist_ok=True)
    results = json.loads((evidence / "progress.json").read_text()) if args.resume_final_checks else []

    def read(name):
        try:
            return json.loads((output / name).read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def state():
        return read("dialogue-state.json")

    def wait(predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            if (output / "native-counters.json").exists():
                raise RuntimeError("Host exited before verification completed")
            time.sleep(.025)
        raise TimeoutError(f"Native UI condition timed out: {state()}")

    def vi_wait(ticks):
        vi = max(read("live-state.json")["vi"], state().get("vi", 0))
        wait(lambda: max(read("live-state.json").get("vi", 0), state().get("vi", 0)) >= vi + ticks,
             ticks / 60 + 15)

    def pulse(*buttons, duration=6):
        process = subprocess.run([sys.executable, str(Path(__file__).with_name("control_host.py")),
                                  str(output), "--buttons", *buttons, "--duration", str(duration)],
                                 check=True, capture_output=True, text=True)
        applied = json.loads(process.stdout)["applied"]["vi"]
        wait(lambda: max(read("live-state.json").get("vi", 0), state().get("vi", 0)) >= applied + duration + 8)

    def active():
        return next((b for b in state().get("boxes", []) if b["active"]), {})

    def capture(label):
        # live-state is sampled only once per 60 VI and can trail an already
        # captured frame. Use dialogue/present clocks to require a NEW frame.
        minimum = max(read("live-state.json")["vi"], state().get("vi", 0),
                      read("dialogue-present.json").get("native_vi", 0)) + 8
        expected_size = read("dialogue-raster.json").get("drawable") if state().get("active") else None

        def latest():
            for path in sorted(output.glob("present-*.json"),
                               key=lambda p: int(p.stem.split("-")[-1]), reverse=True):
                try:
                    meta = json.loads(path.read_text())
                except json.JSONDecodeError:
                    continue
                if (meta["native_vi_at_draw"] >= minimum and path.with_suffix(".png").exists()
                        and (expected_size is None or [meta["width"], meta["height"]] == expected_size)):
                    return path
                break
            return None

        source = wait(latest)
        target = evidence / f"{label}.png"
        shutil.copyfile(source.with_suffix(".png"), target)
        entry = {"check": label, "state": state(), "GPU": json.loads(source.read_text()),
                 "image": str(target), "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}
        results.append(entry)
        print(json.dumps({"passed": label, "vi": entry["GPU"]["native_vi_at_draw"]}), flush=True)
        (evidence / "progress.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")

    if not args.resume_final_checks:
        wait(lambda: read("live-state.json").get("vi", 0) > 7080 and
             active().get("text_id") == 17412 and active().get("segment") in (0, 1), 150)
        if active().get("segment") == 0:
            pulse("a")
        wait(lambda: active().get("text_id") == 17412 and active().get("segment") == 1)
        event = state()["event"]
        vi_wait(100)
        capture("reference-13")

        for _ in range(5):
            pulse("c_up")
        assert state()["font_size"] == 18 and state()["pages"] > 1 and state()["event"] == event
        capture("font-18-page-1")
        pulse("a")
        assert state()["event"] == event and state()["page"] == 1
        capture("font-18-page-2")

        pulse("l")
        frozen = state()
        vi_wait(180)
        assert state()["history_open"] and state()["event"] == event
        assert state()["page"] == frozen["page"] and state()["revealed_utf16"] == frozen["revealed_utf16"]
        capture("history")
        pulse("up", duration=24)
        assert state()["history_offset"] > 0
        capture("history-scrolled")
        pulse("a")
        assert not state()["history_open"] and state()["event"] == event and state()["page"] == 1
        for _ in range(5):
            pulse("c_down")
        assert state()["font_size"] == 13 and state()["pages"] == 1

        pulse("up", duration=24)
        assert state()["automatic"] and state()["speed"] >= 2
        pulse("down", duration=54)
        assert not state()["automatic"] and state()["speed"] == 0
        capture("speed-back-to-manual")

        for width, height in ((1280, 960), (1280, 720), (960, 720)):
            temporary = output / "window-control.tmp"
            temporary.write_text(f"SRWW1 {time.time_ns()} {width} {height}\n")
            temporary.replace(output / "window-control.txt")
            wait(lambda: read("dialogue-raster.json").get("drawable") == [width, height])
            capture(f"resize-{width}x{height}")

    before = state()["event"]
    pulse("r", "a", duration=24)
    after = state()["event"]
    assert after > before and not state()["automatic"]
    vi_wait(160)
    assert state()["event"] == after
    capture("fast-released")

    pulse("r", "start")
    wait(lambda: any(json.loads(line).get("kind") == "skip_start" for line in
                     (output / "dialogue-events.jsonl").read_text().splitlines()))
    wait(lambda: not state().get("skipping", True), 60)
    vi_wait(180)
    assert not state()["skipping"] and not state()["automatic"]
    capture("skip-boundary")
    events = [json.loads(line) for line in (output / "dialogue-events.jsonl").read_text().splitlines()]
    start = max(i for i, e in enumerate(events) if e["kind"] == "skip_start")
    boundaries = [e for e in events[start:] if e["kind"] == "boundary"]
    assert boundaries, "Skip did not reach a script/choice/overlay boundary"
    # The opening skip must stop before the next map's first dialogue. That
    # dialogue should use the same native UI and remain in manual mode.
    wait(lambda: state().get("active") and active().get("text_id") == 17460, 60)
    stopped = state()["event"]
    vi_wait(180)
    assert state()["event"] == stopped and not state()["skipping"] and not state()["automatic"]
    capture("first-map-dialogue")
    (evidence / "acceptance.json").write_text(json.dumps(
        {"schema": "srw64.native-dialogue-acceptance.v1", "status": "passed",
         "checks": results, "skip_boundaries": boundaries}, ensure_ascii=False, indent=2) + "\n")
    print("Native dialogue input, history, resize, fast-release and skip-boundary checks passed.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
