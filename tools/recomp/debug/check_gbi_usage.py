#!/usr/bin/env python3
"""What the game asks of the N64 graphics pipeline, scene by scene (docs/design/psv-port.md V0,
docs/data/gbi-usage.md).

Runs the desktop RT64 host once per scene with SRW64_GBI_STATS set (src/host/gbi_stats.cpp
counts every display list's commands, combiner settings, other modes and texture formats),
with the original pictures and the original screens (the native pages switched off) so the
lists are the game's own: the title and its demo battle, the start of a game, the tactical
map, a battle animation, the intermission screens, the world map, a chapter title card and
the ending, and a scripted stage with dialogue. Each scene's counts go to
build/recomp/gbi-usage/<scene>.json; tools/recomp/analysis/gbi_usage.py turns them into the
tables the data page quotes.

  --scenes a,b   only these scenes (title, start, map, battle, intermission, worldmap, act,
                 ending, flow)
  --binary PATH  a built host (build/recomp/gfx-build/srw64-gfx-host); otherwise the launch
                 builds one
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, HostError

ROOT = Path(__file__).resolve().parents[3]
OUTPUT = ROOT / "build/recomp/gbi-usage"
SAVE = ROOT / "build/recomp/save-recovery-check/intermission-cold-1.source.sram"
STAGES = ROOT / "config/recomp/mini-stages"
SCENES = ("title", "start", "map", "battle", "intermission", "worldmap", "act", "ending", "flow")

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--scenes", default=",".join(SCENES))
parser.add_argument("--binary", default=str(ROOT / "build/recomp/gfx-build/srw64-gfx-host"))
args = parser.parse_args()
binary = args.binary if Path(args.binary).exists() else None
OUTPUT.mkdir(parents=True, exist_ok=True)
results = {}


def launch(scene: str, **kwargs) -> Session:
    stats = OUTPUT / f"{scene}.json"
    if stats.exists():
        stats.unlink()
    s = Session.launch(language="zh-Hans", images="original", binary=binary,
                       env={"SRW64_GBI_STATS": str(stats)}, **kwargs)
    print("RUN", scene, s.run, flush=True)
    return s


def status(s):
    return s.client.call("status")


def key(s, name, pause=0.6):
    s.client.call("keys", press=name, hold_ms=100)
    time.sleep(pause)


def title_menu(s, timeout=60):
    """PRESS START through to the ring menu (title_major 3)."""
    s.wait(vi=600, timeout=300)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if status(s)["intro"].get("title_major") == 3:
            time.sleep(2.5)   # the ring is still turning in
            return
        key(s, "return", 0.5)
    raise HostError("title menu not reached")


def original_screens(s):
    """The original screens wherever a native page would replace them."""
    for name in ("title_ui", "intermission_ui", "battle_ui", "name_entry_ui"):
        try:
            s.client.call("settings", **{name: "original"})
        except HostError as error:
            print("settings", name, error, flush=True)


def finish(s, scene):
    stats = OUTPUT / f"{scene}.json"
    s.client.call("screenshot", path=str(s.run / f"{scene}-end.png"))
    s.quit()
    for _ in range(50):
        if stats.exists():
            break
        time.sleep(0.2)
    data = json.loads(stats.read_text()) if stats.exists() else None
    results[scene] = {"run": str(s.run), "lists": data and data.get("lists"), "ok": data is not None}
    (OUTPUT / "runs.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
    print("DONE", scene, results[scene], flush=True)


def scene_title():
    # The title: the flying works, PRESS START, the ring menu, then no input until the
    # title falls back and its demo battle plays, then the ring once more.
    s = launch("title")
    original_screens(s)
    title_menu(s)
    s.client.call("screenshot", path=str(s.run / "title-ring.png"))
    time.sleep(100)
    s.client.call("screenshot", path=str(s.run / "title-demo.png"))
    title_menu(s)
    for _ in range(3):
        key(s, "up", 1.2)   # the ring turns through its entries
    finish(s, "title")


def scene_start():
    # スタート from the ring: whatever the game draws until the name entry (its original
    # screen, switched on) and some seconds after.
    s = launch("start")
    original_screens(s)
    title_menu(s)
    key(s, "return", 1.0)
    for n in range(12):
        time.sleep(5)
        st = status(s)
        if st.get("name_page", {}).get("visible"):
            break
    key(s, "z", 1.0)
    key(s, "z", 1.0)
    time.sleep(10)
    finish(s, "start")


def scene_map():
    s = launch("map", mini_stage=str(STAGES / "battle-ui.json"))
    original_screens(s)
    s.enter_mini_stage()
    time.sleep(3)
    for name in ("right", "right", "down", "down", "left", "up"):
        key(s, name, 0.5)
    key(s, "z", 1.5)      # the unit's command window
    key(s, "x", 1.0)
    key(s, "e", 1.5)      # R: the next unit
    key(s, "e", 1.5)
    key(s, "up", 0.5)
    key(s, "z", 1.5)      # an empty cell: the map menu
    s.client.call("screenshot", path=str(s.run / "map-menu.png"))
    key(s, "x", 1.0)
    time.sleep(3)
    finish(s, "map")


def scene_battle():
    s = launch("battle", mini_stage=str(STAGES / "battle-ui.json"))
    original_screens(s)
    s.enter_mini_stage()
    time.sleep(3)
    # The enemy attacks: end the phase from the empty cell above the player unit
    # (check_aspect.py); the original pre-battle screen, then the animation.
    for name in ("up", "z", "z", "z"):
        key(s, name, 0.8)
    end = time.monotonic() + 120
    while time.monotonic() < end:
        st = status(s)
        if st["battle_page"].get("visible") or st.get("intro", {}).get("title_major") is None:
            pass
        time.sleep(0.5)
        if time.monotonic() > end - 110:
            break
    s.client.call("screenshot", path=str(s.run / "battle-confirm.png"))
    key(s, "z", 1.0)      # confirm (the original screen or the native page's A)
    time.sleep(8)
    s.client.call("screenshot", path=str(s.run / "battle-animation.png"))
    time.sleep(27)
    s.client.call("screenshot", path=str(s.run / "battle-after.png"))
    time.sleep(5)
    finish(s, "battle")


def scene_intermission():
    s = launch("intermission", save=str(SAVE))
    original_screens(s)
    title_menu(s)
    # ロード is three to the right of スタート; ROM cartridge, slot 1, yes (check_intermission.py).
    for name in ("right", "right", "right"):
        key(s, name, 1.5)
    key(s, "return", 3.0)
    for _ in range(8):
        st = status(s)
        if (st.get("intro") or {}).get("title_major") != 7:
            break
        key(s, "z", 1.5)
    time.sleep(4)
    s.client.call("screenshot", path=str(s.run / "intermission-menu.png"))
    # Every entry of the original menu opened and closed: ユニット改造, 強化パーツ, のりかえ,
    # 能力, データセーブ, リンク and the rest; a screen that needs more presses just stays.
    for item in range(9):
        key(s, "z", 3.0)
        s.client.call("screenshot", path=str(s.run / f"intermission-{item}.png"))
        key(s, "z", 2.0)
        key(s, "x", 1.0)
        key(s, "x", 1.0)
        key(s, "x", 1.0)
        key(s, "down", 0.8)
    finish(s, "intermission")


def scene_stage(scene, seconds):
    s = launch(scene, mini_stage=str(STAGES / f"{scene}.json"))
    original_screens(s)
    s.wait(vi=600, timeout=300)
    title_menu(s)
    s.client.call("ui.click", id="mini-enter")
    for n in range(seconds // 5):
        time.sleep(5)
        if n % 3 == 2:
            s.client.call("screenshot", path=str(s.run / f"{scene}-{n:02}.png"))
    finish(s, scene)


def scene_flow():
    # The opening event's dialogue, the map, victory and the ending event: A every two
    # seconds pages through whatever is up.
    s = launch("flow", mini_stage=str(STAGES / "flow.json"))
    original_screens(s)
    title_menu(s)
    s.client.call("ui.click", id="mini-enter")
    for n in range(40):
        time.sleep(2)
        key(s, "z", 0.2)
        if n % 8 == 7:
            s.client.call("screenshot", path=str(s.run / f"flow-{n:02}.png"))
    finish(s, "flow")


runners = {"title": scene_title, "start": scene_start, "map": scene_map, "battle": scene_battle,
           "intermission": scene_intermission, "worldmap": lambda: scene_stage("worldmap", 45),
           "act": lambda: scene_stage("act", 30), "ending": lambda: scene_stage("ending", 30), "flow": scene_flow}
for scene in args.scenes.split(","):
    try:
        runners[scene]()
    except Exception as error:   # the next scene still runs; runs.json records the failure
        results[scene] = {"error": repr(error)}
        (OUTPUT / "runs.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
        print("FAILED", scene, repr(error), flush=True)
print(json.dumps(results, ensure_ascii=False, indent=2))
