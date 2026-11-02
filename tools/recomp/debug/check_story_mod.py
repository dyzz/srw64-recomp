#!/usr/bin/env python3
"""Live check of story MODs (src/host/story_mod.hpp) with the example package
config/recomp/mods/master-asia on the story-master mini stage (scene 129):

  1. the package's events load (story-mod-events.jsonl "loaded": 2 events, 7 lines);
  2. before the talk (variable 131 = 3) Domon's command menu offers no 説得;
  3. with 131 = 0 (as the original's after-battle talk leaves it) the menu offers 説得; with
     Domon at 49 kills it is the refusal: its four lines, Master Asia stays, 131 stays 0;
     Domon is given his action back and 50 kills, and 説得 is the persuasion,
     and choosing it runs the package's persuasion: its first line is shown, Master Asia
     leaves the map, Wong comes in (group 11), 131 ends at 3 and 101 at 0;
  4. after the stage is won (3D4A) the end event runs, then the package's "joins":
     its line is shown and pilot 18 is on the roster with unit 27.

Screens are saved beside the run. Pass --keep to leave the game open afterwards.
"""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/
from recomp.debug.session import ROOT, Session, HostError  # noqa: E402
sys.path.insert(0, str(ROOT / "tools"))
from mods.pack import build_package  # noqa: E402

STAGE = ROOT / "config/recomp/mini-stages/story-master.json"
PACKAGE = ROOT / "config/recomp/mods/master-asia"
ENGINE, VARS, CURSOR = 0x8015F950, 0x8015E818, 0x80102308
UNITS, UNIT_SIZE = 0x8016A210, 0x54
PILOTS, PILOT_SIZE = 0x80172F40, 0x4C

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--language", default="zh-Hans")
parser.add_argument("--keep", action="store_true", help="leave the game running at the end")
parser.add_argument("--launch-only", action="store_true", help="only start it on the stage, for driving by hand")
args = parser.parse_args()

mods = ROOT / "build/recomp/debug" / time.strftime("story-mods-%Y%m%dT%H%M%S")
build_package(PACKAGE, mods, archive=False)
s = Session.launch(language=args.language, images="original", mini_stage=str(STAGE),
                   env={"SRW64_MODS_DIR": str(mods)}, detach=args.keep or args.launch_only)
print("run", s.run, flush=True)
results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((name, ok, detail))
    print(("PASS " if ok else "FAIL ") + name + (f" — {detail}" if detail else ""), flush=True)


def mem(address: int, size: int) -> bytes:
    return bytes.fromhex(s.client.call("memory.read", address=address, size=size)["hex"])


def word(address: int, size: int = 2) -> int:
    return int.from_bytes(mem(address, size), "big")


def poke(address: int, data: bytes) -> None:
    s.client.call("memory.write", address=address, hex=data.hex())


