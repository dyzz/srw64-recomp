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

    .venv/bin/python tools/content/export_weapon_animations.py
"""
from __future__ import annotations

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.battle_graphics import CUTIN_REGISTRY, parse_scene, read_animation_bank, read_triplets  # noqa: E402
from srw64_rom.resources import ResourceTable  # noqa: E402

OUT = ROOT / "web/src/data/weapon-animations.json"


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
            "scenes": scenes, "poses": poses, "cutins": list(CUTIN_REGISTRY)}
    OUT.write_text(json.dumps(data, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"{OUT}: {len(records)} records, {len(weapons)} weapons, {len(scenes)} scenes, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
