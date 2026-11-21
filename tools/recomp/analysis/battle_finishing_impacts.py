#!/usr/bin/env python3
"""List every weapon animation and the original actors involved in its hit.

Run with the project's Python from any directory. The CSV includes animation-only
alternate slots and the ordinary weapon/unit pairs that select those slots.
The family column identifies known finishing cues in battle_impact.hpp. A blank
family means no special cue was identified, not that someone viewed every frame.
"""
import argparse
import csv
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.battle_graphics import read_animation_bank, read_battle_weapons

# Plain-hit defender actions from the 801E4668 dispatch table. The game uses the
# live routine instead, so a dodge, shield reaction or replaced actor is respected.
GROUPS = {
    "throw-landing": (133,),
    "throw-follow-up": (135,),
    "return-pass": (132, 154, 155, 167),
    "successive-strikes": (134, 136, 138, 141, 143, 144, 161, 162, 185),
    "melee-finisher": (149, 150, 151, 152, 153),
    "release-and-knockback": (147, 168, 173, 180),
    "scatter-finisher": (160,),
}
FAMILY = {action: family for family, actions in GROUPS.items() for action in actions}


def routine(rom, action, *, effect=False):
    """Read the function address returned by the original switch thunk."""
    base = 0x121560 - 0x801C2600
    if effect:
        if not 3 <= action < 383:
            return ""
        entry = 0x80225030 + 4 * (action - 3)
    else:
        if not 0 <= action < 189:
            return "801E3CBC"
        entry = 0x80223990 + 4 * action
    thunk, = struct.unpack_from(">I", rom, base + entry)
    if thunk == 0x80221988:
        return ""
    high, jump, low = struct.unpack_from(">3I", rom, base + thunk)
    if thunk == 0x801E4E10:
        low = jump
    if high >> 16 != 0x3C02 or low >> 16 != 0x2442:
        raise ValueError(f"Unexpected dispatch thunk at {thunk:08X}")
    address = ((high & 0xFFFF) << 16) + ((low & 0x7FFF) - (low & 0x8000))
    return f"{address:08X}"


def actors(rom, script):
    return " ".join(f"{a.behavior}/{routine(rom, a.behavior, effect=True)}" for a in script.actors)


def census(rom, names):
    scripts = read_animation_bank(rom, "weapon")
    hits = read_animation_bank(rom, "hit")
    records = read_battle_weapons(rom)
    aliases = defaultdict(list)
    for weapon, unit, animation in struct.iter_unpack(">3H", rom[0x561AC:0x561AC + 6 * 4096]):
        if not (weapon or unit or animation):
            break
        aliases[animation].append(f"{weapon}/{unit}")
    else:
        raise ValueError("unterminated weapon/unit animation table")

    for i, ((offset, script), record) in enumerate(zip(scripts, records, strict=True)):
        row = names[i]
        assert int(row["weapon"]) == i
        hit = hits[script.hit_script][1]
        mode = record["hit_pattern"]
        count, gap, timeout, lethal = struct.unpack_from(">4h", rom, 0x181DB0 + mode * 8)
        families = []
        if lethal:
            families.append("lethal-reserve")
        if any(actor.behavior == 58 for actor in script.actors):
            families.append("lettering-flash")
        if sum(actor.behavior == 310 for actor in script.actors) > 1 or any(a.behavior == 345 for a in script.actors):
            families.append("paired-weapon-strike")
        if any(a.behavior in (96, 99) for a in script.actors):
            # 99 replaces its own callback with 96 as the camera changes sides.
            families.append("storm-final-impact")
        if script.defender_action in FAMILY:
            families.append(FAMILY[script.defender_action])
        yield dict(animation=i, name=row["name_zh"], units=row["units"],
                   selected_by_weapon_unit=" ".join(aliases[i]), script_offset=offset,
                   attack_action=script.action, defender_action=script.defender_action,
                   attack_routine=routine(rom, script.action),
                   defender_routine=routine(rom, script.defender_action),
                   weapon_actors=actors(rom, script),
                   hit_script=script.hit_script, hit_actors=actors(rom, hit),
                   damage_mode=mode, damage_steps=count, step_gap=gap, timeout=timeout,
                   lethal_mode=lethal, family=" ".join(families))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--bank", choices=("weapon", "hit", "reaction"), default="weapon")
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if args.bank == "weapon":
        with (ROOT / "docs/guide/data-mods/weapons.csv").open(encoding="utf-8") as f:
            rows = list(census(rom, list(csv.DictReader(f))))
    else:
        rows = [dict(index=i, offset=offset, action=s.action, defender_action=s.defender_action,
                     actors=actors(rom, s)) for i, (offset, s) in enumerate(read_animation_bank(rom, args.bank))]
    writer = csv.DictWriter(sys.stdout, fieldnames=list(rows[0]), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)


if __name__ == "__main__":
    main()
