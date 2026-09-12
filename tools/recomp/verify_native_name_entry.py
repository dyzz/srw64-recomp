#!/usr/bin/env python3
"""Exercise the live Cocoa field and guest writeback in an isolated, muted run.

Start run_host_probe with SRW64_NAME_ENTRY_CONTROL=1, the play profile, and
config/recomp/native-name-entry.json, then run this with --run RUN_DIRECTORY.
The --exit-mode window path additionally requires SRW64_WINDOW_CONTROL=1.
This uses the actual field editor/button actions; human IME composition and
SRAM reload are separate acceptance checks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

from verification_support import shutdown_observation, shutdown_verified, wait_for, wait_for_report, write_control


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--run", required=True, type=Path)
    p.add_argument("--exit-mode", choices=("control", "window"), default="control")
    args = p.parse_args()
    run = args.run.resolve()
    sequence = 0
    def events():
        return [json.loads(s) for s in (run / "name-entry-events.jsonl").read_text().splitlines()]
    def ui():
        return json.loads((run / "name-entry-ui.json").read_text())
    def command(serial, field, action, text="", **extra):
        nonlocal sequence
        sequence += 1
        payload = {"schema": "srw64.name-entry-control.v1", "sequence": sequence,
                   "serial": serial, "field": field, "action": action, "text": text, **extra}
        write_control(run / "name-entry-control.json", json.dumps(payload, ensure_ascii=False))
        return wait_for(ui, lambda value: value["sequence"] == sequence)
    def opened(serial):
        return wait_for(events, lambda rows: any(r["serial"] == serial and r["kind"] == "open" for r in rows), 60)
    opened(1)
    first = command(1, 0, "snapshot")
    assert first["text"] == "マナミ"
    assert first["embedded"] and first["child_windows"] == 0
    def screenshot(name, state):
        time.sleep(.15)
        subprocess.run(["screencapture", "-x", "-l", str(state["window_id"]), str(run / name)], check=True)
    screenshot("page-player-window.png", first)
    assert command(1, 0, "key", "z", key_code=6)["text"] == "z"
    command(1, 0, "default")
    assert command(1, 0, "tab")["field"] == 1
    assert command(1, 1, "shift-tab")["field"] == 0
    small = command(1, 0, "resize", width=800, height=600)
    small = command(1, 0, "snapshot")
    assert small["width"] == 800 and small["height"] == 600
    screenshot("page-small-window.png", small)
    command(1, 0, "resize", width=1200, height=800)
    screenshot("page-wide-window.png", command(1, 0, "snapshot"))
    for value in ("", "🙂", "アアアアアアアア"):
        command(1, 0, "insert", value)
        result = command(1, 0, "next")
        assert result["field"] == 0 and result["error"]
    command(1, 0, "default")
    assert command(1, 0, "snapshot")["text"] == "マナミ"
    for field, value in enumerate(("光", "ミナト", "ヒカリ")):
        command(1, field, "insert", value)
        if field == 2:
            command(1, field, "back")
            assert command(1, 1, "snapshot")["text"] == "ミナト"
            command(1, 1, "next")
        command(1, field, "next")
    opened(2)
    for field, value in enumerate(("光", "ミナト", "アイシャ")):
        command(2, field, "insert", value)
        command(2, field, "next")
    wait_for(events, lambda rows: rows[-1]["kind"] == "rejected")
    wait_for(lambda: command(2, 2, "snapshot"), lambda value: bool(value["error"]))
    command(2, 2, "cancel")
    def buttons(sequence, mask, duration=3):
        (run / "control.txt").write_text(f"SRWC1 {sequence} {mask} {duration}\n")
    time.sleep(1.5)
    buttons(1, 0x8000);time.sleep(1.5)
    buttons(2, 0x8000)
    rows = opened(3)
    assert rows[-1]["values"] == ["光", "ミナト", "ヒカリ"]
    for field in range(3):
        command(3, field, "next")
    opened(4)
    for field, value in enumerate(("サクラ", "ハルノ", "サクラ")):
        command(4, field, "insert", value)
        command(4, field, "next")
    wait_for(events, lambda rows: rows[-1]["kind"] == "committed")
    opened(5)
    review = command(5, 0, "snapshot")
    assert review["person"] == 2 and review["embedded"] and review["child_windows"] == 0
    screenshot("page-review-window.png", review)
    command(5, 0, "key", "\x1b", key_code=53) # Escape on the modern review.
    rows = opened(6)
    assert rows[-1]["values"] == ["光", "ミナト", "ヒカリ"]
    command(6, 0, "focus", index=2)
    command(6, 2, "commit")
    rows = opened(7)
    assert rows[-1]["values"] == ["サクラ", "ハルノ", "サクラ"]
    command(7, 0, "commit")
    opened(8)
    command(8, 0, "key", "\r", key_code=36)
    time.sleep(4);buttons(4, 0x1010, 180)
    time.sleep(5);buttons(5, 0x800, 3)
    def dialogue():
        return [json.loads(s) for s in (run / "dialogue-events.jsonl").read_text().splitlines()]
    rows = wait_for(dialogue, lambda rows: any(r.get("speaker") == "ヒカリ" for r in rows), 30)
    wait_for(lambda: json.loads((run / "dialogue-raster.json").read_text()),
             lambda value: value["drawable"] == [1200, 800])
    # Give the GPU and periodic RAM snapshot time to catch the completed text.
    time.sleep(2)
    screenshot("page-exited-to-story-window.png", review)
    ram = (run / "latest-gfx-rdram.bin").read_bytes()
    assert len(ram) == 0x800000
    glyphs = json.loads((run.parent / (run.name + ".content/dialogue.json")).read_text())["glyphs"]
    bank = {}
    for key, address, limit, expected in (
        ("given", 0x10F5F8, 8, "光"), ("family", 0x10F618, 8, "ミナト"),
        ("nickname", 0x10F638, 6, "ヒカリ"), ("full", 0x10F650, 18, "光・ミナト"),
        ("partner_given", 0x10F608, 8, "サクラ"), ("partner_family", 0x10F628, 8, "ハルノ"),
        ("partner_nickname", 0x10F644, 6, "サクラ"), ("partner_full", 0x10F674, 18, "サクラ・ハルノ")):
        codes = []
        for i in range(limit):
            code = struct.unpack_from(">H", ram, address + 2*i)[0]
            if code == 0xFFFF:
                break
            codes.append(code)
        else:
            raise AssertionError(f"Unterminated name: {key}")
        decoded = "".join(glyphs[str(code)] for code in codes)
        assert decoded == expected, (key, decoded)
        bank[key] = {"address": hex(address), "glyphs": codes, "text": decoded}
    (run / "names-readback.json").write_text(json.dumps(bank, ensure_ascii=False, indent=2) + "\n")
    if args.exit_mode == "window":
        write_control(run / "window-close.txt", "SRWX1 1 0\n")
    else:
        write_control(run / "control.txt", "SRWQ1 6 0 0\n")
    report = wait_for_report(run)
    expected_status = "native-run-ended-before-VI-limit" if args.exit_mode == "window" else "native-run-ended-by-control"
    passed = report.get("exit_code") == 0 and report["status"] == expected_status and report["audio_output_enabled"] is False
    if args.exit_mode == "window":
        close_event = json.loads((run / "window-close-events.jsonl").read_text().splitlines()[-1])
        assert close_event["action"] == "NSWindow.performClose"
    result = {"schema": "srw64.native-name-acceptance.v1", "status": "passed" if passed else "exit-failed",
              "exit_mode": args.exit_mode, "exit_code": report.get("exit_code"),
              "run_report_sha256": hashlib.sha256((run / "report.json").read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "support_sha256": hashlib.sha256(Path(__file__).with_name("verification_support.py").read_bytes()).hexdigest(),
              "checks": ["embedded game-window page without child windows", "all three fields and live preview",
                         "native Tab/Shift-Tab field navigation", "800x600 and 1200x800 resize",
                         "window key dispatch: typing Z and review Escape/Return",
                         "native-field insertion", "empty/unsupported/length rejection", "back/default",
                         "original duplicate-name validation", "cancel", "reopen virtual character records",
                         "modern two-person review and re-edit roundtrip",
                         "post-resize Metal framebuffer and dialogue raster are 1200x800",
                         "two character name banks and full-name delimiter", "custom name in native dialogue", "audio output disabled"] +
                         ([args.exit_mode + " process exit code 0"] if passed else []),
              "shutdown_observation": shutdown_observation(Path(report["native_log_path"]).read_text()),
              "shutdown_lifecycle_verified": report.get("exit_code") == 0 and shutdown_verified(Path(report["native_log_path"]).read_text()),
              "not_exercised": ["human IME composition/clipboard shortcuts", "SRAM save/reload", "arbitrary Unicode outside the original font"]}
    (run / "name-entry-acceptance.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
