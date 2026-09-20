#!/usr/bin/env python3
"""Close the actual game window at a selected VI and record the outcome.

Start run_host_probe with SRW64_WINDOW_CONTROL=1. This injects the SDL window-close
event; it does not synthesize SDL_QUIT, kill the process, or treat exit 0 as
proof that every guest thread was reclaimed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.run.verification_support import shutdown_observation, shutdown_verified, wait_for, wait_for_report, write_control


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=Path)
    parser.add_argument("--at-vi", required=True, type=int)
    args = parser.parse_args()
    run = args.run.resolve()
    state = wait_for(lambda: json.loads((run / "live-state.json").read_text()), lambda _: True, 60)
    if state["vi"] >= args.at_vi:
        raise RuntimeError("Selected close VI already passed")
    write_control(run / "window-close.txt", f"SRWX1 1 {args.at_vi}\n")
    report = wait_for_report(run, (args.at_vi - state["vi"]) / 60 + 60)
    events = [json.loads(line) for line in (run / "window-close-events.jsonl").read_text().splitlines()]
    log = Path(report["native_log_path"]).read_text()
    assert events[-1]["action"] == "SDL_WINDOWEVENT_CLOSE"
    assert "SRW64_WINDOW_QUIT event=512" in log
    assert report["audio_output_enabled"] is False
    result = {"schema": "srw64.window-close-verification.v1", "exit_code": report.get("exit_code"),
              "actual_close": events[-1], "native_name_entry": report["native_name_entry"]["enabled"],
              "guest_threads_after_rdram_free": shutdown_observation(log)["after_rdram_free"],
              "shutdown_lifecycle_verified": report.get("exit_code") == 0 and shutdown_verified(log),
              "report_sha256": hashlib.sha256((run / "report.json").read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "support_sha256": hashlib.sha256((Path(__file__).resolve().parents[1] / "run" / "verification_support.py").read_bytes()).hexdigest()}
    (run / "window-close-verification.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))
    return 0 if report.get("exit_code") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
