#!/usr/bin/env python3
"""Live checks of upgrades::grant_bonuses: a full-upgrade bonus (D_801DC87C) whose weapon
reached its original cap without the paid step that lands on it still unlocks.

  swap          the bonus-grant.json mini stage. Step 1 (control): マジンガーZ at 12 becomes
                マジンガーZ(JS): 1032/1036 at 12, 1037/1038 hidden. Then Z's weapons are
                written to 14 (what the cap-break rule allows), and step 2 re-registers the
                pilot onto JS while Z exists: 1032/1036 at 14, past JS's cap 13, 1037/1038
                unlocked, event source "swap".
  unified       an edited stage-one-clear save in which スイームルグ (slot 0) is a νガンダム
                (cap 7) with フィンファンネル 209 at 7, its MAP 208 hidden, two other weapons
                at 7. Loaded per weapon without the cap-break rule: 208 stays hidden (the
                intermission entry leaves it). Then 209 is set back to 3 in RAM and the
                unified page opens on ν: the free sync lifts 209 to 7, 208 is unlocked at 7
                and the bonus message names 209 and 208, event source "unified-sync".
  intermission  the same save loaded per weapon with the cap-break rule on: 208 is unlocked
                on entering the インターミッション (an older save's repair), event source
                "intermission".

--run attaches to a game already at the title on another machine; for the save cases its
slot 1 must hold the save --write-save makes. The swap case loads the compiled stage from
--stage-image, a path on that machine (compile it with mini_stage.py compile).
"""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.debug.session import ROOT, Session, run_keys  # noqa: E402
sys.path.insert(0, str(ROOT / "tools/recomp/gameplay"))
sys.path.insert(0, str(ROOT / "src"))
import upgrade_save  # noqa: E402
from srw64_native import rule_settings  # noqa: E402

STAGE = ROOT / "config/recomp/mini-stages/bonus-grant.json"
SOURCE_SAVE = ROOT / "build/recomp/save-recovery-check/intermission-cold-1.source.sram"
UNITS, UNIT_SIZE, SLOTS, WEAPON_SIZE = 0x8016A210, 0x54, 140, 0x24
NU, FIN, FIN_MAP = 50, 209, 208


def write_save(source: Path, out: Path) -> None:
    """スイームルグ (slot 0, four weapons from index 0) as a νガンダム: 209 at 7, 208 hidden
    at 0, 211 and 212 at 7; every weapon's form is 50. A controlled edit."""
    sram = bytearray(source.read_bytes())
    base = upgrade_save.BLOCK
    unit = base + upgrade_save.UNITS
    assert int.from_bytes(sram[unit:unit + 2], "big") >> 6 == 36 and sram[unit + 1] & 0x3F == 4
    sram[unit:unit + 2] = (NU << 6 | 4).to_bytes(2, "big")
    for index, (number, level, flags) in enumerate([(FIN, 7, 0x8), (FIN_MAP, 0, 0x4), (211, 7, 0x8), (212, 7, 0x8)]):
        at = base + upgrade_save.WEAPONS + index * upgrade_save.WEAPON_SIZE
        sram[at:at + 2] = number.to_bytes(2, "big")
        sram[at + 2:at + 4] = NU.to_bytes(2, "big")
        sram[at + 4] = level << 4 | flags
    sram[base:base + 2] = upgrade_save.checksum(sram).to_bytes(2, "big")
    out.write_bytes(bytes(sram))


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("case", nargs="?", choices=("swap", "unified", "intermission"))
parser.add_argument("--cap-break", action="store_true", help="turn on upgrade-cap-break (the intermission case does)")
parser.add_argument("--run", help="attach to a game already at the title (another machine) instead of launching one")
parser.add_argument("--stage-image", help="with --run and swap: the compiled stage on that machine")
parser.add_argument("--write-save", type=Path, help="write the edited save here and stop")
parser.add_argument("--language", default="ja")
args = parser.parse_args()
if args.write_save:
    write_save(SOURCE_SAVE, args.write_save)
    print("wrote", args.write_save)
    sys.exit(0)
if not args.case:
    parser.error("a case, or --write-save")
cap_break = args.cap_break or args.case == "intermission"
rules = list(rule_settings.CORRECTIONS) + (["upgrade-cap-break"] if cap_break else [])

