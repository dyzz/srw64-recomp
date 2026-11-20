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
  steps     per behaviour that moves its actor to the scene resource next to its own (801C5700
            with +0x2C, the scene's resource number, plus k): the k it uses, from the recompiled
            code (build/recomp/cpu-bound/generated, make recomp-cpu). A package picture in such
            an actor's place needs copies of its scene around it, the sprite's "around".

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
CHANGE_SCENE = "0x801C5700"


def behaviour_steps() -> dict[int, list[int]]:
    """The k of each behaviour that calls 801C5700 with the actor's scene resource (+0x2C) plus k:
    the dispatcher 80220C0C's cases (behaviour - 3) give each routine's address, and the routine's
    listing shows the registry loaded into $a1, the addiu on it, then the call."""
    text = [f.read_text() for f in sorted(GENERATED.glob("funcs_*.c"))]
    if not text:
        raise SystemExit(f"{GENERATED}: no recompiled code (make recomp-cpu)")
    whole = next(t for t in text if "func_80220C0C(" in t and "0x80225030" in t)
    start = whole.find("RECOMP_FUNC void load_00121560_func_80220C0C(")
    dispatcher = whole[start:whole.find("\nRECOMP_FUNC", start + 10)]
    routines = {}
    for case, label in re.findall(r"case (\d+): goto (L_[0-9A-F]+); break;", dispatcher):
        m = re.search(label + r":\n(?:.*\n){0,2}?\s*ctx->r2 = S32\(0X([0-9A-F]+) << 16\);\n(?:.*\n){0,6}?\s*ctx->r2 = ADD32\(ctx->r2, (-?0X[0-9A-F]+)\);", dispatcher)
        if m:   # the 95 empty slots return 0 and have no routine
            routines[int(case) + 3] = (int(m.group(1), 16) << 16) + int(m.group(2), 16)
    steps = {}
    for behaviour, address in sorted(routines.items()):
        name = f"RECOMP_FUNC void load_00121560_func_{address:08X}("
        source = next((t for t in text if name in t), "")
        i = source.find(name)
        body = source[i:source.find("\nRECOMP_FUNC", i + 10)] if i >= 0 else ""
        listing = re.findall(r"// 0x[0-9A-F]+: (.*)", body)
        ks = set()
        for n, ins in enumerate(listing):
            if "jal" in ins and CHANGE_SCENE in ins:
                window = listing[max(0, n - 8):n + 2]
                if any(re.search(r"lhu?\s+\$a1, 0x2C\(", w) for w in window):
                    k = 0
                    for w in window:
                        m = re.search(r"addiu\s+\$a1, \$a1, (-?0x[0-9A-F]+)", w)
                        if m:
                            k = int(m.group(1), 16)
                    if k:
                        ks.add(k)
        if ks:
            steps[behaviour] = sorted(ks)
    return steps


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
    data = {"schema": "srw64.weapon-animations.v1", "records": records, "weapons": weapons,
            "scenes": scenes, "poses": poses, "cutins": list(CUTIN_REGISTRY),
            "steps": {str(k): v for k, v in behaviour_steps().items()}}
    OUT.write_text(json.dumps(data, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"{OUT}: {len(records)} records, {len(weapons)} weapons, {len(scenes)} scenes, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
