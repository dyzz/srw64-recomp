#!/usr/bin/env python3
"""Compile a campaign written stage by stage (docs/design/level-editor.md §3) into what
the game plays.

The source is a folder: campaign.json (schema srw64.campaign.v2: identity, the stages in
order, where it starts) and stages/<key>.json (schema srw64.stage.v1), each a stage in
four parts: its map, its units, its events and where it leads. Events are written as a
story MOD's are (when / if / do), without scene numbers: the compiler gives every stage a
scene of the original to borrow.

Two files come out, both under the package's campaign/ folder:

- campaign.json (srw64.campaign-image.v2): every stage's image for the game's own
  per-scene buffers (the opening, set-up and end events, the deployment block, the map);
- story.json (srw64.story-image.v1 with "campaign"): every other event, which the host
  triggers itself and runs outside the original's 63 events and 0x1A00 bytes, the
  campaign's new lines, and where those lines and the steps' conditions go in the
  opening, set-up and end events.

The compiler is the level editor's TypeScript (web/src/lib/compiler/campaign.ts), run with
node (web_compiler.py); this file hands it the folder and builds the campaign's own maps
(map_tool.py, the same compiler's map.ts) with the ROM's template maps.

    python tools/mods/stage.py compile <source folder> -o <out folder>
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools"))
import map_tool  # noqa: E402
import web_compiler  # noqa: E402
from story import StoryError  # noqa: E402

CAMPAIGN_SCHEMA = "srw64.campaign.v2"
STAGE_SCHEMA = "srw64.stage.v1"
IMAGE_SCHEMA = "srw64.campaign-image.v2"


def catalog(source: Path) -> dict:
    """The original records the stages copy (units.copy_from, a unit's template, raw_events'
    copy_from), by collection and key, from the extracted records (assets/original-data).
    A key that is not there is left out: the compiler says so."""
    wanted: dict[str, set[str]] = {"stage_auxiliary": set(), "stage_deployments": set(), "stage_events": set()}
    for path in sorted((source / "stages").glob("*.json")):
        try:
            stage = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        units = stage.get("units") if isinstance(stage, dict) else None
        if isinstance(units, dict):
            if isinstance(units.get("copy_from"), str):
                wanted["stage_auxiliary"].add(units["copy_from"])
            for row in units.get("place") or []:
                if isinstance(row, dict) and isinstance(row.get("template"), str):
                    wanted["stage_deployments"].add(row["template"])
        for row in (stage.get("raw_events") or []) if isinstance(stage, dict) else []:
            if isinstance(row, dict) and isinstance(row.get("copy_from"), str):
                wanted["stage_events"].add(row["copy_from"])
    if not any(wanted.values()):
        return {}
    from recomp.script_lab import mini_stage
    out: dict[str, dict[str, str]] = {}
    for collection, keys in wanted.items():
        if keys:
            index = mini_stage.catalog_index(collection)
            out[collection] = {key: index[key]["raw_hex"] for key in sorted(keys) if key in index}
    return out


def compile_campaign(source: Path, rom: Path | None = None, templates=None) -> tuple[dict, dict | None, dict[str, dict]]:
    """The campaign image, its story image (None when no stage needs one) and the maps of
    its own that its stages use, by name, as map_tool.build makes them (the ROM is read
    only for those)."""
    try:
        records = catalog(source)
    except ValueError as error:
        raise StoryError(str(error)) from None
    with tempfile.TemporaryDirectory() as temp:
        extra = []
        if records:
            (Path(temp) / "catalog.json").write_text(json.dumps(records))
            extra = [Path(temp) / "catalog.json"]
        try:
            out = web_compiler.run("compile-campaign.ts", source, *extra)
        except web_compiler.CompileError as error:
            raise StoryError(str(error)) from None
    maps: dict[str, dict] = {}
    for stage in out["campaign"]["stages"]:
        name = stage["image"].get("custom_map")
        if name and name not in maps:
            try:
                if templates is None:
                    templates = map_tool.Templates(rom=map_tool.rom_path(rom).read_bytes())
                maps[name] = map_tool.build(source / "maps" / name, templates)
            except map_tool.MapError as error:
                raise StoryError(f"地图 {name}：{error}") from None
    return out["campaign"], out["story"], maps


def is_source(source: Path) -> bool:
    try:
        return json.loads((source / "campaign.json").read_text(encoding="utf-8")).get("schema") == CAMPAIGN_SCHEMA
    except (OSError, ValueError, AttributeError):
        return False


def compile_into(source: Path, out: Path, rom: Path | None = None, previews: bool = False, templates=None) -> tuple[dict, dict | None]:
    """The two images, and the campaign's own maps under maps/, written into out/ (the
    package's campaign/ folder)."""
    campaign, events, maps = compile_campaign(source, rom, templates)
    out.mkdir(parents=True, exist_ok=True)
    if (out / "maps").exists():
        shutil.rmtree(out / "maps")
    for name, built in maps.items():
        print(map_tool.summary(map_tool.write(built, out / "maps" / name, preview=previews)))
    (out / "campaign.json").write_text(json.dumps(campaign, ensure_ascii=False) + "\n", encoding="utf-8")
    if events is not None:
        (out / "story.json").write_text(json.dumps(events, ensure_ascii=False) + "\n", encoding="utf-8")
    elif (out / "story.json").exists():
        (out / "story.json").unlink()
    return campaign, events


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compile")
    c.add_argument("source", type=Path, help="the folder with campaign.json and stages/")
    c.add_argument("-o", "--out", type=Path, required=True, help="where campaign.json, story.json and maps/ go")
    c.add_argument("--rom", type=Path, help="the ROM, for a campaign with maps of its own (default: SRW64_ROM, rom.z64)")
    args = parser.parse_args(argv)
    try:
        campaign, events = compile_into(args.source, args.out, args.rom, previews=True)
    except StoryError as error:
        print(f"{args.source}: {error}", file=sys.stderr)
        return 1
    for stage in campaign["stages"]:
        image = stage["image"]
        hosted = [e["id"] for e in (events or {"events": []})["events"] if e["scene"] == stage["scene"]]
        print(f"{stage['key']}: 场景 {stage['scene']}，地图 {image['map']}，原版事件位 {len(image['events'])} 个 "
              f"{len(image['events_hex']) // 2} 字节，宿主触发 {len(hosted)} 个，单位 {len(image['deployments'])} 个")
    print(f"{args.out}: {len(campaign['stages'])} 关，{len((events or {'texts': []})['texts'])} 句新台词")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
