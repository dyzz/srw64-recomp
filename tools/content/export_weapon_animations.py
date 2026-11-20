#!/usr/bin/env python3
"""The original's weapon animation records for the unit MOD maker's animation editor
(docs/design/weapon-animation-survey.md, web/src/lib/weapon-animation.ts): numbers only, no
pictures or sounds.

  records   every distinct record: [camera, hit, action, defender, [sounds], [[scene, x, y, z,
            h4, h5, h6, behaviour], ...]] (battle_graphics.parse_animation)
  weapons   per weapon number, its record's index (1,329; records are shared)
  scenes    per battle scene registry entry (0-1052), its canvas: [x0, y0, x1, y1, frames,
            vertex mode], the parts' box around the scene's origin, y down
  poses     per unit number, its battle pose's canvas the same way; the site's pose pictures
            (/gen/units/<unit>.webp) are that canvas, 8 times larger (HD whole-v2)
  cutins    the close-up cut-ins' registry range
  steps     per behaviour that only moves its actor to the scene resource next to its own
            (801C5700 with +0x2C, the scene's resource number, plus k): the k it uses. A package
            picture in such an actor's place needs copies of its scene around it ("around").
  complex   the behaviours that compute with scene numbers any other way: a record with one
            keeps the original's pictures (cut-ins aside).
  lines     per behaviour that shows the battle lines after the one the speaker said (8008FFAC
            with the line plus k, 石破天惊拳's shouts): the k.
            Both from the recompiled code (build/recomp/cpu-bound/generated, make recomp-cpu);
            src/host/battle_steps.hpp is written from them too.

    .venv/bin/python tools/content/export_weapon_animations.py
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.battle_graphics import CUTIN_REGISTRY, parse_scene, read_animation_bank, read_triplets  # noqa: E402
from srw64_rom.resources import ResourceTable  # noqa: E402

OUT = ROOT / "web/src/data/weapon-animations.json"
GENERATED = ROOT / "build/recomp/cpu-bound/generated"
CHANGE_SCENE = 0x801C5700
SHOW_LINE = 0x8008FFAC
LOAD_SCENE = re.compile(r"lhu?\s+\$(\w+), 0x2C\(\$(\w+)\)")
STEP = re.compile(r"(addiu\s+\$a1, \$a1, -?0x[0-9A-F]+|sll\s+\$a1, \$a1, 16|sra\s+\$a1, \$a1, 16)$")


def overlay_functions() -> dict[int, tuple[list[str], dict[int, int]]]:
    """The battle overlay's functions in the recompiled code: each one's instructions (the MIPS
    listing in its comments, once each) and the index of each address among them."""
    text = [f.read_text() for f in sorted(GENERATED.glob("funcs_*.c"))]
    if not text:
        raise SystemExit(f"{GENERATED}: no recompiled code (make recomp-cpu)")
    out = {}
    for t in text:
        for m in re.finditer(r"RECOMP_FUNC void load_00121560_func_([0-9A-F]{8})\(", t):
            end = t.find("\nRECOMP_FUNC", m.end())
            seen, lines, at = set(), [], {}
            for line in re.findall(r"// (0x[0-9A-F]+: .*)", t[m.start():end if end > 0 else None]):
                if line not in seen:
                    seen.add(line)
                    at[int(line.split(":")[0], 16)] = len(lines)
                    lines.append(line.split(": ", 1)[1])
            out[int(m.group(1), 16)] = (lines, at)
    whole = next(t for t in text if "func_80220C0C(" in t and "0x80225030" in t)
    start = whole.find("RECOMP_FUNC void load_00121560_func_80220C0C(")
    out["_dispatcher"] = whole[start:whole.find("\nRECOMP_FUNC", start + 10)]
    return out


def behaviour_scenes() -> tuple[dict[int, list[int]], list[int], dict[int, list[int]]]:
    """How each behaviour (its routine and the overlay functions it calls) uses its actor's scene
    resource number (+0x2C): `steps`, the k of each load of it straight into 801C5700 plus k (a
    jump to a shared tail followed); `complex`, the behaviours that use it any other way
    (compared, kept, another actor's). Routines that never read it are in neither. `lines`: the
    behaviours that show a battle line by its id plus k (8008FFAC), the lines after the one the
    speaker said."""
    functions = overlay_functions()
    routines = {}
    dispatcher = functions.pop("_dispatcher")
    for case, label in re.findall(r"case (\d+): goto (L_[0-9A-F]+); break;", dispatcher):
        m = re.search(label + r":\n(?:.*\n){0,2}?\s*ctx->r2 = S32\(0X([0-9A-F]+) << 16\);\n(?:.*\n){0,6}?\s*ctx->r2 = ADD32\(ctx->r2, (-?0X[0-9A-F]+)\);", dispatcher)
        if m:   # the 95 empty slots return 0 and have no routine
            routines[int(case) + 3] = (int(m.group(1), 16) << 16) + int(m.group(2), 16)
    own = {}
    for address, (lines, at) in functions.items():
        ks, other = set(), 0
        for n, ins in enumerate(lines):
            m = LOAD_SCENE.match(ins)
            if not m:
                continue
            window = lines[n + 1:n + 9]
            jump = next((i for i, w in enumerate(window) if re.match(r"j\s+L_", w)), None)
            first = next((i for i, w in enumerate(window) if w.startswith("jal")), None)
            if jump is not None and (first is None or jump < first):
                target = at.get(int(re.search(r"L_([0-9A-F]+)", window[jump]).group(1), 16))
                if target is not None:
                    window = window[:jump + 2] + lines[target:target + 4]
            call = next((i for i, w in enumerate(window) if w.startswith("jal")), None)
            uses = [w for w in window[:call + 2] if "$a1" in w and not w.startswith("j")] if call is not None else []
            if m.group(1) == "a1" and call is not None and f"0x{CHANGE_SCENE:08X}" in window[call] and all(STEP.match(w) for w in uses):
                ks.add(next((int(x, 16) for w in uses for x in re.findall(r"addiu\s+\$a1, \$a1, (-?0x[0-9A-F]+)", w)), 0))
            else:
                other += 1
        shows = set()
        for n, ins in enumerate(lines):
            if ins.startswith("jal") and f"0x{SHOW_LINE:08X}" in ins:
                shows |= {int(x, 16) for w in lines[max(0, n - 6):n + 2] for x in re.findall(r"addiu\s+\$a1, \$a1, (-?0x[0-9A-F]+)", w)}
        calls = {int(x, 16) for ins in lines for x in re.findall(r"jal\s+0x([0-9A-F]{8})", ins)} - {CHANGE_SCENE}
        own[address] = (ks, other, calls, shows)

    def reach(address, seen):
        if address in seen or address not in own:
            return set(), 0, set()
        seen.add(address)
        ks, other, calls, shows = own[address]
        ks, shows = set(ks), set(shows)
        for c in calls:
            more, worse, said = reach(c, seen)
            ks |= more
            other += worse
            shows |= said
        return ks, other, shows

    steps, complex_, line_steps = {}, [], {}
    for behaviour, address in sorted(routines.items()):
        ks, other, shows = reach(address, set())
        if other:
            complex_.append(behaviour)
        elif ks - {0}:
            steps[behaviour] = sorted(ks - {0})
        if shows - {0}:
            line_steps[behaviour] = sorted(shows - {0})
    return steps, complex_, line_steps


HEADER = ROOT / "src/host/battle_steps.hpp"


def write_steps_header(steps: dict[int, list[int]], complex_: list[int]) -> None:
    """The same as tables the host checks packages with (mod_units.hpp) and validate.py reads."""
    def wrapped(items: list[str]) -> str:
        lines, line = [], ""
        for item in items:
            if len(line) + len(item) > 96:
                lines.append(line)
                line = ""
            line += item
        lines.append(line)
        return "\n".join("    " + l for l in lines)
    rows = wrapped([f"{{{b},{-min(0, *ks)},{max(0, *ks)}}}," for b, ks in sorted(steps.items())])
    hard = wrapped([f"{b}," for b in complex_])
    HEADER.write_text(f"""#pragma once
// How the battle actor routines use their actor's scene, which is a resource number (+0x2C;
// docs/design/weapon-animation-survey.md §7.2), from the recompiled code by
// tools/content/export_weapon_animations.py (written there; tests/test_unit_maker.py keeps it the
// same as web/src/data/weapon-animations.json).
// - table: the routines that only move their actor to the scene resource k before or after its
//   own (801C5700 with +0x2C plus k), how far back and forward. A package picture on such an
//   actor needs copies of its scene that far around it (a sprite's "around", mod_units.hpp), or
//   the game reads the picture's atlas as a scene and crashes.
// - complex: the routines that compute with scene numbers any other way (compare them, keep them,
//   read another actor's). A record with one of these keeps the original's pictures: a package
//   picture anywhere in it (but a cut-in's) is refused.
#include <cstdint>
namespace srw64::battle_steps {{
struct Reach {{ uint16_t behaviour; uint8_t before, after; }};
inline constexpr Reach table[]={{
{rows}
}};
inline constexpr uint16_t complex[]={{
{hard}
}};
inline const Reach* of(unsigned behaviour) {{
    for(const auto& r:table)if(r.behaviour==behaviour)return &r;
    return nullptr;
}}
inline bool is_complex(unsigned behaviour) {{
    for(const auto b:complex)if(b==behaviour)return true;
    return false;
}}
}}
""", encoding="utf-8")


def main() -> None:
    rom = (ROOT / "rom.z64").read_bytes()
    table = ResourceTable(rom)
    canvas: dict[int, list[int]] = {}

    def box(scene_id: int) -> list[int]:
        if scene_id not in canvas:
            scene = parse_scene(table.extract(scene_id)[0])
            canvas[scene_id] = [*scene.bounds(), len(scene.frames), scene.vertex_mode]
        return canvas[scene_id]

    records, index, weapons = [], {}, []
    for offset, s in read_animation_bank(rom, "weapon"):
        if offset not in index:
            index[offset] = len(records)
            records.append([s.camera, s.hit_script, s.action, s.defender_action, list(s.sounds),
                            [[a.registry, a.x, a.y, a.z, a.h4, a.h5, a.h6, a.behavior] for a in s.actors]])
        weapons.append(index[offset])
    scenes = [box(t[0]) if t[0] else [0, 0, 1, 1, 0, 0] for t in read_triplets(rom, "battle_scenes")]
    poses = [box(t[0]) if t[0] else [0, 0, 1, 1, 0, 0] for t in read_triplets(rom, "unit_poses")]
    steps, complex_, line_steps = behaviour_scenes()
    data = {"schema": "srw64.weapon-animations.v1", "records": records, "weapons": weapons,
            "scenes": scenes, "poses": poses, "cutins": list(CUTIN_REGISTRY),
            "steps": {str(k): v for k, v in steps.items()}, "complex": complex_,
            "lines": {str(k): v for k, v in line_steps.items()}}
    write_steps_header(steps, complex_)
    OUT.write_text(json.dumps(data, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"{OUT}: {len(records)} records, {len(weapons)} weapons, {len(scenes)} scenes, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
