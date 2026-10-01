#!/usr/bin/env python3
"""The tactical map in the widened view (docs/design/deck-16x10.md §5).

Launches a mini stage (battle-ui by default: the stage-one map with units) with HD images
in a 1280 x 800 window (--window W H for another shape) and screenshots the map as it
opens, with a unit's move range, then with the cursor pushed to the right, bottom and left
edges, with the terrain panel open at the right and left; `status.wide_map` says how much wider than 320 the view is. --map N looks at map N
through a unit-less viewer stage instead (map 34 is one of the 320 x 240 maps, which keep
the original's width, centred). --aspect 4:3 for the same run at the original's width.
Screenshots and wide-map.json land in the run directory; nothing here judges the pictures."""
import json
import os
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
args = sys.argv[1:]
option = lambda name, count=1: args[args.index(name) + 1:args.index(name) + 1 + count] if name in args else None
window = [int(v) for v in option("--window", 2) or [1280, 800]]
aspect = (option("--aspect") or ["auto"])[0]
map_number = (option("--map") or [None])[0]
if map_number:
    stage = json.loads((ROOT / "config/recomp/mini-stages/map20-view.json").read_text())
    stage["map"], stage["name"] = int(map_number), f"map{map_number}-view"
    stage_path = Path(os.environ.get("TMPDIR", "/tmp")) / f"map{map_number}-view.json"
    stage_path.write_text(json.dumps(stage))
else:
    stage_path = ROOT / "config/recomp/mini-stages/battle-ui.json"
binary = os.environ.get("SRW64_HOST_BINARY") or str(ROOT / "build/recomp/gfx-build/srw64-gfx-host")
s = Session.launch(language="zh-Hans", images="hd", binary=binary if Path(binary).exists() else None,
                   mini_stage=str(stage_path), env={"SRW64_ASPECT": aspect})
print("RUN", s.run, flush=True)
states = {}


def shot(name):
    s.client.call("screenshot", path=str(s.run / f"{name}.png"))
    status = s.client.call("status")
    states[name] = {"wide_map": status.get("wide_map"), "picture_width": status.get("picture_width"),
                    "window": status.get("window")}
    print("shot", name, states[name]["wide_map"], flush=True)


def key(name, hold=0.1, wait=0.8):
    s.client.call("keys", press=name, hold_ms=int(hold * 1000))
    time.sleep(wait)


def hold(direction, seconds):
    s.client.call("keys", down=direction)
    time.sleep(seconds)
    s.client.call("keys", release_all=True)
    time.sleep(1.2)


s.client.call("window", width=window[0], height=window[1])
s.enter_mini_stage()
time.sleep(4)
shot("open")
if not map_number:
    # The cursor starts on a player unit: its menu's first item shows the move range.
    key("z")
    key("z", wait=1.2)
    shot("move-range")
    key("x")
    key("x")
hold("right", 5)
shot("right")
# The terrain panel (B on an empty cell) opens beside the cursor at either edge.
key("x", wait=1.0)
shot("panel-right")
key("x")
hold("down", 4)
shot("bottom-right")
hold("left", 7)
shot("bottom-left")
key("x", wait=1.0)
shot("panel-left")
key("x")
hold("up", 5)
shot("top-left")
(s.run / "wide-map.json").write_text(json.dumps(states, ensure_ascii=False, indent=2) + "\n")
s.quit()
print("DONE", s.run, flush=True)
