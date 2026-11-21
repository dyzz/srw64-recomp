#!/usr/bin/env python3
"""Weapon-script actors that draw the attacking unit itself, and how the wide spread treats them.

The wide spread (src/host/battle_spread.hpp, docs/design/battle-animation-rendering.md §17)
moves every actor of a weapon's script and leaves the unit slots 168/169 alone. Some actors
draw the unit's own atlas (unit_poses): an attack pose, a transformed form (天馬翔覇's
pegasus), a fist that flies. They are the unit on the screen, so moving, stretching or
enlarging them moves the unit away from where the original has it. Handoff:
docs/design/battle-spread-unit-actors.md.

Prints, per weapon whose script has such an actor: the units it belongs to (weapons.csv),
the actor's registry, scene and behaviour, how the spread classifies the scene (BEAM is
stretched, BACK is enlarged whole about the screen's centre, MOVE only moved), and whether
the behaviour routine writes the actor's placing (+0x38, 1 or 3 = placed by itself, which
the spread does not keep by its unit) or its own velocity (+0x20..+0x28, so it drifts from
its unit). Both are static reads of the routine's text, not run-time facts.

  .venv/bin/python tools/recomp/analysis/battle_spread_unit_actors.py [--risky] [--json]
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.battle_graphics import read_animation_bank, read_triplets  # noqa: E402

BATTLE_ROM, BATTLE_RAM = 0x121560, 0x801C2600
BEHAVIOURS = 0x80225030          # index behaviour - 3, 12-byte thunks (lui / jr / addiu)
TEXT = ROOT / "build/recomp/cpu-scan/load_00121560/rom_801C2600.text.s"
INC = ROOT / "src/host/battle_spread_scenes.inc"


def scene_lists() -> tuple[set[int], set[int]]:
    text = INC.read_text()

    def listed(name: str) -> set[int]:
        m = re.search(name + r"[^{]*\{([^}]*)\}", text)
        return {int(x) for x in re.findall(r"\d+", m.group(1))} if m else set()
    return listed("kBeamScenes"), listed("kBackdropScenes")


def behaviour_traits(rom: bytes) -> dict[int, dict]:
    if not TEXT.exists():
        return {}
    funcs = {int(m.group(1), 16): m.group(0)
             for m in re.finditer(r"glabel func_([0-9A-F]{8}).*?endlabel", TEXT.read_text(), re.S)}

    def word(address: int) -> int:
        return struct.unpack_from(">I", rom, BATTLE_ROM + address - BATTLE_RAM)[0]
    traits = {}
    for behaviour in range(3, 383):
        thunk = word(BEHAVIOURS + 4 * (behaviour - 3))
        if not BATTLE_RAM <= thunk < 0x80230000:
            continue
        hi, lo = word(thunk) & 0xFFFF, word(thunk + 8) & 0xFFFF
        routine = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
        body = funcs.get(routine, "")
        traits[behaviour] = {
            "routine": f"{routine:08X}",
            "writes_placing": bool(re.search(r"sh\s+\$\w+, 0x38\(\$s0\)", body)),
            "writes_velocity": bool(re.search(r"swc1\s+\$f\d+, 0x2[048]\(\$s0\)", body)),
        }
    return traits


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--risky", action="store_true",
                        help="only BEAM/BACK scenes and behaviours that place by themselves or drift")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    bank = read_animation_bank(rom, "weapon")
    scenes = read_triplets(rom, "battle_scenes")
    poses = read_triplets(rom, "unit_poses")
    beams, backdrops = scene_lists()
    traits = behaviour_traits(rom)
    rows = []
    for w in csv.DictReader((ROOT / "docs/guide/data-mods/weapons.csv").open(encoding="utf-8")):
        weapon = int(w["weapon"])
        units = [int(u) for u in (w["units"] or "").split() if u.isdigit()]
        if weapon >= len(bank):
            continue
        atlases = {poses[u][1] for u in units if u < len(poses)}
        for actor in bank[weapon][1].actors:
            if actor.registry >= len(scenes) or scenes[actor.registry][1] not in atlases:
                continue
            scene = scenes[actor.registry][0]
            kind = "BEAM" if scene in beams else "BACK" if scene in backdrops else "MOVE"
            t = traits.get(actor.behavior, {})
            row = {"weapon": weapon, "name": w["name_ja"], "units": units, "registry": actor.registry,
                   "scene": scene, "behaviour": actor.behavior, "xyz": [actor.x, actor.y, actor.z],
                   "spread": kind, **t}
            if args.risky and kind == "MOVE" and not t.get("writes_placing") and not t.get("writes_velocity"):
                continue
            rows.append(row)
    if args.json:
        print(json.dumps(rows, ensure_ascii=False, indent=1))
        return
    for r in rows:
        flags = "".join(f for f, on in (("P", r.get("writes_placing")), ("V", r.get("writes_velocity"))) if on)
        print(f"{r['weapon']:5} {r['name']:<20} units {','.join(map(str, r['units'])):<12} reg {r['registry']:4} "
              f"scene {r['scene']:5} beh {r['behaviour']:3} {r['spread']:4} {flags}")
    print(f"{len(rows)} actors in {len({r['weapon'] for r in rows})} weapons", file=sys.stderr)


if __name__ == "__main__":
    main()
