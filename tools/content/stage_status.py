"""The stage info page's hidden-element status: scene graph and rule evaluation.

This is the reference the game's evaluator follows (docs/design/stage-info.md §4).
The rules (guide/data/status-rules.json) read the 2-bit script variables, the
player's pilot and machine rosters, a pilot's kill count, the protagonist and the
current scene. A step that is not done yet is missed when none of the scenes it can
be done in is reachable from the current scene any more; reachability follows the
scene graph the stage scripts' `3D4B` (set next scene) commands draw, per
protagonist.

Rule kinds:
- acquire: `done`, and `steps` (or `paths`, each with its own `applies` and steps);
  each step has `when` and the scenes it can be done in (`at`), and may carry a
  `counter` (a pilot's kills against a target). Status: done, step N of M,
  not started, missed, or not applicable.
- choice: `options` with `when` and a label, decided in the `at` scenes;
  `otherwise` labels the outcome when none matched and the scenes are behind.
- count: how many of `count` hold.
- info: no status.

The two side-branch stages (123, 124) return to the route the player came from;
`edge_guards` keep only that edge, so the routes do not leak into each other.
"""
from __future__ import annotations

import collections
import json
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORDS = ROOT / "assets/original-data/records"
RULES = ROOT / "guide/data/status-rules.json"
PROTAGONISTS = ("ark", "serein", "brad", "manami")
# The story section markers each protagonist's script runs (8009F0E8).
SECTIONS = {"ark": {"3DD0", "3DD1", "3DD5"}, "serein": {"3DD0", "3DD2", "3DD5"},
            "brad": {"3DD0", "3DD3", "3DD6"}, "manami": {"3DD0", "3DD4", "3DD6"}}
# First scene of each protagonist (load_001090A0:801C69D0 writes +0x99E).
STARTS = {"ark": 2, "serein": 3, "brad": 0, "manami": 1}
RESUME = 500  # 3D4B 500: back from a Link Battler stage, not a scene
UNUSED = {58, 125, 126, 127, 142}
STATES = ("done", "step", "not_started", "missed", "na")


def scene_graph() -> dict[str, dict[int, list[int]]]:
    """Per protagonist: scene → the scenes its stage scripts can go to next."""
    graph = {p: collections.defaultdict(set) for p in PROTAGONISTS}
    for line in (RECORDS / "stage_events.jsonl").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        script = json.loads(line).get("script") or {}
        for item in script.get("instructions") or []:
            if item["opcode"] != 0x3D4B or item["operands"][0] == RESUME:
                continue
            target = item["operands"][0]
            for protagonist, sections in SECTIONS.items():
                if (item.get("section") or "3DD0") in sections:
                    for scene in script.get("scenes", []):
                        if scene not in UNUSED:
                            graph[protagonist][scene].add(target)
    return {p: {s: sorted(t) for s, t in sorted(g.items())} for p, g in graph.items()}


def reachable(graph: dict[int, list[int]], start: int, open_edge=lambda a, b: True) -> set[int]:
    seen, todo = {start}, [start]
    while todo:
        here = todo.pop()
        for nxt in graph.get(here, []):
            if nxt not in seen and open_edge(here, nxt):
                seen.add(nxt)
                todo.append(nxt)
    return seen


@dataclass
class State:
    protagonist: str
    scene: int
    vars: dict[int, int] = field(default_factory=dict)
    pilots: set[int] = field(default_factory=set)
    units: set[int] = field(default_factory=set)
    kills: dict[int, int] = field(default_factory=dict)


