#!/usr/bin/env python3
"""What the level editor page (web/src/pages/[lang]/mods/editor.astro) shows of the original
game, so that its author needs no ROM: the same kind of export as the Library's. Also
web/src/data/script-ops.json (kept in git), the instruction table the page's compiler uses.

    .venv/bin/python web/scripts/export_editor.py

Reads rom.z64 and the text export (assets/text-export) and writes, under web/public/gen
(git-ignored):

  maps/<map>.png     each of the 158 tactical maps as the original draws it, at its own size
  editor.json        the maps (size, terrain of every cell, whether a campaign's own map may
                     stand in for it and, if so, what tools/mods/map_tool.py needs of it:
                     its three resources' numbers, its palette, the indices its cycles
                     rewrite) and the terrain table (name, defence, hit, recovery, the six
                     movement costs: 802196D0, docs/design/custom-campaign.md §6.1)

Files newer than the ROM are kept.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
sys.path.insert(0, str(ROOT / "tools" / "mods"))
sys.path.insert(0, str(ROOT / "tools" / "content"))
import map_tool  # noqa: E402
from export_audio_tables import LOCALES, plain  # noqa: E402
from srw64_native.original_images import decode_indexed, decode_map  # noqa: E402
from srw64_rom.resources import ResourceTable  # noqa: E402

GEN = ROOT / "web/public/gen"
ROM = ROOT / "rom.z64"
TERRAIN, TERRAINS, TERRAIN_SIZE = 0x102230, 100, 11
OPS = ROOT / "web/src/data/script-ops.json"


def export_ops() -> None:
    """The script instructions the page's compiler (web/src/lib/compiler/assemble.ts) assembles
    with: every opcode's operand count and name, from the layout lock (config/data). Kept in
    git, as the site's other data tables are."""
    spec = json.loads((ROOT / "config/data/original-jp-v1.json").read_text(encoding="utf-8"))["stage_scripts"]
    ops = {}
    for family, key in (("command", "commands"), ("condition", "conditions")):
        for code, row in spec[key].items():
            ops.setdefault(code, {"family": family, "words": row["operand_words"], "name": row["name"]})
    for code, row in spec["context_markers"].items():
        ops.setdefault(code, {"family": "marker", "words": 0, "name": row["name"]})
    text = json.dumps({"schema": "srw64.web-script-ops.v1", "ops": dict(sorted(ops.items()))}, ensure_ascii=False, indent=1) + "\n"
    if not OPS.exists() or OPS.read_text(encoding="utf-8") != text:
        OPS.write_text(text, encoding="utf-8")
    print(f"script-ops.json: {len(ops)} instructions")


OBJECTIVE_FIRST = 5567   # the winning texts; the losing ones from 5593
PART_NAMES = 1129        # text of part 0
TITLES = 281             # a scene's title: 281 + scene
PLACE_TABLE, PLACES = 0xAABD0, 127   # ROM of the world map overlay's 801C5310 (0xA7EC0 + 0x2D10), 6 bytes a place
REGIONS = {13: {"ja": "地球", "zh": "地球", "en": "Earth", "ko": "지구"}, 17: {"ja": "地球", "zh": "地球", "en": "Earth", "ko": "지구"},
           18: {"ja": "ヨーロッパ・地中海", "zh": "欧洲、地中海", "en": "Europe and the Mediterranean", "ko": "유럽·지중해"},
           19: {"ja": "中央アジア・インド", "zh": "中亚、印度", "en": "Central Asia and India", "ko": "중앙아시아·인도"},
           20: {"ja": "北アメリカ", "zh": "北美", "en": "North America", "ko": "북아메리카"},
           14: {"ja": "宇宙", "zh": "宇宙", "en": "Space", "ko": "우주"}}


