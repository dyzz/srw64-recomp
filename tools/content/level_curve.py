#!/usr/bin/env python3
"""Model the original's base level (8010F5F3) over a playthrough from ROM data.

Per scene: the enemy records its scripts deploy (3D45 groups, faction 1 = enemy,
2/4 = third party), each with a level offset, a pilot and a machine. Kill EXP is
(pilot base +0xD + machine +0x1A) x mult[enemy level - own level + 6] (801F60D4,
table 802188CC); hits that do not kill give a tenth. The party: N pilots sharing
kills round-robin; base = mean of the top 15 at stage start, clamped to
[1, min(99, cleared + 40, 95)] (800A4BE0). Enemy level = base + offset (8020ABB4).
"""
import collections
import json
import statistics
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_status as st  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
rom = (ROOT / "rom.z64").read_bytes()
RES = lambda v: v - 0x80075610
OVL = lambda v: v - 0x801C2600 + 0xAB160
MULT = [struct.unpack(">f", rom[OVL(0x802188CC) + 4 * i:][:4])[0] for i in range(15)]
u16 = lambda o: struct.unpack(">H", rom[o:o + 2])[0]
s16 = lambda o: struct.unpack(">h", rom[o:o + 2])[0]

def pilot_exp(actor):
    idx = u16(RES(0x800CA9C4) + 2 * actor)
    return rom[0x7A1A0 + idx * 16 + 0xD]
def unit_exp(machine):
    return rom[0x71B80 + machine * 0x24 + 0x1A]

def load(name):
    with open(ROOT / f"assets/original-data/records/{name}.jsonl") as f:
        return [json.loads(l) for l in f if l.strip()]

dep_by_key = {r["key"]: r for r in load("stage_deployments")}
aux_by_key = {r["key"]: r for r in load("stage_auxiliary")}
scenario_aux = {}
for r in load("scenarios"):
    scene = int(r["key"].rsplit(":", 1)[1])
    for l in r["links"]:
        if l["key"].startswith("base:stage_auxiliary:"):
            scenario_aux[scene] = l["key"]

def group_records(scene):
    out = collections.defaultdict(list)
    aux = aux_by_key.get(scenario_aux.get(scene))
    if not aux:
        return out
    for l in aux["links"]:
        if l["key"].startswith("base:stage_deployments:"):
            raw = bytes.fromhex(dep_by_key[l["key"]]["raw_hex"])
            g = struct.unpack(">h", raw[0:2])[0]
            if g == 999:
                continue
            out[g].append({"actor": struct.unpack(">h", raw[6:8])[0], "offset": raw[9],
                           "machine": struct.unpack(">h", raw[0xA:0xC])[0],
                           "faction": struct.unpack(">h", raw[0x14:0x16])[0]})
    return out

# Groups each scene's scripts deploy, per protagonist section.
deploys = {p: collections.defaultdict(set) for p in st.PROTAGONISTS}
for r in load("stage_events"):
    script = r.get("script") or {}
    for item in script.get("instructions") or []:
        if item["opcode"] != 0x3D45:
            continue
        for p, sections in st.SECTIONS.items():
            if (item.get("section") or "3DD0") in sections:
                for scene in script.get("scenes", []):
                    deploys[p][scene].add(item["operands"][0])

def enemies(p, scene, factions=(1, 2, 4)):
    recs = group_records(scene)
    out = []
    for g in sorted(deploys[p][scene]):
        for rec in recs.get(g, []):
            if rec["faction"] in factions and 0 <= rec["machine"] < 363 and 0 <= rec["actor"] < 361:
                out.append(rec)
    return out

def paths(p):
    g = st.scene_graph()[p]
    res = []
    def walk(node, path):
        if len(res) > 20000:
            return
        nxt = [n for n in g.get(node, []) if n not in path and n not in (123, 124)]
        if not nxt:
            res.append(path)
            return
        for n in nxt:
            walk(n, path + [n])
    walk(st.STARTS[p], [st.STARTS[p]])
    return [x for x in res if x[-1] == 106]

def simulate(path, p, n_pilots=15, start_level=3, hit_share=0.0, factions=(1, 2, 4)):
    exp = [(start_level - 1) * 500] * n_pilots
    level = lambda e: min(e // 500 + 1, 99)
    curve = []
    turn = 0
    for cleared, scene in enumerate(path):
        levels = sorted((level(e) for e in exp), reverse=True)[:15]
        base = int(sum(levels) / len(levels))
        base = max(1, min(base, 99, cleared + 40, 95))
        curve.append((cleared + 1, scene, base, statistics.mean(level(e) for e in exp)))
        for rec in enemies(p, scene, factions):
            e_level = min(base + rec["offset"], 99)
            k = turn % n_pilots
            turn += 1
            diff = max(0, min(14, e_level - level(exp[k]) + 6))
            gain = (pilot_exp(rec["actor"]) + unit_exp(rec["machine"])) * MULT[diff] * (1 + hit_share)
            exp[k] = min(exp[k] + int(gain), 49000)
    return curve

if __name__ == "__main__":
    # The table in docs/gameplay/cheats.md §6.5: route medians (min-max) per party size.
    rows = (0, 9, 19, 29, 39, 49, 54)
    curves = {}
    for n in (10, 15, 20):
        by = collections.defaultdict(list)
        for p in st.PROTAGONISTS:
            for path in paths(p):
                for ep, scene, base, mean in simulate(path, p, n_pilots=n):
                    by[ep - 1].append(base)
        curves[n] = by
    print("cleared | " + " | ".join(f"N={n}" for n in curves))
    for c in rows:
        print(c, "|", " | ".join(f"{int(statistics.median(v[c]))} ({min(v[c])}-{max(v[c])})" for v in curves.values()))