class Evaluator:
    def __init__(self, graph: dict[str, dict[int, list[int]]], state: State, guards: list[dict] = ()):
        self.state = state
        self.reach = set()
        closed = {(a, g["to"]) for g in guards if not self.test(g["when"]) for a in g["from"]}
        self.reach = reachable(graph[state.protagonist], state.scene, lambda a, b: (a, b) not in closed)

    def test(self, p: dict) -> bool:
        s = self.state
        if "all" in p:
            return all(self.test(q) for q in p["all"])
        if "any" in p:
            return any(self.test(q) for q in p["any"])
        if "not" in p:
            return not self.test(p["not"])
        if "var" in p:
            value = s.vars.get(p["var"], 3)
            return value == p["eq"] if "eq" in p else value in p["in"]
        if "pilot" in p:
            ids = p["pilot"] if isinstance(p["pilot"], list) else [p["pilot"]]
            return any(i in s.pilots for i in ids)
        if "unit" in p:
            ids = p["unit"] if isinstance(p["unit"], list) else [p["unit"]]
            return any(i in s.units for i in ids)
        if "kills" in p:
            return s.kills.get(p["kills"], 0) >= p["ge"]
        if "protagonist" in p:
            return s.protagonist in p["protagonist"]
        if "reach" in p:
            return p["reach"] in self.reach
        if "scene" in p:
            return s.scene in p["scene"]
        raise ValueError(f"unknown predicate {p}")

    def counter(self, c: dict | None) -> dict | None:
        if not c:
            return None
        return {"value": self.state.kills.get(c["kills"], 0), "of": c["of"]}

    def evaluate(self, rule: dict) -> dict:
        kind = rule["kind"]
        if kind == "info":
            return {"state": "info"}
        if "applies" in rule and not self.test(rule["applies"]):
            return {"state": "na"}
        if kind == "count":
            have = sum(self.test(p) for p in rule["count"])
            total = len(rule["count"])
            return {"state": "done" if have == total else "count", "value": have, "of": total}
        if kind == "choice":
            for option in rule["options"]:
                if self.test(option["when"]):
                    return {"state": "chosen", "label": option["label"], "counter": None}
            if not self.reach & set(rule["at"]):
                return {"state": "chosen", "label": rule["otherwise"]} if "otherwise" in rule else {"state": "missed"}
            return {"state": "pending", "counter": self.counter(rule.get("counter"))}
        if self.test(rule["done"]):
            return {"state": "done"}
        paths = [p for p in rule.get("paths", [{"steps": rule.get("steps")}])
                 if "applies" not in p or self.test(p["applies"])]
        if not paths:
            return {"state": "na"}
        best = None
        for path in paths:
            steps = path["steps"]
            progress = max((i + 1 for i, step in enumerate(steps) if self.test(step["when"])), default=0)
            if best is None or progress > best[0]:
                best = (progress, steps)
        progress, steps = best
        if progress == len(steps):
            return {"state": "done"}
        step = steps[progress]
        result = {"step": progress, "of": len(steps), "counter": self.counter(step.get("counter"))}
        if not self.reach & set(step["at"]):
            return {"state": "missed", **result}
        return {"state": "step" if progress else "not_started", **result}


def load_rules() -> dict:
    return json.loads(RULES.read_text(encoding="utf-8"))


