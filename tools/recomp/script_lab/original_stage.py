#!/usr/bin/env python3
"""Write a mini stage that is one of the original stages as it is, to enter it directly.

The mini stage takes the scene's place (`slot`), so the game's scene number is the
stage's own and every page keyed by it (the stage info page) sees that stage. Its
map, its events (copied byte for byte, in the scene's own order) and its deployment
block are the original's. The story state is whatever the run has: entered from the
title there is no protagonist and every variable is 3, which a check writes itself
(the debug interface's memory write) before looking.

    python3 tools/recomp/script_lab/original_stage.py 25 --out build/recomp/mini-stage/scene-25.json
    python3 tools/recomp/script_lab/mini_stage.py compile build/recomp/mini-stage/scene-25.json --out scene-25.image.json
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
RECORDS = ROOT / "assets/original-data/records"


def record(collection: str, key: str) -> dict:
    for line in (RECORDS / f"{collection}.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            row = json.loads(line)
            if row["key"] == key:
                return row
    raise ValueError(f"no record {key}")


def stage(scene: int) -> dict:
    scenario = record("scenarios", f"base:scenarios:{scene:04d}")
    links = [link["key"] for link in scenario["links"]]
    stage_map = record("stage_maps", f"base:stage_maps:{scene:04d}")
    map_index = next(f["value"] for f in stage_map["fields"] if f["name"] == "初始地图编号")
    events = [{"copy_from": key} for key in links if key.startswith("base:stage_events:")]
    blocks = [key for key in links if key.startswith("base:stage_auxiliary:")]
    spec = {"schema": "srw64.mini-stage.v1", "name": f"scene-{scene}",
            "note": f"Scene {scene} ({scenario['label']}) as the original has it, entered directly (original_stage.py).",
            "map": map_index, "slot": scene, "events": events}
    if blocks:
        spec["deployments_from"] = blocks[0]
    return spec


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scene", type=int)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(stage(args.scene), ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
