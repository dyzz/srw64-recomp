#!/usr/bin/env python3
"""Export original graphics into one organized, named local folder.

Units get a folder each (battle pose, sprite sheet, map icon, stats, weapon
animation parts); portraits, battle effects, cut-in movies, chapter titles
and maps get their own sections. Every image comes from a pinned ROM table
(see battle_graphics.TRIPLET_TABLES and the layout lock's ``images``), never
from resource adjacency. Output is local-only under assets/.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import asdict
import csv
import html
from io import StringIO
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))

from srw64_native.battle_graphics import (ANIMATION_BANKS, BLANK_FRAME, CUTIN_REGISTRY, MAP_MOVIE_BLOCK,
                                          MAP_MOVIES, TRIPLET_TABLES, animated_png, decode_atlas, frame_strip,
                                          parse_scene, read_animation_bank, read_battle_units, read_battle_weapons,
                                          read_map_movies, read_triplets, render_scene)
from srw64_native.catalog import sha, source_catalog, text_key
from srw64_native.original_data import check_layout, extract_gameplay
from srw64_native.original_images import decode_indexed, decode_map, png_bytes
from srw64_rom.resources import ResourceTable

SCHEMA = "srw64.original-graphics.v1"
# Preview speed only: one step tick is shown as 1/30 s until the player's tick is traced.
TICK_MS = 33


def safe_name(text: str) -> str:
    text = text.replace("<END>", "").replace("<BR>", " ").strip()
    return re.sub(r'[\\/:*?"<>|\s]+', "_", text) or "unnamed"


class Exporter:
    def __init__(self, rom: bytes, out: Path):
        self.rom = rom
        self.out = out
        self.resources = ResourceTable(rom)
        self.cache: dict[int, bytes] = {}
        self.atlases: dict[tuple[int, int], tuple] = {}
        self.files: list[dict] = []
        self.gallery: dict[str, list[dict]] = defaultdict(list)
        self.strips: dict[str, str] = {}  # animation path -> static frame strip

    def resource(self, rid: int) -> bytes:
        if rid not in self.cache:
            self.cache[rid] = self.resources.extract(rid)[0]
        return self.cache[rid]

    def atlas(self, aid: int, pid: int):
        if (aid, pid) not in self.atlases:
            self.atlases[(aid, pid)] = decode_atlas(self.resource(aid), self.resource(pid))
        return self.atlases[(aid, pid)]

    def write(self, path: str, data: bytes, kind: str, sources: list[int], **extra) -> str:
        target = self.out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        self.files.append({"path": path, "kind": kind, "sha256": sha(data), "resources": sources, **extra})
        return path

    def write_image(self, path: str, image, kind: str, sources: list[int], **extra) -> str:
        return self.write(path, png_bytes(image), kind, sources,
                          width=image.width, height=image.height, **extra)

    def write_scene(self, stem: str, triplet: tuple[int, int, int], kind: str, binding: dict) -> dict:
        """APNG of the step list plus a strip of every distinct frame; returns a gallery item."""
        sid, aid, pid = triplet
        scene = parse_scene(self.resource(sid))
        atlas, extra_colours = self.atlas(aid, pid)
        images, clipped = render_scene(scene, atlas)
        x0, y0, _, _ = scene.bounds()
        meta = {"binding": binding, "frames": len(scene.frames), "steps": [list(s) for s in scene.steps],
                "loop_step": scene.loop_step, "vertex_mode": scene.vertex_mode,
                "origin": [-x0, -y0], "clipped_parts": clipped, "palette_colours_unused": extra_colours}
        path = self.write(f"{stem}.png", animated_png(scene, images, TICK_MS), kind, list(triplet),
                          width=images[0].width, height=images[0].height, **meta)
        strip = None
        if len(images) > 1:
            strip = self.write_image(f"{stem}-frames.png", frame_strip(images), kind + "-frames", list(triplet))
            self.strips[path] = strip
        blanks = sum(1 for f, _ in scene.steps if f == BLANK_FRAME)
        return {"path": path, "strip": strip, "frames": len(scene.frames), "steps": len(scene.steps),
                "blank_steps": blanks, "scene": sid, "atlas": aid, "palette": pid}

    def write_sheet(self, path: str, aid: int, pid: int, kind: str) -> str:
        image, extra = self.atlas(aid, pid)
        if not any(f["path"] == path for f in self.files):
            self.write_image(path, image, kind, [aid, pid], palette_colours_unused=extra)
        return path


def export(rom_path: Path, out: Path) -> dict:
    rom = rom_path.read_bytes()
    layout = json.loads((ROOT / "config/data/original-jp-v1.json").read_text())
    check_layout(rom, layout)
    sources, _, _ = source_catalog(ROOT, rom_path)
    gameplay = extract_gameplay(rom, layout, sources)
    names = layout["names"]
    text = lambda index: sources[text_key(0, index)].replace("<END>", "").replace("<BR>", " ")

    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        manifest_path = out / "manifest.json"
        if not manifest_path.exists() or json.loads(manifest_path.read_text()).get("schema") != SCHEMA:
            raise ValueError(f"Refusing to replace {out}: not a generated graphics export")
    temporary = Path(tempfile.mkdtemp(prefix=".original-graphics-", dir=out.parent))
    try:
        ex = Exporter(rom, temporary)
        coverage = build(ex, layout, gameplay, names, text)
        manifest = {"schema": SCHEMA, "rom_sha256": layout["rom_sha256"], "tick_ms_preview": TICK_MS,
                    "tables": {k: {**v, "rom_offset": f"0x{v['rom_offset']:X}"} for k, v in TRIPLET_TABLES.items()},
                    "map_movies": {"block": {**MAP_MOVIE_BLOCK, "rom_offset": f"0x{MAP_MOVIE_BLOCK['rom_offset']:X}"},
                                   "movies": [{"movie": m, "rom_offset": f"0x{o:X}", "count": c, "title": t,
                                               "trigger": g} for m, o, c, t, g in MAP_MOVIES]},
                    "layout_images": {k: layout["images"][k] for k in ("actors", "units")},
                    "coverage": coverage, "files": ex.files}
        (temporary / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n")
        (temporary / "index.html").write_text(gallery_html(ex.gallery, ex.strips))
        if out.exists():
            shutil.rmtree(out)
        temporary.rename(out)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return coverage


def build(ex: Exporter, layout: dict, gameplay: dict, names: dict, text) -> dict:
    rom = ex.rom
    coverage: dict[str, int] = defaultdict(int)
    units = gameplay["units"]
    weapons = gameplay["weapons"]
    weapon_name = lambda w: weapons[w]["label"]
    unit_dirs = {i: f"units/{i:03d}-{safe_name(row['label'])}" for i, row in enumerate(units)}

    poses = read_triplets(rom, "unit_poses")
    atlas_units: dict[int, list[int]] = defaultdict(list)
    for uid, (sid, aid, pid) in enumerate(poses[:len(units)]):
        atlas_units[aid].append(uid)
    registry = read_triplets(rom, "battle_scenes")
    scripts = {name: read_animation_bank(rom, name) for name in ANIMATION_BANKS}
    weapon_users: dict[int, list[int]] = defaultdict(list)  # registry index -> weapon ids
    for wid, (_, script) in enumerate(scripts["weapon"]):
        for actor in script.actors:
            if wid not in weapon_users[actor.registry]:
                weapon_users[actor.registry].append(wid)

    # Battle scene registry (load_00121560 reads ROM 0x11E3D0 + id*6). Scenes on a
    # unit's own atlas go to that unit's folder, close-up cut-ins to cutins/<weapon>/,
    # effects and everything else to battle-scenes/atlas-AAAA/.
    placed: dict[tuple[int, int, int], dict] = {}
    for bid, triplet in enumerate(registry):
        if triplet[0]:
            placed.setdefault(triplet, {"indices": [], "paths": {}})["indices"].append(bid)
    paths: dict[int, dict] = {}  # registry index -> {"unit": {uid: path}, "any": path}
    for triplet, info in sorted(placed.items(), key=lambda kv: kv[1]["indices"][0]):
        sid, aid, pid = triplet
        binding = {"table": "battle_scenes", "indices": info["indices"]}
        stem = f"{sid:04d}" + ("" if all(t[2] == pid for t in placed if t[:2] == (sid, aid)) else f"-p{pid:04d}")
        users = sorted({w for bid in info["indices"] for w in weapon_users[bid]})
        found = {"unit": {}, "any": None}
        if atlas_units.get(aid):
            for uid in atlas_units[aid]:
                item = ex.write_scene(f"{unit_dirs[uid]}/animations/{stem}", triplet, "battle-scene", binding)
                found["unit"][uid] = item["path"]
        elif any(CUTIN_REGISTRY[0] <= bid <= CUTIN_REGISTRY[1] for bid in info["indices"]):
            for folder in ([f"cutins/{w:04d}-{safe_name(weapon_name(w))}" for w in users]
                           or ["cutins/unreferenced"]):
                item = ex.write_scene(f"{folder}/{stem}", triplet, "battle-cutin", binding)
                ex.write_sheet(f"{folder}/sheets/{aid:04d}-p{pid:04d}.png", aid, pid, "cutin-sheet")
                ex.gallery["cutins"].append({**item, "group": folder, "weapons": users})
                found["any"] = found["any"] or item["path"]
            coverage["battle_cutin_scenes"] += 1
        else:
            folder = f"battle-scenes/atlas-{aid:04d}"
            ex.write_sheet(f"{folder}/sheet-p{pid:04d}.png", aid, pid, "battle-sheet")
            item = ex.write_scene(f"{folder}/{stem}", triplet, "battle-scene", binding)
            ex.gallery["battle"].append({**item, "group": folder, "weapons": users})
            found["any"] = item["path"]
        found["any"] = found["any"] or next(iter(found["unit"].values()))
        for bid in info["indices"]:
            paths[bid] = found
        coverage["battle_scene_triplets"] += 1

    def describe(script, uid=None) -> dict:
        actors = []
        for actor in script.actors:
            sid, aid, pid = registry[actor.registry]
            where = paths.get(actor.registry)
            file = where and (where["unit"].get(uid) or where["any"])
            actors.append({**asdict(actor), "scene": sid, "atlas": aid, "palette": pid, "file": file})
        return {"camera": script.camera, "hit_script": script.hit_script, "action": script.action,
                "defender_action": script.defender_action, "sounds": list(script.sounds), "actors": actors}

    # Map icons: layout lock binding (ROM 0x100D78, u16 per unit, palette 1010).
    icon_spec = layout["images"]["units"]
    icon_raw = rom[icon_spec["rom_offset"]:icon_spec["rom_offset"] + icon_spec["stride"] * icon_spec["count"]]
    if sha(icon_raw) != icon_spec["sha256"]:
        raise ValueError("Unit icon table changed")
    battle_weapons = read_battle_weapons(rom)
    battle_units = read_battle_units(rom)
    unit_rows = []
    for uid, row in enumerate(units):
        folder = unit_dirs[uid]
        item = {"id": uid, "name": row["label"], "folder": folder, "weapons": []}
        icon_id = struct.unpack_from(">H", icon_raw, uid * 2)[0]
        item["icon"] = ex.write_image(f"{folder}/map-icon.png",
                                      decode_indexed(ex.resource(icon_id), ex.resource(icon_spec["palette_resource"])),
                                      "unit-map-icon", [icon_id, icon_spec["palette_resource"]],
                                      binding={"table": "layout images.units", "index": uid})
        sid, aid, pid = poses[uid]
        pose = ex.write_scene(f"{folder}/battle", poses[uid], "unit-battle-pose",
                              {"table": "unit_poses", "index": uid})
        item["battle"] = pose["path"]
        item["sheet"] = ex.write_sheet(f"{folder}/battle-sheet.png", aid, pid, "unit-battle-sheet")
        item["shares_sheet_with"] = [u for u in atlas_units[aid] if u != uid]
        fields = {f["name"]: f["value"] for f in row["fields"]}
        weapon_ids = [r["weapon_id"] for r in row.get("weapon_list", {}).get("rows", [])]
        weapon_details = []
        for w in weapon_ids:
            offset, script = scripts["weapon"][w]
            animation = {"script_rom_offset": f"0x{ANIMATION_BANKS['weapon']['bank'] + offset:X}",
                         **describe(script, uid)}
            if 0 <= script.hit_script < len(scripts["hit"]):
                animation["hit_effect"] = describe(scripts["hit"][script.hit_script][1], uid)
            weapon_details.append({"id": w, "name": weapon_name(w),
                                   "stats": {f["name"]: f["value"] for f in weapons[w]["fields"]},
                                   "markers": [m["token"] for m in weapons[w].get("weapon_traits", {}).get("markers", [])],
                                   "battle_record": battle_weapons[w], "animation": animation})
            files = list(dict.fromkeys(a["file"] for a in animation["actors"] if a["file"]))
            item["weapons"].append({"id": w, "name": weapon_name(w), "files": files})
        detail = {"id": uid, "name": row["label"], "stats": fields,
                  "battle_pose": {"scene": sid, "atlas": aid, "palette": pid},
                  "map_icon": {"image": icon_id, "palette": icon_spec["palette_resource"]},
                  "battle_record": battle_units[uid] if uid < len(battle_units) else None,
                  "weapons": weapon_details}
        ex.write(f"{folder}/unit.json", (json.dumps(detail, ensure_ascii=False, indent=1) + "\n").encode(),
                 "unit-detail", [])
        unit_rows.append((uid, row["label"], fields, weapon_ids, sid, aid, pid, icon_id))
        ex.gallery["units"].append(item)
        coverage["units"] += 1

    banks = {name: [{"index": i, "rom_offset": f"0x{ANIMATION_BANKS[name]['bank'] + off:X}",
                     **({"weapon": weapon_name(i)} if name == "weapon" else {}), **describe(script)}
                    for i, (off, script) in enumerate(entries)] for name, entries in scripts.items()}
    ex.write("animations.json", (json.dumps(banks, ensure_ascii=False, indent=1) + "\n").encode(),
             "animation-scripts", [])

    # Map movies (combination / change / Aura Road), in the order their step functions load them.
    for movie, title, trigger, triplets in read_map_movies(rom):
        folder = f"movies/{movie:02d}-{safe_name(title)}"
        items = []
        for order, triplet in enumerate(triplets):
            item = ex.write_scene(f"{folder}/{order:02d}-{triplet[0]:04d}", triplet, "movie",
                                  {"table": "map_movies", "movie": movie, "order": order})
            ex.write_sheet(f"{folder}/sheets/{triplet[1]:04d}-p{triplet[2]:04d}.png",
                           triplet[1], triplet[2], "movie-sheet")
            items.append(item)
        ex.gallery["movies"].append({"movie": movie, "title": title, "trigger": trigger, "scenes": items})
        coverage["map_movie_scenes"] += len(items)

    for index, triplet in enumerate(read_triplets(rom, "chapter_titles")):
        item = ex.write_scene(f"chapter-titles/{index:03d}", triplet, "chapter-title",
                              {"table": "chapter_titles", "index": index})
        item["index"] = index
        ex.gallery["chapter-titles"].append(item)
        coverage["chapter_titles"] += 1

    # Portraits: layout lock binding (ROM 0x84220, u16 image + u16 palette per actor).
    spec = layout["images"]["actors"]
    raw = rom[spec["rom_offset"]:spec["rom_offset"] + spec["stride"] * spec["count"]]
    if sha(raw) != spec["sha256"]:
        raise ValueError("Portrait table changed")
    for aid in range(spec["count"]):
        rid, pid = struct.unpack_from(">2H", raw, aid * 4)
        short, full = text(names["actor_short_base"] + aid), text(names["actor_full_base"] + aid)
        path = ex.write_image(f"portraits/{aid:03d}-{safe_name(full or short)}.png",
                              decode_indexed(ex.resource(rid), ex.resource(pid)), "portrait", [rid, pid],
                              binding={"table": "layout images.actors", "index": aid}, name=full, short_name=short)
        ex.gallery["portraits"].append({"path": path, "index": aid, "name": full, "short": short,
                                        "image": rid, "palette": pid})
        coverage["portraits"] += 1

    # Static battle maps (ROM 0x10267C, layout/atlas/palette per map asset).
    for index, row in enumerate(gameplay["map_assets"]):
        lid, tid, pid = struct.unpack_from(">3H", bytes.fromhex(row["raw_hex"]))
        image, _ = decode_map(ex.resource(lid), decode_indexed(ex.resource(tid), ex.resource(pid)))
        path = ex.write_image(f"maps/{index:03d}.png", image, "map", [lid, tid, pid],
                              binding={"table": "map_assets", "index": index})
        ex.gallery["maps"].append({"path": path, "index": index})
        coverage["maps"] += 1

    write_tables(ex, unit_rows, weapons)
    return dict(coverage)


def write_tables(ex: Exporter, unit_rows: list, weapons: list) -> None:
    def csv_bytes(header, rows) -> bytes:
        stream = StringIO()
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)
        return ("﻿" + stream.getvalue()).encode()  # BOM so spreadsheet apps read UTF-8

    stat_names = list(unit_rows[0][2])
    ex.write("units.csv", csv_bytes(["id", "name", *stat_names, "weapons", "pose_scene", "atlas", "palette",
                                     "map_icon", "folder"],
                                    [[uid, name, *[f[k] for k in stat_names], " / ".join(weapons[w]["label"] for w in ws),
                                      sid, aid, pid, icon, ex.gallery["units"][uid]["folder"]]
                                     for uid, name, f, ws, sid, aid, pid, icon in unit_rows]), "table", [])
    weapon_stats = [f["name"] for f in weapons[0]["fields"]]
    ex.write("weapons.csv", csv_bytes(["id", "name", "markers", *weapon_stats],
                                      [[i, w["label"], " ".join(m["token"] for m in w.get("weapon_traits", {}).get("markers", [])),
                                        *[f["value"] for f in w["fields"]]] for i, w in enumerate(weapons)]), "table", [])


def gallery_html(gallery: dict, strips: dict) -> str:
    """Static page; animations show their frame strip and link to the APNG."""
    esc = html.escape

    def img(path, title="", cls=""):
        return f'<img loading="lazy" src="{esc(strips.get(path, path))}" title="{esc(title)}" class="{cls}">'

    sections = []
    cards = []
    for u in gallery["units"]:
        rows = "".join(f'<div class="weapon"><span>{w["id"]} {esc(w["name"])}</span>'
                       + "".join(f'<a href="{esc(f)}">{img(f, "", "anim")}</a>' for f in w["files"]) + "</div>"
                       for w in u["weapons"])
        shared = f'<div class="note">共用图集：{", ".join(map(str, u["shares_sheet_with"]))}</div>' if u["shares_sheet_with"] else ""
        cards.append(f'<div class="card unit" id="unit-{u["id"]}"><div class="head">{img(u["icon"], "", "icon")}'
                     f'<b>{u["id"]:03d}</b> {esc(u["name"])}</div>'
                     f'<a href="{esc(u["battle"])}">{img(u["battle"], "battle pose", "pose")}</a>{shared}'
                     f'<div class="links"><a href="{esc(u["sheet"])}">sheet</a> · <a href="{esc(u["folder"])}/unit.json">unit.json</a>'
                     f' · 武器 {len(u["weapons"])}</div><details><summary>武器动画零件</summary>{rows}</details></div>')
    sections.append(("units", f"机体 ({len(gallery['units'])})", "".join(cards)))
    cutins = defaultdict(list)
    for item in gallery["cutins"]:
        cutins[item["group"]].append(item)
    sections.append(("cutins", f"战斗 cut-in ({len(cutins)} 组)", "".join(
        f'<div class="group"><h3>{esc(g.split("/", 1)[1])}</h3>' + "".join(
            f'<a href="{esc(i["path"])}">{img(i["path"], "scene " + str(i["scene"]), "movie")}</a>' for i in items) + "</div>"
        for g, items in sorted(cutins.items()))))
    sections.append(("portraits", f"人物头像 ({len(gallery['portraits'])})", "".join(
        f'<div class="card small">{img(p["path"], p["name"])}<div>{p["index"]:03d} {esc(p["short"])}</div></div>'
        for p in gallery["portraits"])))
    groups = defaultdict(list)
    for item in gallery["battle"]:
        groups[item["group"]].append(item)
    sections.append(("battle", f"战斗特效与其他战斗图集 ({len(gallery['battle'])})", "".join(
        f'<div class="group"><h3>{esc(g)}</h3>' + "".join(
            f'<a href="{esc(i["path"])}">{img(i["path"], "scene " + str(i["scene"]), "anim")}</a>' for i in items) + "</div>"
        for g, items in sorted(groups.items()))))
    sections.append(("movies", f"地图合体／变形动画 ({len(gallery['movies'])})", "".join(
        f'<div class="group"><h3>#{m["movie"]:02d} {esc(m["title"])} <span class="note">{esc(m["trigger"])}</span></h3>'
        + "".join(f'<a href="{esc(i["path"])}">{img(i["path"], "scene " + str(i["scene"]), "movie")}</a>' for i in m["scenes"])
        + "</div>" for m in gallery["movies"])))
    sections.append(("chapter-titles", f"章节标题 ({len(gallery['chapter-titles'])})", "".join(
        f'<div class="card"><a href="{esc(m["path"])}">{img(m["path"], "", "movie")}</a>'
        f'<div>#{m["index"]:03d} · scene {m["scene"]}</div></div>' for m in gallery["chapter-titles"])))
    sections.append(("maps", f"战场底图 ({len(gallery['maps'])})", "".join(
        f'<div class="card small"><a href="{esc(m["path"])}">{img(m["path"], "", "map")}</a><div>{m["index"]:03d}</div></div>'
        for m in gallery["maps"])))
    nav = " · ".join(f'<a href="#{k}">{esc(t)}</a>' for k, t, _ in sections)
    body = "".join(f'<section id="{k}"><h2>{esc(t)}</h2><div class="grid">{c}</div></section>' for k, t, c in sections)
    return f"""<!doctype html><html lang="zh"><head><meta charset="utf-8"><title>SRW64 Original Graphics</title>
