#!/usr/bin/env python3
"""Look at the HD terrain panel (docs/design/tactical-map-hd-kit.md §4).

Launches the move-jump mini stage (map 20, units present so the terrain panel shows) with
the HD map bundle, walks the cursor over a few cells and screenshots the panel in HD and,
at the same cell, with the original images. hd-map-summary.json reports panel_draws."""
import json
import os
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
# The maps come with the HD art pack (content/art/stage1-hd.json); SRW64_HD_MAPS, when set
# in the environment, points at another folder instead.
binary = os.environ.get("SRW64_HOST_BINARY")
# Optional argument: another mini stage (e.g. map87-colony-view); then the window is opened
# where the stage leaves the cursor and shot twice, to see the colony overlay animate in it.
stage = sys.argv[1] if len(sys.argv) > 1 else "move-jump"
s = Session.launch(language="zh-Hans", images="hd", binary=binary if binary and Path(binary).exists() else None,
                   mini_stage=str(ROOT / f"config/recomp/mini-stages/{stage}.json"))
print("RUN", s.run, flush=True)


def shot(name):
    s.client.call("screenshot", path=str(s.run / f"{name}.png"))
    print("shot", name, flush=True)


def press(key, times=1, pause=0.4):
    for _ in range(times):
        s.client.call("keys", press=key, hold_ms=80)
        time.sleep(pause)


s.enter_mini_stage()
time.sleep(4)
if stage != "move-jump":
    press("x"); time.sleep(1.0)
    shot("panel-hd-a")
    time.sleep(1.6)
    shot("panel-hd-a-later")
    press("f6"); time.sleep(1.2)
    shot("panel-original-a")
    press("f6"); time.sleep(0.5)
else:
    shot("panel-unit")                      # cursor on the unit at 8,8
    press("right", 3)
    time.sleep(0.8)
    press("x"); time.sleep(1.0)             # B on an empty cell opens the terrain window (801C8B04 -> 801CABAC)
    shot("panel-hd-a")                      # cell 11,8
    press("f6"); time.sleep(1.2)
    shot("panel-original-a")
    press("f6"); time.sleep(0.8)
    press("x"); time.sleep(0.6)             # close it
    press("down", 4); press("right", 2)
    time.sleep(0.8)
    press("x"); time.sleep(1.0)
    shot("panel-hd-b")                      # cell 13,12
    press("f6"); time.sleep(1.2)
    shot("panel-original-b")
    press("f6"); time.sleep(0.8)
    press("x"); time.sleep(0.5)
s.quit()
summary = s.run / "hd-map-summary.json"
print("SUMMARY", summary.read_text() if summary.exists() else "missing", flush=True)
