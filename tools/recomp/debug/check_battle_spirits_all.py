#!/usr/bin/env python3
"""Every spirit the pre-battle page offers, cast from the page, on a running game.

Start the game with the debug interface, load config/recomp/mini-stages/spirit-enemy-phase.json
and open the pre-battle page (タケル attacking the enemy, or the enemy attacking him after the
phase ends), then:

  check_battle_spirits_all.py --run RUN [--batches 2,3,4,5,7,9 11,12,14 ...] [--out DIR]

Each batch is written over the page's pilot (+0x0A count, +0x0B ids, up to six), with 999 SP;
his and マリア's HP are lowered first so the recovering commands have something to do. A
cast passes when the page comes back on its own, the SP went down by the command's cost and
the game still answers; what each command did (flags, HP, morale, the unit list 80227B61 that
友情, 愛 and かく乱 walk) is recorded with a screenshot after each cast.
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session

PARTICIPANTS = 0x8018B6E8
ROSTER = 0x8015E100           # +side*0x258 +slot*0x14; +0x0C the unit
TARGETS = 0x80227B60          # cast id, count, handles
NAMES = ("自爆 偵察 加速 てかげん 集中 ひらめき 根性 必中 信頼 鉄壁 みがわり 熱血 気合 ド根性 幸運 脱力 努力 魂 "
         "隠れ身 覚醒 補給 挑発 激励 かく乱 友情 愛 再動 奇跡 祈り 復活").split()
DEFAULT_BATCHES = ["2,3,4,5,7,9", "11,12,14,16,18,19", "6,13,17,23,24,25", "27"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True)
    parser.add_argument("--batches", nargs="+", default=DEFAULT_BATCHES)
    parser.add_argument("--out", help="where the screenshots and results go (default: the run directory)")
    args = parser.parse_args()
    s = Session.attach(args.run)
    out = Path(args.out or s.run / "spirits-all")
    out.mkdir(parents=True, exist_ok=True)
    results = []

    def call(method, **params):
        return s.client.call(method, **params)

    def mem(address, size):
        return bytes.fromhex(call("memory.read", address=address, size=size)["hex"])

    def u16(address):
        return int.from_bytes(mem(address, 2), "big")

    def u32(address):
        return int.from_bytes(mem(address, 4), "big")

    def write(address, data):
        call("memory.write", address=address, hex=bytes(data).hex())

    def page():
        return call("status")["battle_page"]

    def wait_page(serial, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            p = page()
            if p.get("visible") and p["serial"] > serial and not p.get("spirit_running"):
                return p
            time.sleep(.15)
        return None

    def slot(n):
        base = PARTICIPANTS + n * 0x5C
        return {"unit": u32(base + 4), "pilot": u32(base + 0xC)}

    def roster_unit(side, n):
        return u32(ROSTER + side * 0x258 + n * 0x14 + 0xC)

    def shot(name):
        try:
            taken = call("screenshot")
            shutil.copy(s.local_file(taken["path"]), out / f"{name}.png")
        except Exception as error:  # a missing picture is noted, not fatal
            print("screenshot failed", name, error, flush=True)

    def refresh():
        """The page lists spirits when it opens; a language change lists them again."""
        locale = call("status")["locale"]
        call("settings", locale="en" if locale != "en" else "ja")
        time.sleep(.6)
        call("settings", locale=locale)
        time.sleep(.8)

    p = page()
    if not p.get("visible"):
        sys.exit("open the pre-battle page first")
    mine = 0 if p["attacker"]["side"] == 0 else 1
    theirs = 1 - mine
    me, them = slot(mine), slot(theirs)
    maria = roster_unit(0, 1)
    print("mode", p["mode"], "pilot", hex(me["pilot"]), "unit", hex(me["unit"]), "maria", hex(maria), flush=True)

    def state():
        return {"sp": u16(me["pilot"] + 0x16), "flags": u32(me["pilot"] + 0x1C), "morale": u16(me["pilot"] + 0x20),
                "hp": u16(me["unit"] + 4), "max_hp": u16(me["unit"] + 6), "en": u16(me["unit"] + 8),
                "maria_hp": u16(maria + 4), "enemy_flags": u32(them["pilot"] + 0x1C), "enemy_hp": u16(them["unit"] + 4)}

    for batch in args.batches:
        ids = [int(x) for x in batch.split(",")]
        write(me["pilot"] + 0xA, [len(ids)] + ids)
        write(me["pilot"] + 0x16, (999).to_bytes(2, "big") * 2)
        refresh()
        for spirit in ids:
            # Something to recover: him at half, マリア at a third.
            write(me["unit"] + 4, (u16(me["unit"] + 6) // 2).to_bytes(2, "big"))
            write(maria + 4, (u16(maria + 6) // 3).to_bytes(2, "big"))
            write(TARGETS, b"\xff" * 8)   # what a stale list looked like in 0.4.8
            p = page()
            option = next((o for o in p["spirit_options"] if o["id"] == spirit), None)
            row = {"id": spirit, "name": NAMES[spirit], "mode": p["mode"]}
            if not option or not option["enabled"]:
                row.update(result="not offered", reason=option and option["reason"])
                results.append(row)
                print(json.dumps(row, ensure_ascii=False), flush=True)
                continue
            before = state()
            if not p.get("spirit_menu"):
                call("ui.click", id="battle-spirits")
                time.sleep(.4)
            call("ui.click", id=f"battle-cast:{option['crew']}:{option['slot']}")
            back = wait_page(p["serial"])
            alive = s.alive()
            after = state() if alive else {}
            targets = mem(TARGETS, 8).hex() if alive else ""
            row.update(cost=option["cost"], before=before, after=after, targets=targets, alive=alive,
                       returned=bool(back))
            row["passed"] = bool(back) and alive and after.get("sp") == before["sp"] - option["cost"]
            row["result"] = "PASS" if row["passed"] else "FAIL"
            results.append(row)
            print(json.dumps(row, ensure_ascii=False), flush=True)
            (out / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
            if alive:
                shot(f"{p['mode']}-{spirit:02d}-{NAMES[spirit]}")
            if not row["passed"]:
                sys.exit(f"{NAMES[spirit]} failed")
    (out / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
    print("DONE", sum(r.get("passed", False) for r in results), "passed of", len(results), flush=True)


if __name__ == "__main__":
    main()