if args.run:
    s = Session.attach(args.run)   # rules and language once at the title (to_title): set at start-up they are overwritten
elif args.case == "swap":
    s = Session.launch(language=args.language, images="original", rules=rules, mini_stage=str(STAGE))
else:
    save = ROOT / "build/recomp/debug/bonus-grant.sram"
    write_save(SOURCE_SAVE, save)
    s = Session.launch(language=args.language, images="original", rules=rules, save=str(save))
call = s.client.call
print("RUN", s.run, flush=True)
result = {"case": args.case, "cap_break": cap_break, "run": str(s.run), "checks": []}


def mem(address, size):
    return bytes.fromhex(call("memory.read", address=address, size=size)["hex"])


def units():
    raw = mem(UNITS, SLOTS * UNIT_SIZE)
    out = {}
    for slot in range(SLOTS):
        u = raw[slot * UNIT_SIZE:(slot + 1) * UNIT_SIZE]
        if u[0]:
            out.setdefault(int.from_bytes(u[2:4], "big"), []).append(
                {"slot": slot, "count": u[0x2C], "list": int.from_bytes(u[0x30:0x34], "big")})
    return out


def weapons(unit):
    raw = mem(unit["list"], unit["count"] * WEAPON_SIZE)
    return [{"number": int.from_bytes(raw[i * 0x24 + 2:i * 0x24 + 4], "big"), "level": raw[i * 0x24 + 0x16],
             "flags": raw[i * 0x24 + 0x22], "address": unit["list"] + i * 0x24} for i in range(unit["count"])]


def pick(ws, *numbers):
    return {w["number"]: (w["level"], hex(w["flags"])) for w in ws if w["number"] in numbers}


def until(condition, timeout, what):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = condition()
        if value:
            return value
        time.sleep(0.2)
    raise SystemExit(f"timed out: {what}")


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{"press": name}])
        time.sleep(pause)


def shot(name):
    path = call("screenshot")["path"] if args.run else str(s.run / name)
    if args.run:
        import shutil
        shutil.copy(s.local_file(path), s.run / name)
    else:
        call("screenshot", path=path)


def granted():
    """The bonus-granted rows of the host's upgrade-page-events.jsonl (none before it exists)."""
    run = s.host_run()
    try:
        text = s.local_file(run + ("\\" if "\\" in run else "/") + "upgrade-page-events.jsonl").read_text(encoding="utf-8")
    except Exception:
        return []
    return [json.loads(line) for line in text.splitlines() if '"bonus-granted"' in line]


def check(name, passed, state):
    result["checks"].append({"check": name, "passed": bool(passed), "state": state})
    print(name, "PASS" if passed else "FAIL", json.dumps(state, ensure_ascii=False), flush=True)


def to_title():
    s.wait(vi=600, timeout=120)
    if args.run:
        s.wait(timeout=240, title_major=2)   # past the opening; START earlier starts the prologue
        time.sleep(2)
    end = time.monotonic() + 120
    while call("status")["intro"].get("title_major") != 3:
        if time.monotonic() > end:
            raise SystemExit("title menu not reached")
        keys("return", pause=.5)
    if args.run:
        call("settings", locale=args.language, rules=rules)
    result["rules"] = call("status")["rules"]


def swap():
    to_title()
    if args.run:
        call("mini_stage.load", path=args.stage_image)
    else:
        # Session.enter_mini_stage returns only once the opening has ended; the window to
        # write Z's levels is inside the opening, so enter and poll from there.
        call("ui.click", id="mini-enter")

    def js_at(level):
        js = units().get(263)
        if not js:
            return None
        ws = weapons(js[0])
        return ws if any(w["number"] == 1032 and w["level"] == level for w in ws) else None

    ws = until(lambda: js_at(12), 240, "step 1 swap")
    step1 = pick(ws, 1032, 1036, 1037, 1038)
    check("swap-at-cap-minus-one", all(int(step1[n][1], 16) & 0x04 for n in (1037, 1038)), step1)
    shot("step1.png")
    z = units()[262][0]
    for w in weapons(z):
        call("memory.write", address=w["address"] + 0x16, hex="0E")
    result["z"] = {w["number"]: w["level"] for w in weapons(z)}
    until(lambda: js_at(14), 240, "step 2 swap")
    time.sleep(1.0)
    ws = weapons(units()[263][0])
    step2 = pick(ws, 1032, 1036, 1037, 1038)
    result["js_all"] = [(w["number"], w["level"], hex(w["flags"])) for w in ws]
    shot("step2.png")
    check("swap-past-cap", step2[1032][0] == 14 and step2[1036][0] == 14
          and all(not int(step2[n][1], 16) & 0x04 for n in (1037, 1038)), step2)
    events = granted()
    check("swap-event", sorted((e["source"], e["upgraded"], e["unlocked"]) for e in events)
          == [("swap", 1032, 1037), ("swap", 1036, 1038)], events)