def variable(n: int) -> int:
    return (word(VARS + 2 * (n // 8)) >> (2 * (n % 8))) & 3


def set_variable(n: int, value: int) -> None:
    at = VARS + 2 * (n // 8)
    old = word(at)
    poke(at, ((old & ~(3 << (2 * (n % 8)))) | (value << (2 * (n % 8)))).to_bytes(2, "big"))


def story_rows(action: str | None = None) -> list[dict]:
    path = s.run / "story-mod-events.jsonl"
    rows = [json.loads(l) for l in path.read_text().splitlines()] if path.exists() else []
    return [r for r in rows if not action or r.get("action") == action]


def idle() -> bool:
    return word(ENGINE + 4) == 0xC0 and word(ENGINE + 0x97C) == 0x80 and word(0x8010F5E8, 1) == 1


def until(test, timeout: float, what: str) -> None:
    deadline = time.monotonic() + timeout
    while not test():
        if time.monotonic() > deadline:
            raise HostError(f"timed out: {what}")
        time.sleep(0.3)


def shot(name: str) -> None:
    s.client.call("screenshot", path=str(s.run.parent / f"{s.run.name}.{name}.png"))


def read_dialogue(timeout: float = 60.0) -> list[str]:
    """Every story line shown until the event ends, advancing each with Z."""
    seen: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = s.client.call("status")
        for box in (status.get("dialogue") or {}).get("boxes", []):
            if box.get("active") and box.get("text") and (not seen or seen[-1] != box["text"]):
                seen.append(box["text"])
        if word(ENGINE + 0x97C) == 0x80 and not (status.get("dialogue") or {}).get("active"):
            break
        s.client.call("keys", press="z", hold_ms=80)
        time.sleep(0.35)
    return seen


def fragments(since: int = 0) -> list[dict]:
    path = s.run / "dialogue-events.jsonl"
    rows = [json.loads(l) for l in path.read_text().splitlines()[since:]] if path.exists() else []
    return [r for r in rows if r.get("kind") == "fragment"]


def lines_seen() -> int:
    path = s.run / "dialogue-events.jsonl"
    return len(path.read_text().splitlines()) if path.exists() else 0


def advance_until(test, timeout: float, what: str) -> None:
    """Z every 0.6 s (dialogue pages) until test() holds."""
    deadline = time.monotonic() + timeout
    while not test():
        if time.monotonic() > deadline:
            raise HostError(f"timed out: {what}")
        s.client.call("keys", press="z", hold_ms=100)
        time.sleep(0.6)


def finished(event: str) -> bool:
    return any(r.get("event") == event for r in story_rows("finished"))


MOD_LINES = 0xC800
ROSTER, SIDE_SIZE, SLOT_SIZE = 0x8015E100, 0x258, 0x14   # map units; +0xB 0x80 = has acted


def map_slot(pilot: int) -> int:
    """Our side's map slot whose unit's first pilot is `pilot`."""
    for slot in range(30):
        unit = word(ROSTER + slot * SLOT_SIZE + 0xC, 4)   # the unit instance, as 800A4634 reads it
        if unit and word(word(unit + 0x38, 4) + 2) == pilot:
            return slot
    raise HostError(f"pilot {pilot} is not on the map")


def choose_persuade() -> None:
    """Domon's menu (the cursor is on him), its second row: 説得."""
    s.client.call("keys", press="down", hold_ms=80)
    time.sleep(0.4)
    s.client.call("keys", press="z", hold_ms=80)


def ours(since: int) -> list[int]:
    return [r["text_id"] for r in fragments(since) if r.get("text_id", 0) >= MOD_LINES]
try:
    s.enter_mini_stage()
    until(idle, 180, "the stage's opening")
    loaded = story_rows("loaded")
    check("package loaded", bool(loaded) and loaded[-1]["events"] == 3 and loaded[-1]["texts"] == 11 and not loaded[-1]["problems"], json.dumps(loaded[-1:], ensure_ascii=False))
    registered = story_rows("registered")
    check("scene 129 registered", bool(registered) and registered[-1]["scene"] == 129, json.dumps(registered[-1:]))
    if args.launch_only:
        print("left on the map")
        sys.exit(0)

    # Before the talk: Domon's menu (the cursor starts on him) has no persuasion.
    check("131 is 3 at the start", variable(131) == 3)
    s.client.call("keys", press="z", hold_ms=80)
    time.sleep(1.0)
    check("no 説得 before the talk (+0x992 = -1)", word(ENGINE + 0x992) == 0xFFFF, hex(word(ENGINE + 0x992)))
    shot("menu-before")
    s.client.call("keys", press="x", hold_ms=80)
    time.sleep(1.0)

    # After it (131 = 0, as 001d387c leaves it), Domon at 49 kills: 説得 is offered, and it
    # is the refusal (the package's second event: mark 0x7001).
    set_variable(131, 0)
    pilots = mem(PILOTS, 100 * PILOT_SIZE)
    domon = next(PILOTS + i * PILOT_SIZE for i in range(100)
                 if pilots[i * PILOT_SIZE] and int.from_bytes(pilots[i * PILOT_SIZE + 2:i * PILOT_SIZE + 4], "big") == 4)
    poke(domon + 0x14, (49).to_bytes(2, "big"))
    s.client.call("keys", press="z", hold_ms=80)
    time.sleep(1.0)
    check("説得 at 49 kills is the refusal (+0x992 = 0x7001)", word(ENGINE + 0x992) == 0x7001, hex(word(ENGINE + 0x992)))
    shot("menu-after")
    mark = lines_seen()
    choose_persuade()
    advance_until(lambda: finished("srw64.master-asia/refused"), 120, "the refusal to end")
    check("the refusal's four lines read", ours(mark) == [MOD_LINES + n for n in range(5, 9)], str(ours(mark)))
    check("refused: 131 still 0, Master Asia still on the map, not persuaded",
          variable(131) == 0 and not any(r.get("event") == "srw64.master-asia/persuade" for r in story_rows("started")),
          str(variable(131)))
    until(idle, 60, "the map after the refusal")
    shot("after-refusal")

    # Domon gets his action back and 50 kills: now 説得 is the persuasion (mark 0x7000).
    # Acted is two things (cheats.md §7): the pilot's actions left (+0x35, refilled from +0x34
    # each phase) and the map slot's +0xB 0x80.
    slot = map_slot(4)
    flags = word(ROSTER + slot * SLOT_SIZE + 0xB, 1)
    poke(ROSTER + slot * SLOT_SIZE + 0xB, bytes([flags & 0x7F]))
    poke(domon + 0x35, bytes([word(domon + 0x34, 1)]))
    poke(domon + 0x14, (50).to_bytes(2, "big"))
    for _ in range(2):
        s.client.call("keys", press="x", hold_ms=80)
        time.sleep(0.4)
    s.client.call("keys", press="z", hold_ms=80)
    time.sleep(1.0)
    check("説得 at 50 kills is the persuasion (+0x992 = 0x7000)", word(ENGINE + 0x992) == 0x7000, hex(word(ENGINE + 0x992)))
    mark = lines_seen()
    started_before = len(story_rows("started"))
    choose_persuade()
    until(lambda: len(story_rows("started")) > started_before, 30, "the persuasion to start")
    check("persuasion started", story_rows("started")[-1]["event"] == "srw64.master-asia/persuade")
    shown = []

    def note_lines() -> bool:
        shown[:] = [r for r in fragments(mark)]
        return finished("srw64.master-asia/persuade")

    advance_until(note_lines, 240, "the persuasion to end")
    ids = [r.get("text_id") for r in fragments(mark)]
    ours = [i for i in ids if i is not None and i >= MOD_LINES]
    check("all five persuasion lines read", ours[:5] == [MOD_LINES + n for n in range(5)], str(ours))
    texts = [r["text"] for r in fragments(mark) if r.get("text_id", 0) >= MOD_LINES]
    check("in Chinese, by Domon and Master Asia", bool(texts) and texts[0].startswith("师父！") and
          [r["speaker"] for r in fragments(mark) if r.get("text_id", 0) >= MOD_LINES][:2] == ["多蒙", "东方不败"], json.dumps(texts[:2], ensure_ascii=False))
    check("Wong's original lines follow", 43215 in ids and 43230 in ids, str(ids))
    check("variables: 131 = 3, 128 = 3, 101 = 0", (variable(131), variable(128), variable(101)) == (3, 3, 0),
          str((variable(131), variable(128), variable(101))))
    until(idle, 60, "the map after the persuasion")
    shot("after-persuasion")

    # Win: the end event, then the package's join.
    for _ in range(3):
        s.client.call("keys", press="x", hold_ms=80)
        time.sleep(0.4)
    s.client.call("victory", timeout_ms=20000)
    mark = lines_seen()
    advance_until(lambda: finished("srw64.master-asia/joins"), 300, "the join after the end event")
    ids = [r.get("text_id") for r in fragments(mark)]
    check("the end event plays no death scene", not any(43248 <= i <= 43291 for i in ids if i), str(ids[:12]))
    check("the join's two lines read", [i for i in ids if i and i >= MOD_LINES] == [MOD_LINES + 9, MOD_LINES + 10], str(ids))
    units = mem(UNITS, 140 * UNIT_SIZE)
    has_unit = any(units[i * UNIT_SIZE] and int.from_bytes(units[i * UNIT_SIZE + 2:i * UNIT_SIZE + 4], "big") == 27 for i in range(140))
    pilots = mem(PILOTS, 120 * PILOT_SIZE)
    has_pilot = any(pilots[i * PILOT_SIZE] and int.from_bytes(pilots[i * PILOT_SIZE + 2:i * PILOT_SIZE + 4], "big") == 18 for i in range(120))
    check("Master Gundam (27) and Master Asia (18) on the roster", has_unit and has_pilot, f"unit {has_unit} pilot {has_pilot}")
    shot("after-join")
finally:
    failed = [r for r in results if not r[1]]
    print(f"{len(results) - len(failed)}/{len(results)} passed", flush=True)
    if not (args.keep or args.launch_only):
        try:
            s.quit()
        except HostError:
            pass
sys.exit(1 if any(not r[1] for r in results) else 0)
