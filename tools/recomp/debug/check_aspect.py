#!/usr/bin/env python3
"""The picture that follows the screen beside the original 4:3 (docs/design/deck-16x10.md).

Launches the battle-ui mini stage with HD images in a 1280 x 800 window (--window W H
for another) and screenshots the title menu, the tactical map, the pre-battle page and
the battle animation, each with the aspect auto and 4:3, switching it while the game
runs (debug `settings aspect`). Then the map in a 960 x 720 window. Screenshots and
aspect-checks.json land in the run directory; nothing here judges the pictures.

--fixed auto or --fixed 4:3: the whole run at one aspect (SRW64_ASPECT), no switching,
the same screenshots under one name each, to tell a switch's effects from the aspect's.

--stage NAME (with --fixed): another mini stage, entered and screenshot every 3 s for
45 s from the click (stage-NN.png), e.g. worldmap-models for the world map's models, act
for a chapter title card; --stage intro starts a new game for the zooming prologue text
instead. --every S --count N change the screenshots' spacing and number."""
import json
import os
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

ROOT = Path(__file__).resolve().parents[3]
fixed = sys.argv[sys.argv.index("--fixed") + 1] if "--fixed" in sys.argv else None
window = [int(v) for v in sys.argv[sys.argv.index("--window") + 1:][:2]] if "--window" in sys.argv else [1280, 800]
stage = sys.argv[sys.argv.index("--stage") + 1] if "--stage" in sys.argv else "battle-ui"
every = float(sys.argv[sys.argv.index("--every") + 1]) if "--every" in sys.argv else 3.0
count = int(sys.argv[sys.argv.index("--count") + 1]) if "--count" in sys.argv else 15
binary = os.environ.get("SRW64_HOST_BINARY") or str(ROOT / "build/recomp/gfx-build/srw64-gfx-host")
s = Session.launch(language="zh-Hans", images="hd", diagnostics="light",
                   binary=binary if Path(binary).exists() else None,
                   mini_stage=str(ROOT / f"config/recomp/mini-stages/{'battle-ui' if stage == 'intro' else stage}.json"),
                   env={"SRW64_ASPECT": fixed} if fixed else None)
print("RUN", s.run, flush=True)
checks = []


def status():
    return s.client.call("status")


def shot(name):
    s.client.call("screenshot", path=str(s.run / f"{name}.png"))
    print("shot", name, flush=True)


def aspect(value):
    if fixed:
        return
    s.client.call("settings", aspect=value)
    time.sleep(0.6)
    checks.append({"aspect": value, "status": status().get("aspect")})


def both(name):
    """The same moment at auto and at 4:3, back to auto afterwards."""
    if fixed:
        shot(f"{name}-{fixed.replace(':', 'x')}")
        return
    shot(f"{name}-auto")
    aspect("4:3")
    shot(f"{name}-4x3")
    aspect("auto")


s.client.call("window", width=window[0], height=window[1])
s.wait(vi=600, timeout=300)
for _ in range(30):
    if status()["intro"].get("title_major") == 3:
        break
    s.client.call("keys", press="return", hold_ms=100)
    time.sleep(0.5)
time.sleep(2.5)
both("title")
if stage != "battle-ui":
    if stage == "intro":
        s.client.call("keys", press="return", hold_ms=100)   # スタート faces the front
    else:
        s.client.call("ui.click", id="mini-enter")
    for n in range(count):
        time.sleep(every)
        shot(f"stage-{n:02}")
    s.quit()
    print("DONE", s.run, flush=True)
    raise SystemExit(0)
s.enter_mini_stage()
time.sleep(4)
both("map")
# The enemy attacks: end the phase from the empty cell above the player unit.
for key in ["up", "z", "z", "z"]:
    s.client.call("keys", press=key, hold_ms=100)
    time.sleep(0.8)
end = time.monotonic() + 120
while not status()["battle_page"].get("visible"):
    if time.monotonic() > end:
        raise SystemExit("no battle page")
    time.sleep(0.2)
time.sleep(1)
both("prebattle")
page = status()["battle_page"]
if not page.get("animation", False):
    s.client.call("ui.click", id="battle-animation")
    time.sleep(0.3)
s.client.call("ui.click", id="battle-confirm")
for n, delay in enumerate([2.0, 2.0, 2.5]):
    time.sleep(delay)
    both(f"battle-{n}")
time.sleep(20)
shot("after-battle")
s.client.call("window", width=960, height=720)
time.sleep(1.2)
shot("window-960x720-auto")
aspect("4:3")
shot("window-960x720-4x3")
aspect("auto")
s.client.call("window", width=window[0], height=window[1])
(s.run / "aspect-checks.json").write_text(json.dumps(checks, ensure_ascii=False, indent=2) + "\n")
s.quit()
print("DONE", s.run, flush=True)
