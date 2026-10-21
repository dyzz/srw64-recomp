#!/usr/bin/env python3
"""Live check of upgrades::grant_bonuses on a swap (bonus-grant.json mini stage).

Step 1 (control): マジンガーZ at 12 -> マジンガーZ(JS): 1032/1036 at 12, 1037/1038 hidden.
Then Z's weapons are written to 14 (what the cap-break rule allows), and step 2 re-registers
the pilot onto JS while Z exists: 1032/1036 at 14, past JS's cap 13, 1037/1038 unlocked.
"""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.debug.session import ROOT, Session  # noqa: E402

STAGE = ROOT / "config/recomp/mini-stages/bonus-grant.json"
UNITS, UNIT_SIZE, SLOTS, WEAPON_SIZE = 0x8016A210, 0x54, 140, 0x24


def main() -> int:
    s = Session.launch(language="ja", images="original", mini_stage=str(STAGE))
    call = s.client.call
    print("run", s.run, flush=True)

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

    # Session.enter_mini_stage returns only once the opening has ended; the window to write
    # Z's levels is inside the opening, so enter and poll from there.
    s.wait(vi=600, timeout=120)
    end = time.monotonic() + 120
    while call("status")["intro"].get("title_major") != 3:
        if time.monotonic() > end:
            raise SystemExit("title menu not reached")
        call("keys", press="return", hold_ms=100)
        time.sleep(0.5)
    call("ui.click", id="mini-enter")
    result = {"run": str(s.run)}

    def js_at(level):
        js = units().get(263)
        if not js:
            return None
        ws = weapons(js[0])
        return ws if any(w["number"] == 1032 and w["level"] == level for w in ws) else None

    ws = until(lambda: js_at(12), 240, "step 1 swap")
    result["step1"] = pick(ws, 1032, 1036, 1037, 1038)
    call("screenshot", path=str(s.run / "step1.png"))
    print("step1", result["step1"], flush=True)

    z = units()[262][0]
    zs = weapons(z)
    result["z_before"] = {w["number"]: w["level"] for w in zs}
    for w in zs:
        call("memory.write", address=w["address"] + 0x16, hex="0E")
    result["z_after"] = {w["number"]: w["level"] for w in weapons(z)}
    print("z", result["z_before"], "->", result["z_after"], flush=True)

    ws = until(lambda: js_at(14), 240, "step 2 swap")
    time.sleep(1.0)
    ws = weapons(units()[263][0])
    result["step2"] = pick(ws, 1032, 1036, 1037, 1038)
    result["js_all"] = [(w["number"], w["level"], hex(w["flags"])) for w in ws]
    call("screenshot", path=str(s.run / "step2.png"))
    print("step2", result["step2"], flush=True)

    events = s.run / "upgrade-page-events.jsonl"
    result["events"] = [json.loads(l) for l in events.read_text().splitlines()
                        if '"bonus-granted"' in l] if events.exists() else "no log"
    print("events", result["events"], flush=True)

    ok = (all(int(result["step1"][n][1], 16) & 0x04 for n in (1037, 1038))
          and all(not int(result["step2"][n][1], 16) & 0x04 for n in (1037, 1038))
          and result["step2"][1032][0] == 14 and result["step2"][1036][0] == 14)
    result["ok"] = ok
    (s.run / "bonus-grant-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print("OK" if ok else "FAIL", flush=True)
    s.quit()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
