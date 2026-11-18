#!/usr/bin/env python3
"""Starts the live check of new unit numbers (docs/design/new-unit-ids.md §10.3) and leaves the
game running for the debug interface (srw64ctl / the srw64 MCP tools):

  --launch-only        packs config/recomp/mods/final-dancouga into a fresh MOD folder, prints
                       the numbers its names get (mod_units.hpp assign, recomputed here) and
                       starts its mini stage (scene 78, Dancouga 224 and Black Wing), HD, Chinese;
  --without-package    starts with no package, optionally from a run's SRAM (--save), to load a
                       save holding the new unit: it must be turned back to the title.

The steps of §10.3 (victory, the roster, the intermission pages, save and load, the battle
viewer, the turn-back) are then driven by hand through the debug interface.
"""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/
from recomp.debug.session import ROOT, Session  # noqa: E402
sys.path.insert(0, str(ROOT / "tools"))
from mods.pack import build_package  # noqa: E402

PACKAGE = ROOT / "config/recomp/mods/final-dancouga"
STAGE = ROOT / "assets/hd-ai/final-dancouga-kit/battle/final-swap.json"
UNITS, UNIT_SIZE, UNIT_COUNT = 0x8016A210, 0x54, 140
WEAPONS, WEAPON_SIZE = 0x80178F80, 0x24
FIRST_UNIT, LAST_UNIT, FIRST_WEAPON, LAST_WEAPON = 400, 998, 1400, 2399


def fnv1a(text: str) -> int:
    h = 2166136261
    for c in text.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def place(keys: list[str], first: int, last: int) -> dict[str, int]:
    """mod_units.hpp place: by the key's hash, the next free number on a clash."""
    out: dict[str, int] = {}
    taken: set[int] = set()
    span = last - first + 1
    for h, k in sorted((fnv1a(k), k) for k in keys):
        for step in range(span):
            n = first + (h + step) % span
            if n not in taken:
                out[k] = n
                taken.add(n)
                break
    return out


def numbers(package_id: str, document: dict) -> tuple[dict[str, int], dict[str, int]]:
    units = place([f"{package_id}@{k}" for k in document.get("units", {})], FIRST_UNIT, LAST_UNIT)
    weapons = place([f"{package_id}@{k}" for k in document.get("weapons", {})], FIRST_WEAPON, LAST_WEAPON)
    return units, weapons


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--language", default="zh-Hans")
    parser.add_argument("--images", choices=("hd", "original"), default="hd")
    parser.add_argument("--launch-only", action="store_true")
    parser.add_argument("--save", type=Path, help="start from this SRAM (a run's runtime-data/saves/*.bin)")
    parser.add_argument("--without-package", action="store_true", help="no package installed: a save with its unit must turn back")
    args = parser.parse_args()
    if not args.launch_only and not args.without_package:
        parser.error("pass --launch-only or --without-package; the rest is driven through the debug interface")

    mods = ROOT / "build/recomp/debug" / time.strftime("new-units-%Y%m%dT%H%M%S")
    if args.without_package:
        mods.mkdir(parents=True)
        s = Session.launch(language=args.language, images=args.images, save=str(args.save) if args.save else None,
                           env={"SRW64_MODS_DIR": str(mods)}, detach=True)
        print("run", s.run, flush=True)
        return 0
    folder, _, _ = build_package(PACKAGE, mods, archive=False)
    manifest = json.loads((folder / "mod.json").read_text(encoding="utf-8"))
    manifest["game"] = "*"   # a development build calls itself the last release
    (folder / "mod.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    document = json.loads((folder / "data/new_units.json").read_text(encoding="utf-8"))
    units, weapons = numbers(manifest["id"], document)
    print("numbers", json.dumps({"units": units, "weapons": weapons}), flush=True)
    s = Session.launch(language=args.language, images=args.images, mini_stage=str(STAGE),
                       env={"SRW64_MODS_DIR": str(mods)}, detach=True)
    print("run", s.run, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
