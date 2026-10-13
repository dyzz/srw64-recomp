#!/usr/bin/env python3
"""List every enemy retreat the stage scripts can make, for the stage-info page.

A retreat is a `3D46` (unit leaves the map) aimed at an enemy or third-party unit:
directly by character (< 500) or by deployment group (500 + group, the group's
records in that scene's deployment block). Each one is reported with the event's
trigger in screen terms (turns + 1, docs/gameplay/hidden-elements.md §4.1), the
condition blocks it sits in, and the story section (protagonist) it belongs to.

Opening, initial-deployment and closing events (types 12–14) only stage cutscenes,
and persuasion events (type 9) are the parts table's "persuaded" note, so both are
left out. A unit that the same event deploys again (`3D45` of its group after the
`3D46`) is being moved, not retreated, and is marked so.

This is a reading aid: the guide's parts notes and its `retreats` rows
(guide/data/<lang>/progression.json) are written from its output by hand, with the
conditions put in players' words (docs/design/stage-info.md §3). Nothing here runs
the game.

    python3 tools/content/scan_retreats.py               # every scene, Markdown
    python3 tools/content/scan_retreats.py --scene 25    # one scene
    python3 tools/content/scan_retreats.py --json        # machine-readable
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORDS = ROOT / "assets/original-data/records"
SKIP_TYPES = {9, 12, 13, 14}
GENERIC = 287  # actors from here on are generic soldiers (AI, ムゲ兵, スペシャルズ…)
FACTIONS = {0: "我方", 1: "敌方", 2: "第三方"}
GENERIC_NAMES: set[str] = set()


def jsonl(name: str) -> list[dict]:
    return [json.loads(l) for l in (RECORDS / name).read_text(encoding="utf-8").splitlines() if l.strip()]


def actor_names() -> dict[int, str]:
    return {int(r["key"].rsplit(":", 1)[1]): r["label"] for r in jsonl("actors.jsonl")}


def deployments() -> dict[int, list[dict]]:
    """Scene → its deployment records (group, actor, faction after 3 → 0, 4 → 2)."""
    records = {r["key"]: r for r in jsonl("stage_deployments.jsonl")}
    by_scene: dict[int, list[dict]] = collections.defaultdict(list)
    for block in jsonl("stage_auxiliary.jsonl"):
        scenes = [int(l["relation"].split()[-1]) for l in block["links"] if l["relation"].startswith("场景索引")]
        rows = []
        for link in block["links"]:
            record = records.get(link["key"])
            if not record:
                continue
            fields = {f.get("role") or f["name"]: f for f in record["fields"]}
            side = fields["deploy_side"]["encoded_value"]
            rows.append({"group": fields["group"]["encoded_value"], "actor": fields["actor"]["encoded_value"],
                         "faction": {3: 0, 4: 2}.get(side, side)})
        for scene in scenes:
            by_scene[scene].extend(rows)
    return by_scene


def trigger_text(script: dict, names: dict[int, str]) -> str:
    kind, *words = script["header_words"]
    name = lambda a: names.get(a, f"#{a}")
    if kind == 0:
        phase = {1: "我方阶段", 2: "敌方阶段", 3: "第三方阶段"}.get(words[1], f"阶段 {words[1]}")
        return f"第 {words[0] + 1} 回合{phase}开始"
    if kind == 1:
        return "延迟计数到期（3D52 启动）"
    if kind == 2:
        gate = f"（门槛 var{words[0]}）" if 100 <= words[0] <= 115 else ""
        return f"{name(words[1])} 被击坠{gate}"
    if kind == 3:
        return f"{name(words[0])} HP {words[1]}% 以下"
    if kind in (4, 5):
        who = lambda a: "任意" if a == 0 else name(a)
        return f"{who(words[0])} 与 {who(words[1])} 交战{'后' if kind == 4 else '前'}"
    if kind == 6:
        return "敌方全灭" + ("" if words[0] == 254 else f"（第 {words[0] + 1} 回合以内）")
    if kind == 7:
        side = "第三方" if words[0] == 2 else "敌方"
        gate = f"（门槛 var{words[3]}）" if 100 <= words[3] <= 115 else ""
        return f"{side}剩 {words[1]} 台以下{gate}"
    if kind == 8:
        target = words[1] % 1000
        who = "我方旗舰" if target == 21 else (f"阵营 {target - 500} 任意单位" if target >= 500 else name(target))
        return f"{who} 进入区域 x{words[2] // 10}+{words[2] % 10} y{words[3] // 10}+{words[3] % 10}"
    return script["trigger"]["name"]


def condition_text(item: dict, names: dict[int, str]) -> str:
    ops = item.get("operands") or []
    code = item["opcode"]
    name = lambda a: names.get(a, f"#{a}")
    if code == 0x3E1B:
        return f"{name(ops[0])}在场状态"
    if code == 0x3E0D:
        return f"{name(ops[0])}击坠数"
    if code == 0x3E16:
        return f"交战另一方（排除{name(ops[0])}）"
    if code == 0x3E15:
        return "阵营单位数"
    if code == 0x3E03:
        return f"var{ops[0]} == {ops[1]}"
    if code == 0x3E02:
        return f"var{ops[0]} != {ops[1]}"
    if code in (0x3E08, 0x3E0A, 0x3E09, 0x3E0B, 0x3E0C):
        op = {0x3E08: "==", 0x3E0A: "!=", 0x3E09: ">=", 0x3E0B: ">=", 0x3E0C: "<"}[code]
        value = ops[0]
        return f"{op} {name(value) if value >= 2 and code == 0x3E08 and value < 500 and value > 3 else value}"
    if code in (0x3E10, 0x3E11, 0x3E12):
        return f"选择第 {code - 0x3E0F} 项"
    if code == 0x3E04:
        return f"画面回合 <= {ops[0]}"
    return f"{code:04X} {ops}"


def guards(items: list[dict], index: int, names: dict[int, str]) -> list[str]:
    """The condition chain around instruction INDEX: each enclosing block's opener with
    the ACC loads right before it."""
    out = []
    depth = items[index]["depth"]
    i = index - 1
    while i >= 0 and depth > 0:
        if items[i]["depth"] < depth and items[i]["kind"] == "condition" and items[i]["opcode"] != 0x3E1D:
            chain = [items[i]]
            j = i - 1
            while j >= 0 and items[j]["depth"] == items[i]["depth"] and items[j]["kind"] == "condition" \
                    and items[j]["opcode"] in (0x3E1B, 0x3E0D, 0x3E16, 0x3E15, 0x3E14, 0x3E17, 0x3E18, 0x3E06):
                chain.insert(0, items[j])
                j -= 1
            out.insert(0, " ".join(condition_text(c, names) for c in chain))
            depth = items[i]["depth"]
        i -= 1
    return out


def scan() -> dict[int, list[dict]]:
    names = actor_names()
    GENERIC_NAMES.update(name for actor, name in names.items() if actor >= GENERIC)
    deploy = deployments()
    found: dict[int, list[dict]] = collections.defaultdict(list)
    for row in sorted(jsonl("stage_events.jsonl"), key=lambda r: r["rom_offset"]):
        script = row.get("script") or {}
        kind = script.get("header_words", [None])[0]
        if kind in SKIP_TYPES:
            continue
        items = script.get("instructions") or []
        if not any(i["opcode"] == 0x3D46 for i in items):
            continue
        ends = any(i["opcode"] == 0x3D4A for i in items)
        defeated = script["header_words"][2] if kind == 2 else None
        for scene in script.get("scenes", []):
            records = deploy.get(scene, [])
            side_of = {}
            for r in records:
                side_of.setdefault(r["actor"], set()).add(r["faction"])
            for index, item in enumerate(items):
                if item["opcode"] != 0x3D46:
                    continue
                target = item["operands"][0]
                redeployed = {i["operands"][0] for i in items[index:] if i["opcode"] == 0x3D45}
                if target >= 500:
                    group = target - 500
                    members = [r for r in records if r["group"] == group]
                    units = [(names.get(r["actor"], f"#{r['actor']}"), r["faction"]) for r in members]
                    sides = {r["faction"] for r in members}
                    moved = group in redeployed
                    label = f"组 {group}"
                else:
                    sides = side_of.get(target, set())
                    units = [(names.get(target, f"#{target}"), min(sides) if sides else None)]
                    moved = any(r["actor"] == target and r["group"] in redeployed for r in records)
                    label = names.get(target, f"#{target}")
                if sides and sides <= {0}:
                    continue  # our own units leaving
                if target == defeated:
                    continue  # the unit that was just shot down
                units = [(n, s) for n, s in units if n not in GENERIC_NAMES]
                if not units:
                    continue
                found[scene].append({
                    "event": row["key"].rsplit(":", 1)[1], "offset": item["offset"], "type": kind,
                    "trigger": trigger_text(script, names), "guards": guards(items, index, names),
                    "section": items[index].get("section"), "target": label, "units": units,
                    "sides": sorted(sides), "moved": moved, "ends_stage": ends,
                })
    return found


def section_names() -> dict[str, str]:
    return {f"{int(r['key'].rsplit(':', 1)[1], 16):04X}": r["label"] for r in jsonl("script_markers.jsonl")}


def markdown(found: dict[int, list[dict]], scenes: list[int]) -> str:
    stages = {int(r["key"].rsplit(":", 1)[1]) - 281: r["label"] for r in jsonl("stages.jsonl")}
    sections = section_names()
    lines = []
    for scene in scenes:
        rows = found.get(scene)
        if not rows:
            continue
        lines.append(f"## S{scene} {stages.get(scene, '')}\n")
        for r in rows:
            units = "、".join(f"{n}" + (f"[{FACTIONS.get(s, s)}]" if s not in (1, None) else "") for n, s in r["units"])
            flags = [f for f, on in (("关卡结束", r["ends_stage"]), ("重新部署＝移动", r["moved"])) if on]
            section = sections.get(r["section"] or "", r["section"])
            section = "" if (r["section"] or "") == "3DD0" else f" ⟨{section}⟩"
            guard = (" ｜ " + " ∧ ".join(r["guards"])) if r["guards"] else ""
            flag = f" **{'、'.join(flags)}**" if flags else ""
            lines.append(f"- `{r['event']}@{r['offset']}` 类型 {r['type']}：{r['trigger']}{guard}{section} → {r['target']}：{units}{flag}")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", type=int, action="append")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    found = scan()
    scenes = args.scene or sorted(found)
    if args.json:
        json.dump({s: found.get(s, []) for s in scenes}, sys.stdout, ensure_ascii=False, indent=1)
        print()
    else:
        print(markdown(found, scenes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