def load_save():
    """Title ring to ロード, ROMカートリッジ slot 1, as check_unified_weapons.py does."""
    call("settings", weapon_upgrade="single")
    time.sleep(2.5)
    keys("right", "right", "right", pause=1.5)
    keys("return", pause=3)
    for _ in range(8):
        st = call("status")
        if st["intermission_page"].get("visible") or (st.get("intro") or {}).get("title_major") != 7:
            break
        keys("z", pause=1.5)
    s.wait(intermission_page=True, timeout=30)
    time.sleep(1.0)


def nu():
    found = units().get(NU)
    if not found:
        raise SystemExit("no νガンダム in the roster: was the edited save loaded?")
    return found[0]


def unified():
    to_title()
    load_save()
    ws = weapons(nu())
    entry = pick(ws, FIN, FIN_MAP)
    events = granted()
    check("entry-leaves-it", entry[FIN][0] == 7 and int(entry[FIN_MAP][1], 16) & 0x04 and events == [],
          {"weapons": entry, "events": events})
    fin = next(w for w in ws if w["number"] == FIN)
    call("memory.write", address=fin["address"] + 0x16, hex="03")
    call("settings", weapon_upgrade="unified")
    time.sleep(1.0)
    call("ui.click", text="intermission:2")
    list_page = until(lambda: (p := call("status")["upgrade_page"]).get("visible") and p.get("screen") == "list" and p, 30, "machine list")
    slot = nu()["slot"]
    rows = list_page["rows"]
    index = next(i for i, row in enumerate(rows) if row.get("slot") == slot)
    keys(*["down"] * (index - list_page.get("cursor", 0)), pause=.5)
    keys("z")
    page = until(lambda: (p := call("status")["upgrade_page"]).get("visible") and p.get("screen") == "unified" and p, 30, "unified page")
    time.sleep(1.5)
    page = call("status")["upgrade_page"]
    shot("unified-bonus.png")
    after = pick(weapons(nu()), FIN, FIN_MAP, 211, 212)
    check("unified-message", page.get("window") == "bonus" and page.get("bonus", {}).get("numbers") == [FIN, FIN_MAP],
          {"window": page.get("window"), "bonus": page.get("bonus")})
    check("unified-levels", after[FIN][0] == 7 and after[FIN_MAP][0] == 7 and not int(after[FIN_MAP][1], 16) & 0x04, after)
    events = granted()
    check("unified-event", [(e["source"], e["upgraded"], e["unlocked"]) for e in events]
          == [("unified-sync", FIN, FIN_MAP)], events)
    keys("z", pause=1.0)
    page = call("status")["upgrade_page"]
    shot("unified-after.png")
    check("unified-dismissed", page.get("window") == "" and page.get("screen") == "unified",
          {"window": page.get("window"), "level": page.get("level")})


def intermission():
    to_title()
    before = call("status")["rules"]
    load_save()
    entry = pick(weapons(nu()), FIN, FIN_MAP)
    shot("intermission.png")
    check("entry-unlocks", entry[FIN][0] == 7 and entry[FIN_MAP][0] == 0 and not int(entry[FIN_MAP][1], 16) & 0x04,
          {"weapons": entry, "rules_before_load": before, "rules_after_load": call("status")["rules"]})
    events = granted()
    check("entry-event", [(e["source"], e["upgraded"], e["unlocked"]) for e in events]
          == [("intermission", FIN, FIN_MAP)], events)


{"swap": swap, "unified": unified, "intermission": intermission}[args.case]()
result["ok"] = all(c["passed"] for c in result["checks"])
(s.run / f"bonus-grant-{args.case}.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
print("OK" if result["ok"] else "FAIL", flush=True)
s.quit()
sys.exit(0 if result["ok"] else 1)
