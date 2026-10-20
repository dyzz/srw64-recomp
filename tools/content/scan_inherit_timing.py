#!/usr/bin/env python3
"""The facts the next step of carried hidden elements needs, per element and route.

docs/design/new-game-plus-next.md §4 joins a carried element no earlier than the
original join stage's stage number, the variable's last route read, and the last
stage of the current route that deploys the character on another side. This tool
reads those facts off the original records so the design table is not written by
hand:

- every route a protagonist can take, as the paths the stage scripts' `3D4B` draw
  (stage_status.scene_graph, with the status rules' edge guards), and each scene's
  stage number on each path (every `3D4A` adds one, Link Battler stages excepted);
- for each element's characters (the pilots its inherit-grants entries register):
  the scenes whose deployment blocks carry them on the enemy or third side, the
  other actor records with the same name (an enemy copy such as アレンビー 248 next
  to the playable 9: the engine finds units by actor id, so a copy never collides), the
  `3D5A` registrations and removals (modes 2000/4000) and `3D58`/`3D59` aimed at
  them, with the event type (opening 12, deployment 13, closing 14, in-stage);
- for each element's progress variables (from status-rules.json): where the
  scripts write them (`3E13`) and read them (`3E03`/`3E02`), by scene and event type.

A route is named by the scenes it passes: IA (S31), OZ (S46), CP-solo (S62 then
S71), CP-treize (S62 then S74). Everything here is static; nothing runs the game.

    python3 tools/content/scan_inherit_timing.py            # Markdown, every element
    python3 tools/content/scan_inherit_timing.py --json     # machine-readable
    python3 tools/content/scan_inherit_timing.py --element secret-gato
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scan_retreats  # noqa: E402
import stage_status  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RECORDS = ROOT / "assets/original-data/records"
GRANTS = ROOT / "guide/data/inherit-grants.json"
RULES = ROOT / "guide/data/status-rules.json"
OPENING, DEPLOYMENT, CLOSING = 12, 13, 14
ROUTE_MARKS = (("IA", 31), ("OZ", 46), ("CP-solo", 71), ("CP-treize", 74))
GROUPS = {"real": ("ark", "serein"), "super": ("brad", "manami")}
VAR_SET, VAR_INC, VAR_DEC, VAR_EQ, VAR_NE = 0x3E13, 0x3E0E, 0x3E0F, 0x3E03, 0x3E02
REGISTER, SIDE, BAN, NEXT = 0x3D5A, 0x3D58, 0x3D59, 0x3D4B


def jsonl(name: str) -> list[dict]:
    return [json.loads(l) for l in (RECORDS / name).read_text(encoding="utf-8").splitlines() if l.strip()]


def scene_titles() -> dict[int, str]:
    return {int(r["key"].rsplit(":", 1)[1]): r["label"].split("·", 1)[-1].strip() for r in jsonl("scenarios.jsonl")}


# ---------------------------------------------------------------- routes and stage numbers

def guard_ok(path: list[int], here: int, nxt: int) -> bool:
    """The side stages 123/124 return to the route the player came from."""
    if here in (123, 124):
        oz = 46 in path
        return (nxt == 57) == oz
    return True


def paths(graph: dict[int, list[int]], start: int) -> list[list[int]]:
    out: list[list[int]] = []

    def walk(path: list[int]) -> None:
        here = path[-1]
        nxt = [n for n in graph.get(here, []) if n not in path and n not in stage_status.UNUSED and guard_ok(path, here, n)]
        if not nxt:
            out.append(path)
            return
        for n in nxt:
            walk(path + [n])

    walk([start])
    return out


def route_name(path: list[int]) -> str:
    """CP branches off IA at S32, so a CP path is named CP, not IA."""
    names = [name for name, mark in ROUTE_MARKS if mark in path]
    if any(n.startswith("CP") for n in names):
        names = [n for n in names if n.startswith("CP")]
    return "+".join(names) if names else "early"


def routes(protagonist: str, graph: dict[str, dict[int, list[int]]]) -> dict[str, list[list[int]]]:
    by_route: dict[str, list[list[int]]] = collections.defaultdict(list)
    for p in paths(graph[protagonist], stage_status.STARTS[protagonist]):
        by_route[route_name(p)].append(p)
    return dict(by_route)


def stage_numbers(route_paths: list[list[int]]) -> dict[int, tuple[int, int]]:
    """Scene → (min, max) stage number over the route's paths (first stage is 1)."""
    spans: dict[int, list[int]] = collections.defaultdict(list)
    for p in route_paths:
        for i, scene in enumerate(p):
            spans[scene].append(i + 1)
    return {s: (min(v), max(v)) for s, v in spans.items()}


