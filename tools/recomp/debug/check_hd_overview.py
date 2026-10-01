#!/usr/bin/env python3
"""Look at the HD map overview (C-right on the idle map; the map drawer's 800943E0 scaling).

Launches the move-jump mini stage with the HD bundle, opens the overview, screenshots it in
HD and with the original images, then closes it. hd-map-summary.json reports overview_draws."""
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
stage = sys.argv[1] if len(sys.argv) > 1 else "move-jump"
s = Session.launch(language="zh-Hans", images="hd", binary=binary if binary and Path(binary).exists() else None,
                   mini_stage=str(ROOT / f"config/recomp/mini-stages/{stage}.json"))
print("RUN", s.run, flush=True)


def shot(name):
    s.client.call("screenshot", path=str(s.run / f"{name}.png"))
    print("shot", name, flush=True)


def press(key, pause=0.5):
    s.client.call("keys", press=key, hold_ms=80)
    time.sleep(pause)


s.enter_mini_stage()
time.sleep(4)
s.client.call("buttons", buttons="c_right", vis=6); time.sleep(1.5)   # C-right: the overview
shot("overview-hd")
press("f6", 1.2)
shot("overview-original")
press("f6", 0.8)
s.client.call("buttons", buttons="b", vis=6); time.sleep(1.0)       # B closes it
shot("after-hd")
# The overview drawn in HD and in the original must cover the same screen area.
from PIL import Image, ImageChops, ImageStat
hd, og = (Image.open(s.run / f"overview-{n}.png").convert("L") for n in ("hd", "original"))
box = ImageChops.difference(og, Image.new("L", og.size, 0)).point(lambda v: 255 if v > 12 else 0).getbbox()
box_hd = ImageChops.difference(hd, Image.new("L", hd.size, 0)).point(lambda v: 255 if v > 12 else 0).getbbox()
print("OVERVIEW BOX original", box, "hd", box_hd, flush=True)
if box:
    small = lambda im: im.crop(box).resize((box[2] - box[0]) // 8 * 1 or 1, Image.BOX) if False else im.crop(box).resize(((box[2] - box[0]) // 8, (box[3] - box[1]) // 8), Image.BOX)
    print("MEAN LUMA DIFF (1/8 scale)", round(ImageStat.Stat(ImageChops.difference(small(hd), small(og))).mean[0], 1), flush=True)
s.quit()
summary = s.run / "hd-map-summary.json"
print("SUMMARY", summary.read_text() if summary.exists() else "missing", flush=True)
