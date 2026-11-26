#!/usr/bin/env python3
"""The map's HP window with the battle animation off, in a widened picture (2026-11-26).

The window (801E3AE4) opens beside the target on the centred 320 and its gauge is drawn at
render slot 0x2E's x there too; the damage figure starts from that x but is drawn in the
widened map view (docs/design/deck-16x10.md §2, tests/test_map_hp_window.py). Since 11-04 the
gauge sat half the extra width right of its window on every renderer (a player saw it under
Windows' GL renderer).

Launches the battle-ui mini stage with HD images in a 1366 x 768 window (--window W H), turns
the battle animation off, ends the player's phase and confirms each enemy attack. For every
round of the 0x4B presentation it checks, in picture pixels of the original's scale:
  window-on-target  the window's left edge on the target unit's x (unless the original clamps it)
  figure-in-window  the figure's first cell 9 to the right of the window's left edge
  gauge-in-window   the gauge's fill inside the window's 47 pixels in every recorded frame it is in
                    (the presentation is recorded frame by frame, lockstep: a small hit drains
                    and closes the window within a frame or two, too fast for a screenshot)
--aspect 4:3 runs the original's width, --renderer gl the GL renderer (SRW64_RENDERER=gl).
Writes map-hp-window.json and each round's recording into the run directory; exits 1 on a failure."""
import argparse
import shutil
import subprocess
import json
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
ANIMATION_FLAGS, ANIMATION_OFF = 0x8015DDA8, 4
MAP_STATE, MAP_SUB = 0x80172EB0, 0x80172EB2
TARGET, CELLS, SCROLL_X = 0x80227BC6, 0x800FFA74, 0x8010F5D4
GAUGE_X, GAUGE_Y, FIGURE_X = 0x80101DAC, 0x80101DB0, 0x80228548
WINDOW_WIDTH = 47

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--window", nargs=2, type=int, default=[1366, 768])
parser.add_argument("--aspect", choices=["auto", "4:3"], default="auto")
parser.add_argument("--renderer", choices=["rt64", "gl"], default="rt64")
parser.add_argument("--rounds", type=int, default=2, help="rounds to check before stopping")
args = parser.parse_args()

env = {"SRW64_ASPECT": args.aspect}
if args.renderer == "gl":
    env["SRW64_RENDERER"] = "gl"
s = Session.launch(language="zh-Hans", images="hd", mini_stage=str(ROOT / "config/recomp/mini-stages/battle-ui.json"), env=env)
print("RUN", s.run, flush=True)
results = {"window": args.window, "aspect": args.aspect, "renderer": args.renderer, "rounds": [], "failures": []}


def memory(address, size):
    return bytes.fromhex(s.client.call("memory.read", address=address, size=size)["hex"])


def u8(address): return memory(address, 1)[0]
def u16(address): return struct.unpack(">H", memory(address, 2))[0]
def s32(address): return struct.unpack(">i", memory(address, 4))[0]
def f32(address): return struct.unpack(">f", memory(address, 4))[0]


def save():
    (s.run / "map-hp-window.json").write_text(json.dumps(results, ensure_ascii=False, indent=2))


def check(round_, name, passed, **detail):
    round_["checks"].append({"check": name, "passed": bool(passed), **detail})
    if not passed:
        results["failures"].append({"round": round_["index"], "check": name, **detail})
    print(f"  {name}: {'PASS' if passed else 'FAIL'} {detail}", flush=True)
    save()


def gauge_columns(path, top, bottom, scale):
    """The longest run of the gauge's flat green and red fill in rows top..bottom (pixels):
    its left and right edge in pixels, or None."""
    from PIL import Image
    picture = Image.open(path).convert("RGB")
    width, height = picture.size
    pixels = picture.load()
    best = None
    for y in range(max(0, int(top)), min(height, int(bottom))):
        run_start = None
        for x in range(width + 1):
            r, g, b = pixels[x, y] if x < width else (0, 0, 0)
            fill = (g > 150 and r < 90 and b < 90) or (r > 150 and g < 70 and b < 70)
            if fill and run_start is None:
                run_start = x
            elif not fill and run_start is not None:
                if x - run_start >= 20 * scale and (best is None or x - run_start > best[1] - best[0]):
                    best = (run_start, x)
                run_start = None
    return best


s.client.call("window", width=args.window[0], height=args.window[1])
s.enter_mini_stage()
time.sleep(4)
s.client.call("memory.write", address=ANIMATION_FLAGS, hex=f"{u8(ANIMATION_FLAGS) | ANIMATION_OFF:02x}")
# The cursor starts on a unit; the cell above is empty: the phase menu, end the phase, yes.
for key in ["up", "z", "z", "z"]:
    s.client.call("keys", press=key, hold_ms=100)
    time.sleep(0.35)