# ---------------------------------------------------------------- events

def event_rows() -> list[dict]:
    rows = []
    for e in jsonl("stage_events.jsonl"):
        script = e.get("script") or {}
        kind = (script.get("trigger") or {}).get("type")
        rows.append({"key": e["key"].rsplit(":", 1)[1], "scenes": script.get("scenes", []), "type": kind,
                     "instructions": script.get("instructions") or []})
    return rows


def event_kind(kind: int | None) -> str:
    return {OPENING: "开场", DEPLOYMENT: "初始部署", CLOSING: "关末"}.get(kind, "关内")


def actor_events(rows: list[dict]) -> dict[int, dict[str, list[tuple]]]:
    """Actor → registrations / removals / side changes / bans: (scene, event kind, section, operands)."""
    out: dict[int, dict[str, list[tuple]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    for e in rows:
        for item in e["instructions"]:
            ops, code = item.get("operands") or [], item["opcode"]
            section = item.get("section") or "3DD0"
            for scene in e["scenes"]:
                where = (scene, event_kind(e["type"]), section, ops)
                if code == REGISTER and 0 < ops[0] < 500 and ops[0] != 999:
                    out[ops[0]]["removals" if ops[3] in (2000, 4000) else "registrations"].append(where)
                elif code == SIDE and 0 < ops[0] < 500:
                    out[ops[0]]["side_changes"].append(where)
                elif code == BAN and 0 < ops[0] < 500:
                    out[ops[0]]["bans"].append(where)
    return out


def variable_events(rows: list[dict]) -> dict[int, dict[str, list[tuple]]]:
    """Variable → writes (scene, kind, section, value or ±1) and reads (scene, kind, section, test)."""
    out: dict[int, dict[str, list[tuple]]] = collections.defaultdict(lambda: collections.defaultdict(list))
    for e in rows:
        for item in e["instructions"]:
            ops, code = item.get("operands") or [], item["opcode"]
            section = item.get("section") or "3DD0"
            for scene in e["scenes"]:
                if code == VAR_SET:
                    out[ops[0]]["writes"].append((scene, event_kind(e["type"]), section, f"={ops[1]}"))
                elif code == VAR_INC:
                    out[ops[0]]["writes"].append((scene, event_kind(e["type"]), section, "+1"))
                elif code == VAR_DEC:
                    out[ops[0]]["writes"].append((scene, event_kind(e["type"]), section, "-1"))
                elif code == VAR_EQ:
                    out[ops[0]]["reads"].append((scene, event_kind(e["type"]), section, f"=={ops[1]}"))
                elif code == VAR_NE:
                    out[ops[0]]["reads"].append((scene, event_kind(e["type"]), section, f"!={ops[1]}"))
    return out


def route_reads(rows: list[dict]) -> dict[int, set[int]]:
    """Variables whose value decides a `3D4B`: variable → scenes (the condition chain above a 3D4B)."""
    out: dict[int, set[int]] = collections.defaultdict(set)
    for e in rows:
        items = e["instructions"]
        for i, item in enumerate(items):
            if item["opcode"] != NEXT or (item.get("operands") or [stage_status.RESUME])[0] == stage_status.RESUME:
                continue
            depth = item["depth"]
            j = i - 1
            while j >= 0 and depth > 0:
                prev = items[j]
                if prev["depth"] < depth:
                    depth = prev["depth"]
                    k = j
                    while k >= 0 and items[k]["depth"] == depth and items[k]["opcode"] != 0x3E1D:
                        if items[k]["opcode"] in (VAR_EQ, VAR_NE):
                            for scene in e["scenes"]:
                                out[items[k]["operands"][0]].add(scene)
                        k -= 1
                j -= 1
    return out


# ---------------------------------------------------------------- elements

def rule_variables(rule: dict) -> set[int]:
    found: set[int] = set()

    def walk(node):
        if isinstance(node, dict):
            if "var" in node and isinstance(node["var"], int):
                found.add(node["var"])
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    walk(rule.get("steps")), walk(rule.get("paths")), walk(rule.get("done"))
    return found


def elements() -> list[dict]:
    rules = {r["id"]: r for r in json.loads(RULES.read_text(encoding="utf-8"))["rules"]}
    out = []
    for grant in json.loads(GRANTS.read_text(encoding="utf-8"))["grants"]:
        actors, units, joins = [], [], []
        for entry in grant["entries"]:
            for cmd in entry["commands"]:
                if cmd[0] == "3D5A":
                    if 0 < cmd[1] < 500 and cmd[1] != 999 and cmd[1] not in actors:
                        actors.append(cmd[1])
                    if 0 < cmd[3] < 999 and cmd[3] not in units:
                        units.append(cmd[3])
            for scene in entry.get("after", []):
                joins.append((scene, "after"))
            for scene in entry.get("before", []):
                joins.append((scene, "before"))
        rule = rules[grant["id"]]
        out.append({"id": grant["id"], "actors": actors, "units": units, "joins": joins,
                    "applies": rule.get("applies"), "variables": sorted(rule_variables(rule))})
    return out


def fmt_scene(scene: int, numbers: dict[int, tuple[int, int]]) -> str:
    span = numbers.get(scene)
    if span is None:
        return f"S{scene}"
    lo, hi = span
    return f"S{scene}(第{lo}话)" if lo == hi else f"S{scene}(第{lo}–{hi}话)"


def same_name(actors: list[int], names: dict[int, str], deployments) -> dict[int, list[int]]:
    """Actor → the other records with its name, each with the scenes deploying it on another side."""
    out = {}
    for a in actors:
        for b, label in names.items():
            if b != a and label == names.get(a):
                out[b] = sorted({s for s, rows in deployments.items() for r in rows if r["actor"] == b and r["faction"] != 0})
    return out


def element_report(el: dict, group_routes: dict[str, dict[str, list[list[int]]]], deployments, events, names) -> dict:
    report = {"id": el["id"], "actors": {a: names.get(a, f"#{a}") for a in el["actors"]}, "units": el["units"],
              "variables": el["variables"], "applies": el["applies"],
              "same_name": same_name(el["actors"], names, deployments), "routes": {}}
    for group, by_route in group_routes.items():
        for route, route_paths in by_route.items():
            scenes = {s for p in route_paths for s in p}
            numbers = stage_numbers(route_paths)
            joins = sorted({(s, how) for s, how in el["joins"] if s in scenes})
            other_side = sorted({s for s in scenes for row in deployments.get(s, []) if row["actor"] in el["actors"] and row["faction"] != 0})
            removals = sorted({(s, kind, sec) for a in el["actors"] for s, kind, sec, _ in events[a]["removals"] if s in scenes})
            side_changes = sorted({(s, kind, sec, tuple(ops)) for a in el["actors"] for s, kind, sec, ops in events[a]["side_changes"] if s in scenes})
            bans = sorted({(s, kind, sec) for a in el["actors"] for s, kind, sec, _ in events[a]["bans"] if s in scenes})
            join_numbers = [numbers[s][0] for s, _ in joins]
            report["routes"][f"{group}/{route}"] = {
                "joins": [f"{fmt_scene(s, numbers)}{'前' if how == 'before' else '后'}" for s, how in joins],
                "join_stage": min(join_numbers) if join_numbers else None,
                "other_side": [fmt_scene(s, numbers) for s in other_side],
                "last_other_side_stage": max(numbers[s][1] for s in other_side) if other_side else None,
                "removals": [f"{fmt_scene(s, numbers)}{kind}{'' if sec == '3DD0' else '/' + sec}" for s, kind, sec in removals],
                "side_changes": [f"{fmt_scene(s, numbers)}{kind}→阵营{ops[1]}" for s, kind, sec, ops in side_changes],
                "bans": [f"{fmt_scene(s, numbers)}{kind}" for s, kind, sec in bans],
            }
    return report


def variable_report(variables: list[int], var_events, route_vars, names_of_scene) -> dict:
    out = {}
    for v in variables:
        ev = var_events.get(v, {})
        out[v] = {
            "writes": sorted({f"S{s}{kind}{'' if sec == '3DD0' else '/' + sec} {val}" for s, kind, sec, val in ev.get("writes", [])}),
            "reads": sorted({f"S{s}{kind}{'' if sec == '3DD0' else '/' + sec} {test}" for s, kind, sec, test in ev.get("reads", [])}),
            "route_reads": sorted(route_vars.get(v, set())),
        }
    return out


def markdown(reports: list[dict], var_reports: dict, titles: dict[int, str]) -> str:
    lines = ["# 周目继承：各要素的路线事实（scan_inherit_timing.py 输出）", ""]
    for r in reports:
        actors = "、".join(f"{n}({a})" for a, n in r["actors"].items()) or "—"
        copies = "；".join(f"{b}（非我方部署 {'、'.join('S%d' % s for s in scenes) or '无'}）" for b, scenes in r["same_name"].items())
        lines += [f"## {r['id']}", "", f"- 人物：{actors}；机体：{r['units'] or '—'}；变量：{r['variables'] or '—'}",
                  f"- 规则适用：`{json.dumps(r['applies'], ensure_ascii=False)}`",
                  f"- 同名其他记录（敌方副本，按号查找不会撞车）：{copies or '无'}", "",
                  "| 主角/路线 | 原入队 | 以非我方部署 | 最后非我方话数 | 原版删除 | 转阵营 | 3D59 |",
                  "| --- | --- | --- | --- | --- | --- | --- |"]
        for key, row in r["routes"].items():
            lines.append(f"| {key} | {'、'.join(row['joins']) or '—'} | {'、'.join(row['other_side']) or '—'} | "
                         f"{row['last_other_side_stage'] or '—'} | {'、'.join(row['removals']) or '—'} | "
                         f"{'、'.join(row['side_changes']) or '—'} | {'、'.join(row['bans']) or '—'} |")
        lines.append("")
        for v in r["variables"]:
            vr = var_reports[v]
            lines += [f"- var{v} 写：{'；'.join(vr['writes']) or '—'}",
                      f"- var{v} 读：{'；'.join(vr['reads']) or '—'}",
                      f"- var{v} 参与 3D4B：{'、'.join('S%d' % s for s in vr['route_reads']) or '不参与'}"]
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--element", help="one element id")
    ap.add_argument("--routes", action="store_true", help="print each protagonist's routes and stage numbers")
    args = ap.parse_args()

    graph = stage_status.scene_graph()
    group_routes = {}
    for group, protagonists in GROUPS.items():
        merged: dict[str, list[list[int]]] = collections.defaultdict(list)
        for p in protagonists:
            for route, ps in routes(p, graph).items():
                merged[route].extend(ps)
        group_routes[group] = dict(merged)
    titles = scene_titles()
    if args.routes:
        for group, by_route in group_routes.items():
            for route, ps in sorted(by_route.items()):
                numbers = stage_numbers(ps)
                print(f"## {group}/{route}: {len(ps)} 条路径")
                print("、".join(fmt_scene(s, numbers) for s in sorted(numbers, key=lambda s: numbers[s])))
                print()
        return 0

    rows = event_rows()
    deployments = scan_retreats.deployments()
    events = actor_events(rows)
    var_events = variable_events(rows)
    route_vars = route_reads(rows)
    names = scan_retreats.actor_names()
    els = [e for e in elements() if not args.element or e["id"] == args.element]
    reports = [element_report(e, group_routes, deployments, events, names) for e in els]
    variables = sorted({v for e in els for v in e["variables"]})
    var_reports = variable_report(variables, var_events, route_vars, titles)
    if args.json:
        print(json.dumps({"elements": reports, "variables": var_reports}, ensure_ascii=False, indent=1))
    else:
        print(markdown(reports, var_reports, titles))
    return 0


if __name__ == "__main__":
    sys.exit(main())
