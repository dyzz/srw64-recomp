#!/usr/bin/env python3
"""The インターミッション in a picture wider than 4:3 (docs/design/deck-16x10.md).

Loads a stage-one-clear save through the title ring with HD images and screenshots the
title's ロード page, the main menu and the ユニット改造 list in a 16:10 (1280 x 800), a 16:9 (1280 x 720) and a
4:3 (960 x 720) window: the pages stay on the original's 320 x 240, the background
fills the picture. --aspect 4:3 for the same run at the original's width. Screenshots land
in the run directory; nothing here judges the pictures."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
args = sys.argv[1:]
aspect = args[args.index("--aspect") + 1] if "--aspect" in args else "auto"
save = ROOT / "build/recomp/save-recovery-check/intermission-cold-1.source.sram"
binary = ROOT / "build/recomp/gfx-build/srw64-gfx-host"
s = Session.launch(language="zh-Hans", images="hd", save=str(save), diagnostics="light",
                   binary=str(binary) if binary.exists() else None, env={"SRW64_ASPECT": aspect})
print("RUN", s.run, flush=True)


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{"press": name}])
        time.sleep(pause)


def page():
    return s.client.call("status")["intermission_page"]


s.client.call("window", width=1280, height=800)
s.wait(vi=600, timeout=300)
for _ in range(8):
    keys("return", pause=.5)
    try:
        s.wait(title_major=3, timeout=4)
        break
    except Exception:
        pass
time.sleep(2.5)
keys("right", "right", "right", pause=1.5)
keys("return", pause=3)
s.client.call("screenshot", path=str(s.run / "load-1280x800.png"))   # the title's ロード page
for _ in range(8):
    status = s.client.call("status")
    if status["intermission_page"].get("visible") or (status.get("intro") or {}).get("title_major") != 7:
        break
    keys("z", pause=1.5)
end = time.monotonic() + 60
while not page().get("visible"):
    if time.monotonic() > end:
        raise SystemExit("no intermission menu")
    time.sleep(.2)
time.sleep(2)
for width, height in ((1280, 800), (1280, 720), (960, 720)):
    s.client.call("window", width=width, height=height)
    time.sleep(1.5)
    s.client.call("screenshot", path=str(s.run / f"menu-{width}x{height}.png"))
    print("shot menu", width, height, flush=True)
s.client.call("window", width=1280, height=720)
time.sleep(1)
s.client.call("ui.click", id="intermission:1")
time.sleep(1.5)
s.client.call("ui.click", id="intermission:1")
time.sleep(2.5)
s.client.call("screenshot", path=str(s.run / "upgrade-1280x720.png"))
print("shot upgrade", flush=True)
s.quit()
print("DONE", s.run, flush=True)