def check_rules(rules: list[dict], entry_ids: list[str], graph: dict) -> list[str]:
    """Every hidden element has exactly one rule, in the guide's order, and every
    scene a rule names is one the stage scripts can reach."""
    errors = []
    if [r["id"] for r in rules] != entry_ids:
        errors.append("status-rules.json must list the guide's hidden elements, in order")
    known = set()
    for p, g in graph.items():
        known |= reachable(g, STARTS[p])

    def scenes(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key in ("at", "scene"):
                    yield from value
                elif key == "reach":
                    yield value
                else:
                    yield from scenes(value)
        elif isinstance(node, list):
            for item in node:
                yield from scenes(item)

    for rule in rules:
        unknown = sorted(set(scenes(rule)) - known)
        if unknown:
            errors.append(f"{rule['id']}: scenes {unknown} are not reachable from any start")
        if rule["kind"] == "acquire" and not ("steps" in rule or "paths" in rule):
            errors.append(f"{rule['id']}: an acquire rule needs steps")
        if rule["kind"] == "choice" and not rule.get("at"):
            errors.append(f"{rule['id']}: a choice rule needs its scenes")
    return errors


# Hidden elements carried into a later playthrough (docs/design/new-game-plus.md).
# The record (saves/legacy/legacy.json) maps each acquire rule a playthrough finished to
# the pilot and machine numbers it had for it; a later playthrough, in the intermission
# right after (or before) the stage of the original join, runs that join's commands
# when the player lacks it. The game's version is src/host/legacy.hpp.
GRANTS = ROOT / "guide/data/inherit-grants.json"
NOBODY = 999   # 3D5A's "no pilot" / "no machine"
REGISTER, KEEP_MODES = "3D5A", (500, 999)   # 3D5A modes that are not a swap from an old machine


def load_grants() -> dict:
    return json.loads(GRANTS.read_text(encoding="utf-8"))


def grant_ids(entries: list[dict]) -> tuple[set[int], set[int]]:
    """The pilot and machine numbers the entries' 3D5A commands register."""
    pilots, units = set(), set()
    for entry in entries:
        for command in entry["commands"]:
            if command[0] == REGISTER:
                if command[1] != NOBODY:
                    pilots.add(command[1])
                if command[3] != NOBODY:
                    units.add(command[3])
    return pilots, units


def owned(entry: dict) -> dict:
    """What says the player already has an entry's element: its own `owned`, else the
    first registered pilot, else that command's machine."""
    if "owned" in entry:
        return entry["owned"]
    first = next(c for c in entry["commands"] if c[0] == REGISTER)
    return {"pilot": first[1]} if first[1] != NOBODY else {"unit": first[3]}


def ids_of(predicate) -> tuple[set[int], set[int]]:
    pilots, units = set(), set()
    if isinstance(predicate, dict):
        for key, value in predicate.items():
            if key in ("pilot", "unit"):
                (pilots if key == "pilot" else units).update(value if isinstance(value, list) else [value])
            else:
                p, u = ids_of(value)
                pilots |= p
                units |= u
    elif isinstance(predicate, list):
        for item in predicate:
            p, u = ids_of(item)
            pilots |= p
            units |= u
    return pilots, units


def record(legacy: dict, rules: list[dict], grants: dict, evaluator: "Evaluator") -> dict:
    """LEGACY with every acquire rule this state finishes added: the rule's own pilot and
    machine numbers and the ones its grants register, those the player has."""
    out = {key: {"pilots": list(v["pilots"]), "units": list(v["units"])} for key, v in legacy.items()}
    by_id = {g["id"]: g["entries"] for g in grants["grants"]}
    s = evaluator.state
    for rule in rules:
        if rule["kind"] != "acquire" or evaluator.evaluate(rule)["state"] != "done":
            continue
        pilots, units = ids_of(rule["done"])
        gp, gu = grant_ids(by_id.get(rule["id"], []))
        have = out.setdefault(rule["id"], {"pilots": [], "units": []})
        have["pilots"] = sorted(set(have["pilots"]) | ((pilots | gp) & s.pilots))
        have["units"] = sorted(set(have["units"]) | ((units | gu) & s.units))
    return out


def due(legacy: dict, rules: list[dict], grants: dict, evaluator: "Evaluator", cleared: int, upcoming: int) -> list[tuple[str, dict]]:
    """The (rule id, entry) pairs to run in this intermission: CLEARED is the stage just
    played (8010F5F1), UPCOMING the next one (8010F5F0). At most one entry per element."""
    by_rule = {r["id"]: r for r in rules}
    out = []
    for grant in grants["grants"]:
        have = legacy.get(grant["id"])
        rule = by_rule.get(grant["id"])
        if have is None or rule is None:
            continue
        if "applies" in rule and not evaluator.test(rule["applies"]):
            continue
        # The variant is judged on what was recorded, not on this game.
        who = evaluator.state.protagonist
        recorded = Evaluator({who: {}}, State(who, -1, pilots=set(have["pilots"]), units=set(have["units"])))
        for entry in grant["entries"]:
            if cleared not in entry.get("after", []) and upcoming not in entry.get("before", []):
                continue
            if "applies" in entry and not evaluator.test(entry["applies"]):
                continue
            if "variant" in entry and not recorded.test(entry["variant"]):
                continue
            if evaluator.test(owned(entry)):
                continue
            out.append((grant["id"], entry))
            break
    return out


def runnable(command: list, units: set[int]) -> bool:
    """A 3D5A swapping from an old machine (mode below 2000, not 500/999) runs only when
    the player has that machine; everything else always runs."""
    return not (command[0] == REGISTER and command[4] not in KEEP_MODES and command[4] < 2000
                and command[4] not in units)


def inherited(legacy: dict, grants: dict, rule: dict, evaluator: "Evaluator") -> bool:
    """A finished-before element the player has now although its rule says otherwise:
    the page shows it as done (inherited). Gato's rule reads var 37, which a grant leaves."""
    if rule["id"] not in legacy:
        return False
    entries = next((g["entries"] for g in grants["grants"] if g["id"] == rule["id"]), [])
    return any(evaluator.test(owned(e)) for e in entries)