<style>
:root{{--bg:#f4f4f6;--card:#fff;--ink:#222;--muted:#666;--line:#ddd}}
@media (prefers-color-scheme: dark){{:root{{--bg:#16171b;--card:#22242a;--ink:#e8e8ea;--muted:#9a9aa2;--line:#33353c}}}}
body{{margin:0;padding:16px;background:var(--bg);color:var(--ink);font:14px -apple-system,"PingFang SC",sans-serif}}
nav{{position:sticky;top:0;background:var(--bg);padding:8px 0;border-bottom:1px solid var(--line);z-index:1}}
a{{color:inherit}} .grid{{display:flex;flex-wrap:wrap;gap:10px}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px;width:220px}} .card.unit{{width:260px}}
.card.small{{width:auto;text-align:center;font-size:12px}}
.head{{display:flex;gap:6px;align-items:center}} .icon{{width:32px;image-rendering:pixelated}}
.pose{{max-width:200px;max-height:160px;image-rendering:pixelated;display:block;margin:6px auto}}
.anim{{max-height:96px;max-width:200px;image-rendering:pixelated;margin:2px;background:#0003}}
.movie{{max-width:210px;max-height:180px;image-rendering:pixelated}} .map{{max-width:160px}}
.note,.links{{color:var(--muted);font-size:12px}} .weapon{{margin:6px 0;font-size:12px}} .weapon span{{display:block}} .group{{width:100%}} h3{{font-size:13px;margin:10px 0 4px}}
img{{vertical-align:middle}}
</style></head><body><h1>SRW64 原版图像导出</h1>
<p class="note">由 tools/content/export_graphics.py 从原版 ROM 生成；绑定来自固定 ROM 表，详见 manifest.json。
缩略图为逐帧横排，点开为按步骤表播放的 APNG（每拍按 {TICK_MS} ms 预览；一拍是一次精灵更新，实际帧率未核对）。</p><nav>{nav}</nav>{body}</body></html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", type=Path, default=ROOT / "rom.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "assets/original-graphics")
    args = parser.parse_args()
    coverage = export(args.rom, args.out)
    print(json.dumps(coverage, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