def main() -> int:
    rom = ROM.read_bytes()
    table = ResourceTable(rom)
    names = {}
    with (ROOT / "assets/text-export/records.jsonl").open(encoding="utf-8") as f:
        for line in f:
            r = json.loads(line)
            if r.get("table") == 0 and (r["id"] < 64 or OBJECTIVE_FIRST <= r["id"] < OBJECTIVE_FIRST + 0x80 or r.get("category") == "names.part"
                                        or TITLES <= r["id"] < TITLES + 143):
                names[r["id"]] = r
    korean = {int(e["key"][-5:]): plain(e["target"]) for e in
              json.loads((ROOT / "content/locales/ko.json").read_text(encoding="utf-8"))["entries"]}

    def name(record: int) -> dict:
        r = names[record]
        ja = plain(r["display"])
        zh, en = (plain(r.get("existing", {}).get(locale, "")) for locale in LOCALES)
        return {"ja": ja, "zh": zh or ja, "en": en or ja, "ko": korean.get(record) or en or ja}

    terrain = []
    for number in range(TERRAINS):
        row = rom[TERRAIN + number * TERRAIN_SIZE:TERRAIN + (number + 1) * TERRAIN_SIZE]
        terrain.append({"id": number, "name": name(row[0]), "defense": row[1], "hit": struct.unpack("b", row[2:3])[0],
                        "hp": row[3], "en": row[4], "move": list(row[5:11])})

    # The map menu's 作戦目的 window (801C688C) shows text 5567 + the stage's winning number
    # and 5593 + its losing number, as 3D65 sets them (engine+0x996, 7 bits each).
    objectives = [{"id": i, **name(i)} for i in sorted(names) if names[i].get("category") == "story.condition"]
    # The parts an enemy may leave when defeated (deployment record +0x12): text 1129 + part.
    parts = [{"id": i - PART_NAMES, **name(i)} for i in sorted(names) if names[i].get("category") == "names.part"]

    # The world map's places (3D32 / 3D33, table 801C5310: surface, x, y): which surface, and
    # the original's stages that show or travel to each, so the page can say where it is.
    used: dict[int, set[int]] = {}
    with (ROOT / "assets/original-data/records/stage_events.jsonl").open(encoding="utf-8") as f:
        for line in f:
            script = json.loads(line).get("script") or {}
            for ins in script.get("instructions", []):
                if ins["opcode"] in (0x3D32, 0x3D33) and ins["operands"]:
                    used.setdefault(ins["operands"][0], set()).update(
                        s for s in script.get("scenes", []) if 0 < s < 143 and TITLES + s in names and plain(names[TITLES + s]["display"]) != "リザーブ")
    places = []
    for number in range(PLACES):
        surface, x, y = struct.unpack_from(">3h", rom, PLACE_TABLE + number * 6)
        places.append({"id": number, "surface": surface, "region": REGIONS.get(surface, REGIONS[13]), "x": x, "y": y,
                       "scenes": sorted(used.get(number, ()))[:4]})
    titles = {str(scene): name(TITLES + scene) for scene in sorted({s for p in places for s in p["scenes"]}) if TITLES + scene in names}

    (GEN / "maps").mkdir(parents=True, exist_ok=True)
    maps, drawn = [], 0
    sums: dict[int, list[int]] = {}      # terrain -> [r, g, b, cells]: what each looks like in the original's maps
    for number in range(map_tool.MAPS):
        layout_id, atlas_id, palette_id, _, _, mode, music = struct.unpack_from(">5H2B", rom, map_tool.MAP_RECORDS + number * 12)
        layout = table.extract(layout_id)[0]
        _, _, width, height = struct.unpack_from(">4H", layout)
        columns, rows = width // 2, height // 2
        cells = bytes(layout[8 + i * 4 + 1] for i in range(columns * rows))
        entry = {"map": number, "columns": columns, "rows": rows, "mode": mode, "space": rom[map_tool.SPACE_FLAGS + number],
                 "music": music, "terrain": cells.hex()}
        if mode == 0:
            like = map_tool.template(rom, number)
            entry["template"] = {"map": number, "layout": like["layout"], "atlas": like["atlas"], "palette": like["palette"],
                                 "palette_hex": like["palette_bytes"].hex(), "cycles": like["cycles"],
                                 "space": like["space"], "music": like["music"]}
        maps.append(entry)
        out = GEN / "maps" / f"{number}.png"
        if not out.exists() or out.stat().st_mtime < ROM.stat().st_mtime:
            image, _ = decode_map(layout, decode_indexed(table.extract(atlas_id)[0], table.extract(palette_id)[0]))
            image.convert("RGB").save(out, optimize=True)
            drawn += 1
        if mode == 0 and not rom[map_tool.SPACE_FLAGS + number]:
            from PIL import Image
            with Image.open(out) as picture:
                small = picture.convert("RGB").resize((columns, rows), Image.Resampling.BOX).tobytes()
            for at, t in enumerate(cells):
                if t < 99:
                    acc = sums.setdefault(t, [0, 0, 0, 0])
                    for k in range(3):
                        acc[k] += small[at * 3 + k]
                    acc[3] += 1
    # The colour a terrain mostly has on the ground maps (space ones left out): the page's
    # first guess for a picture's cells: [r, g, b, cells seen]. Only terrains seen on 200 cells or more.
    colours = {str(t): [*(round(v / acc[3]) for v in acc[:3]), acc[3]] for t, acc in sorted(sums.items()) if acc[3] >= 200}
    (GEN / "editor.json").write_text(json.dumps({"schema": "srw64.web-editor.v1", "maps": maps, "terrain": terrain, "objectives": objectives, "parts": parts,
                                                "places": places, "titles": titles,
                                                "terrain_colours": colours},
                                                ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    export_ops()
    print(f"editor.json: {len(maps)} maps ({sum('template' in m for m in maps)} may be stood in for), {len(terrain)} terrains; {drawn} map pictures drawn")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
