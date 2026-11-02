#!/usr/bin/env python3
"""Live check of story MODs (src/host/story_mod.hpp) with the example package
config/recomp/mods/master-asia, on a route's mini stage: --route ia is scene 129
(story-master.json, the talk variable 131), --route oz scene 137 (story-master-oz.json, 133):

  1. the package's events load (story-mod-events.jsonl "loaded": 6 events, 22 lines);
  2. before the talk (the talk variable 3) Domon's command menu offers no 説得;
  3. with it at 0 (as the original's after-battle talk leaves it) the menu offers 説得; with
     Domon at 49 kills it is the refusal: its four lines, Master Asia stays, the variable 0;
     Domon is given his action back and 50 kills, and 説得 is the persuasion,
     and choosing it runs the package's persuasion: its lines are shown, Master Asia
     leaves the map, Wong's original lines follow, the route's variables as listed below;
  4. after the stage is won (3D4A) the end event runs without Master Asia's death, then
     the package's join: its lines are shown and pilot 18 is on the roster with unit 27.

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

# Per route: the stage, the talk variable, the package's events, their marks (0x7000 + place
# in the library), their lines (0xC800 + place), Wong's original lines after the persuasion,
# the end event's lines about Master Asia's end, and the variables after the persuasion.
ROUTES = {
    "ia": {"stage": "story-master.json", "talk": 131, "suffix": "", "marks": (0x7000, 0x7001),
           "lines": (0, 5, 9), "wong": (43215, 43230), "ends": [(43248, 43291)], "after": {131: 3, 128: 3, 101: 0}},
    "oz": {"stage": "story-master-oz.json", "talk": 133, "suffix": "-oz", "marks": (0x7003, 0x7004),
           "lines": (11, 16, 20), "wong": (45581, 45583), "next_phase": (45584, 45590), "ends": [(45606, 45650), (45757, 45765)], "after": {133: 0}},
}
PACKAGE = ROOT / "config/recomp/mods/master-asia"
ENGINE, VARS, CURSOR = 0x8015F950, 0x8015E818, 0x80102308
UNITS, UNIT_SIZE = 0x8016A210, 0x54
PILOTS, PILOT_SIZE = 0x80172F40, 0x4C

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--route", choices=ROUTES, default="ia")
parser.add_argument("--language", default="zh-Hans")
parser.add_argument("--keep", action="store_true", help="leave the game running at the end")
parser.add_argument("--launch-only", action="store_true", help="only start it on the stage, for driving by hand")
parser.add_argument("--before-victory", action="store_true", help="stop on the map after the persuasion, the game left running")
args = parser.parse_args()
ROUTE = ROUTES[args.route]
STAGE = ROOT / "config/recomp/mini-stages" / ROUTE["stage"]
TALK = ROUTE["talk"]
PERSUADE, REFUSED, JOINS = (f"srw64.master-asia/{name}{ROUTE['suffix']}" for name in ("persuade", "refused", "joins"))

mods = ROOT / "build/recomp/debug" / time.strftime("story-mods-%Y%m%dT%H%M%S")
build_package(PACKAGE, mods, archive=False)
s = Session.launch(language=args.language, images="original", mini_stage=str(STAGE),
                   env={"SRW64_MODS_DIR": str(mods)}, detach=args.keep or args.launch_only or args.before_victory)
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
    check("package loaded", bool(loaded) and loaded[-1]["events"] == 6 and loaded[-1]["texts"] == 22 and not loaded[-1]["problems"], json.dumps(loaded[-1:], ensure_ascii=False))
    registered = story_rows("registered")
    scene = 129 if args.route == "ia" else 137
    check(f"scene {scene} registered", bool(registered) and registered[-1]["scene"] == scene, json.dumps(registered[-1:]))
    if args.launch_only:
        print("left on the map")
        sys.exit(0)

    # Before the talk: Domon's menu (the cursor starts on him) has no persuasion.
    check(f"{TALK} is 3 at the start", variable(TALK) == 3)
    s.client.call("keys", press="z", hold_ms=80)
    time.sleep(1.0)
    check("no 説得 before the talk (+0x992 = -1)", word(ENGINE + 0x992) == 0xFFFF, hex(word(ENGINE + 0x992)))
    shot("menu-before")
    s.client.call("keys", press="x", hold_ms=80)
    time.sleep(1.0)

    # After it (131 = 0, as 001d387c leaves it), Domon at 49 kills: 説得 is offered, and it
    # is the refusal (the package's second event: mark 0x7001).
    set_variable(TALK, 0)
    pilots = mem(PILOTS, 100 * PILOT_SIZE)
    domon = next(PILOTS + i * PILOT_SIZE for i in range(100)
                 if pilots[i * PILOT_SIZE] and int.from_bytes(pilots[i * PILOT_SIZE + 2:i * PILOT_SIZE + 4], "big") == 4)
    poke(domon + 0x14, (49).to_bytes(2, "big"))
    s.client.call("keys", press="z", hold_ms=80)
    time.sleep(1.0)
    check("説得 at 49 kills is the refusal (our mark)", word(ENGINE + 0x992) == ROUTE["marks"][1], hex(word(ENGINE + 0x992)))
    shot("menu-after")
    mark = lines_seen()
    choose_persuade()
    advance_until(lambda: finished(REFUSED), 120, "the refusal to end")
    first = MOD_LINES + ROUTE["lines"][1]
    check("the refusal's four lines read", ours(mark) == [first + n for n in range(4)], str(ours(mark)))
    check(f"refused: {TALK} still 0, Master Asia still on the map, not persuaded",
          variable(TALK) == 0 and not any(r.get("event") == PERSUADE for r in story_rows("started")),
          str(variable(TALK)))
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
    check("説得 at 50 kills is the persuasion (our mark)", word(ENGINE + 0x992) == ROUTE["marks"][0], hex(word(ENGINE + 0x992)))
    mark = lines_seen()
    started_before = len(story_rows("started"))
    choose_persuade()
    until(lambda: len(story_rows("started")) > started_before, 30, "the persuasion to start")
    check("persuasion started", story_rows("started")[-1]["event"] == PERSUADE)
    shown = []

    def note_lines() -> bool:
        shown[:] = [r for r in fragments(mark)]
        return finished(PERSUADE)

    advance_until(note_lines, 240, "the persuasion to end")
    ids = [r.get("text_id") for r in fragments(mark)]
    ours = [i for i in ids if i is not None and i >= MOD_LINES]
    check("all five persuasion lines read", ours[:5] == [MOD_LINES + ROUTE["lines"][0] + n for n in range(5)], str(ours))
    texts = [r["text"] for r in fragments(mark) if r.get("text_id", 0) >= MOD_LINES]
    check("in Chinese, by Domon and Master Asia", bool(texts) and texts[0].startswith("师父！") and
          [r["speaker"] for r in fragments(mark) if r.get("text_id", 0) >= MOD_LINES][:2] == ["多蒙", "东方不败"], json.dumps(texts[:2], ensure_ascii=False))
    check("Wong's original lines follow", all(i in ids for i in ROUTE["wong"]), str(ids))
    after = {n: variable(n) for n in ROUTE["after"]}
    check(f"variables: {ROUTE['after']}", after == ROUTE["after"], str(after))
    # On scene 137 the persuasion starts a delay event (3D52) for Wong's next phase, which
    # runs at the next poll with lines of its own.
    advance_until(idle, 120, "the map after the persuasion")
    if ROUTE.get("next_phase"):
        ids = [r.get("text_id") for r in fragments(mark)]
        check("Wong's next phase follows (delay event)", any(i in ids for i in ROUTE["next_phase"]), str(ids[-6:]))
    shot("after-persuasion")
    if args.before_victory:
        print("left on the map after the persuasion")
        sys.exit(0)

    # Win: the end event, then the package's join.
    for _ in range(3):
        s.client.call("keys", press="x", hold_ms=80)
        time.sleep(0.4)
    s.client.call("victory", timeout_ms=20000)
    mark = lines_seen()
    advance_until(lambda: finished(JOINS), 400, "the join after the end event")
    ids = [r.get("text_id") for r in fragments(mark)]
    check("the end event plays nothing of Master Asia's end", not any(a <= i <= b for i in ids if i for a, b in ROUTE["ends"]), str(ids[:12]))
    join = MOD_LINES + ROUTE["lines"][2]
    check("the join's two lines read", [i for i in ids if i and i >= MOD_LINES] == [join, join + 1], str(ids[-4:]))
    units = mem(UNITS, 140 * UNIT_SIZE)
    has_unit = any(units[i * UNIT_SIZE] and int.from_bytes(units[i * UNIT_SIZE + 2:i * UNIT_SIZE + 4], "big") == 27 for i in range(140))
    pilots = mem(PILOTS, 120 * PILOT_SIZE)
    has_pilot = any(pilots[i * PILOT_SIZE] and int.from_bytes(pilots[i * PILOT_SIZE + 2:i * PILOT_SIZE + 4], "big") == 18 for i in range(120))
    check("Master Gundam (27) and Master Asia (18) on the roster", has_unit and has_pilot, f"unit {has_unit} pilot {has_pilot}")
    shot("after-join")
finally:
    failed = [r for r in results if not r[1]]
    print(f"{len(results) - len(failed)}/{len(results)} passed", flush=True)
    if not (args.keep or args.launch_only or args.before_victory):
        try:
            s.quit()
        except HostError:
            pass
sys.exit(1 if any(not r[1] for r in results) else 0)