RECORD_WIDTH = 1366
deadline = time.monotonic() + 240
armed = True   # a round is recorded once, from the frame its window opens (sub-state 4)
while len(results["rounds"]) < args.rounds and time.monotonic() < deadline:
    status = s.client.call("status")
    if status["battle_page"].get("visible"):
        s.client.call("ui.click", id="battle-confirm")
        time.sleep(0.5)
        continue
    state, sub = u8(MAP_STATE), u8(MAP_SUB)
    if state != 0x4B or sub not in (3, 4, 5):
        armed = True
        time.sleep(0.02)
        continue
    if not armed:
        time.sleep(0.02)
        continue
    armed = False
    index = len(results["rounds"])
    s.record_start(width=RECORD_WIDTH, fps=60, lockstep=True)
    snapshot = None
    while time.monotonic() < deadline:
        state, sub = u8(MAP_STATE), u8(MAP_SUB)
        if state != 0x4B or sub >= 6:
            break
        if sub == 5 and snapshot is None:
            status = s.client.call("status")
            target = u16(TARGET)
            snapshot = {"status": status, "target": target, "unit_x": f32(CELLS + target * 0xC4),
                        "scroll": s32(SCROLL_X), "gauge_x": f32(GAUGE_X), "gauge_y": f32(GAUGE_Y)}
        time.sleep(0.01)
    time.sleep(0.2)   # the window's last frames
    video = s.run / f"hp-window-{index}.mp4"
    s.record_stop(video, fps=60)
    if snapshot is None:   # a miss: no window (map_miss.hpp places only the figure)
        continue
    status = snapshot["status"]
    extra, picture = status["wide_map"]["extra"], status["picture_width"]
    view_left = (picture - (320 + extra)) / 2
    gauge_x, gauge_y = snapshot["gauge_x"], snapshot["gauge_y"]
    anchor = gauge_x - 17
    anchor_picture = anchor + (picture - 320) / 2          # the window is on the centred 320
    unit_picture = view_left + snapshot["unit_x"] + snapshot["scroll"]   # the unit is in the widened view
    round_ = {"index": index, "target": snapshot["target"], "extra": extra, "picture_width": picture,
              "unit_x": snapshot["unit_x"], "scroll": snapshot["scroll"], "gauge_x": gauge_x, "gauge_y": gauge_y,
              "recording": video.name, "checks": []}
    results["rounds"].append(round_)
    print(f"round {index}: target {snapshot['target']:#x} extra {extra} gauge {gauge_x}", flush=True)
    clamped = anchor in (0, 240)
    check(round_, "window-on-target", clamped or abs(anchor_picture - unit_picture) <= 1,
          window_left=anchor_picture, unit=unit_picture, clamped=clamped)
    # 801FCC6C seeds the figure's cells in sub-state 5: the first at the x it is given - 8.
    figure_picture = view_left + s32(FIGURE_X)
    check(round_, "figure-in-window", abs(figure_picture - anchor_picture - 9) <= 1,
          figure=figure_picture, window_left=anchor_picture)
    frames = s.run / f"hp-window-{index}"
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir()
    subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(video), str(frames / "%04d.png")], check=True)
    window = status["window"]
    scale = RECORD_WIDTH / window["pixel_width"] * window["pixel_height"] / 240
    left_edge = (RECORD_WIDTH - picture * scale) / 2
    seen, outside = 0, []
    for frame in sorted(frames.glob("*.png")):
        found = gauge_columns(frame, (gauge_y - 14) * scale, (gauge_y + 2) * scale, scale)
        if found is None:
            continue
        seen += 1
        gauge_left, gauge_right = (found[0] - left_edge) / scale, (found[1] - left_edge) / scale
        if not (anchor_picture - 1 <= gauge_left and gauge_right <= anchor_picture + WINDOW_WIDTH + 1):
            outside.append({"frame": frame.name, "gauge": [round(gauge_left, 1), round(gauge_right, 1)]})
    shutil.rmtree(frames, ignore_errors=True)
    check(round_, "gauge-in-window", seen and not outside, frames_with_gauge=seen, outside=outside[:5],
          window=[anchor_picture, anchor_picture + WINDOW_WIDTH])

if len(results["rounds"]) < args.rounds:
    results["failures"].append({"check": "rounds", "seen": len(results["rounds"]), "wanted": args.rounds})
save()
print("FAILED" if results["failures"] else "PASS", json.dumps(results["failures"], ensure_ascii=False), flush=True)
s.quit()
sys.exit(1 if results["failures"] else 0)
