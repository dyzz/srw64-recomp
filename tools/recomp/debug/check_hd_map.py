#!/usr/bin/env python3
"""Look at an HD tactical map in the game (docs/design/tactical-map-hd-kit.md).

Launches the map20-view mini stage with SRW64_HD_MAPS pointing at the exported runtime
assets, screenshots the top-left of the map, scrolls the cursor to the bottom-right and
screenshots again, then switches to the original images (F6) at the same position for
comparison. Screenshots and hd-map-summary.json land in the run directory."""
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
# Argument: a mini-stage name under config/recomp/mini-stages, or a map number, for which a
# unit-less viewer stage is derived from map20-view.
arg = sys.argv[1] if len(sys.argv) > 1 else "map20-view"
if arg.isdigit():
    template = json.loads((ROOT / "config/recomp/mini-stages/map20-view.json").read_text())
    template["map"] = int(arg)
    template["name"] = f"map{arg}-view"
    stage_path = Path(os.environ.get("TMPDIR", "/tmp")) / f"map{arg}-view.json"
    stage_path.write_text(json.dumps(template))
else:
    stage_path = ROOT / f"config/recomp/mini-stages/{arg}.json"
# A prebuilt host (SRW64_HOST_BINARY, default last night's gfx-build) avoids a rebuild that
# other sessions' source edits would force.
binary = os.environ.get("SRW64_HOST_BINARY") or str(ROOT / "build/recomp/gfx-build/srw64-gfx-host")
s = Session.launch(language="zh-Hans", images="hd", binary=binary if Path(binary).exists() else None,
                   mini_stage=str(stage_path),
                   detach=bool(os.environ.get("SRW64_HD_MAP_KEEP")))
print("RUN", s.run, flush=True)


def shot(name):
    s.client.call("screenshot", path=str(s.run / f"{name}.png"))
    print("shot", name, flush=True)


s.enter_mini_stage()
time.sleep(4)
shot("hd-top-left")
time.sleep(2.5)
shot("hd-top-left-later")
s.client.call("keys", down="down+right")
time.sleep(6)
s.client.call("keys", release_all=True)
time.sleep(1.5)
shot("hd-bottom-right")
s.client.call("keys", press="f6", hold_ms=80)
time.sleep(1.5)
shot("original-bottom-right")
s.client.call("keys", press="f6", hold_ms=80)
time.sleep(1.0)
s.client.call("keys", down="up")
time.sleep(2.5)
s.client.call("keys", release_all=True)
time.sleep(1.5)
shot("hd-middle")
status = s.client.call("status")
(s.run / "hd-map-status.json").write_text(json.dumps(status, ensure_ascii=False, indent=2) + "\n")
if os.environ.get("SRW64_HD_MAP_KEEP"):
    print("KEPT RUNNING; quit with Esc or srw64ctl.py quit --run", s.run, flush=True)
else:
    s.quit()
    summary = s.run / "hd-map-summary.json"
    print("SUMMARY", summary.read_text() if summary.exists() else "missing", flush=True)
