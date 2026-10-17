"""Finished hidden elements carried into a later playthrough (docs/design/new-game-plus.md):
the grants copy the original joins, and the reference logic decides when they run."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("stage_status", ROOT / "tools/content/stage_status.py")
STATUS = importlib.util.module_from_spec(SPEC)
sys.modules.setdefault("stage_status", STATUS)   # dataclasses look their module up
SPEC.loader.exec_module(STATUS)
RECORDS = ROOT / "assets/original-data/records"
# D_800CB5DC: a deployment record's upgrade index → the five upgrade levels 8020ABB4 writes.
UPGRADES_AT = 0x800CB5DC - 0x80075610
# Acquire rules with nothing to register: an enemy reinforcement (牛剣鬼), a line of
# dialogue (シュバルツ) and a colour choice (キュベレイMk-II).
NOT_GRANTED = {"secret-gyuken", "secret-schwarz", "secret-qubeley-color"}
REGISTERING = {0x3D5A: "3D5A", 0x3D6C: "3D6C", 0x3D70: "3D70", 0x3D64: "3D64"}


def jsonl(name):
    return [json.loads(line) for line in (RECORDS / name).read_text(encoding="utf-8").splitlines() if line.strip()]


class GrantDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.grants = STATUS.load_grants()
        cls.rules = {r["id"]: r for r in STATUS.load_rules()["rules"]}
        cls.events = {e["key"].rsplit(":", 1)[1]: e["script"] for e in jsonl("stage_events.jsonl") if e.get("script")}
        rom = (ROOT / "rom.z64").read_bytes() if (ROOT / "rom.z64").exists() else None
        cls.upgrades = rom[UPGRADES_AT:UPGRADES_AT + 32] if rom else None
        cls.deploy = {}
        for record in jsonl("stage_deployments.jsonl"):
            f = {x["offset"]: x["encoded_value"] for x in record["fields"]}
            cls.deploy[record["key"].rsplit(":", 1)[1]] = {"actor": f[6], "level": f[9], "unit": f[10], "upgrade": f[12]}
        cls.deploy_scenes = {}
        for block in jsonl("stage_auxiliary.jsonl"):
            scenes = {int(l["relation"].split()[-1]) for l in block["links"] if l["relation"].startswith("场景索引")}
            for link in block["links"]:
                if link["key"].startswith("base:stage_deployments:"):
                    cls.deploy_scenes.setdefault(link["key"].rsplit(":", 1)[1], set()).update(scenes)

    def block(self, key, offset):
        """The registering commands of the condition block around OFFSET in event KEY."""
        items = self.events[key]["instructions"]
        index = next(i for i, x in enumerate(items) if x["offset"] == offset)
        depth = items[index]["depth"]
        a = b = index
        while a > 0 and items[a - 1]["depth"] >= depth:
            a -= 1
        while b + 1 < len(items) and items[b + 1]["depth"] >= depth:
            b += 1
        return [[REGISTERING[x["opcode"]], *x["operands"]] for x in items[a:b + 1] if x["opcode"] in REGISTERING]

    def sources(self, entry):
        """The commands the entry's sources allow, and the scenes those sources play in."""
        allowed, scenes, kinds = [], set(), set()
        for part in entry["source"].split(";"):
            kind, _, what = part.partition(":")
            if kind == "event":
                key, offset = what.split("@")
                allowed += self.block(key, int(offset))
                scenes |= set(self.events[key]["scenes"])
                kinds.add(self.events[key]["header_words"][0])
            else:
                self.assertEqual(kind, "deploy")
                for key in what.split(","):
                    d = self.deploy[key]
                    allowed.append(["3D5A", d["actor"], d["level"], d["unit"], 500])
                    if self.upgrades and self.upgrades[d["upgrade"]]:
                        allowed.append(["3D6C", d["unit"], self.upgrades[d["upgrade"]]])
                    scenes |= self.deploy_scenes[key]
        return allowed, scenes, kinds

    def test_every_acquire_rule_but_three(self):
        acquire = {i for i, r in self.rules.items() if r["kind"] == "acquire"}
        self.assertEqual({g["id"] for g in self.grants["grants"]}, acquire - NOT_GRANTED)
        self.assertEqual(len(self.grants["grants"]), 33)

    def test_commands_copy_the_original(self):
        for grant in self.grants["grants"]:
            for entry in grant["entries"]:
                with self.subTest(grant=grant["id"], source=entry["source"]):
                    allowed, scenes, kinds = self.sources(entry)
                    for command in entry["commands"]:
                        self.assertIn(command, allowed)
                    played = set(entry.get("after", [])) | set(entry.get("before", []))
                    self.assertTrue(played and played <= scenes, f"{played} not among {scenes}")
                    # A join in a stage's opening runs before that stage; any other after it.
                    if "before" in entry:
                        self.assertEqual(kinds, {12})

    def test_scenes_are_the_rules(self):
        for grant in self.grants["grants"]:
            rule = self.rules[grant["id"]]
            at = {s for p in rule.get("paths", [{"steps": rule.get("steps")}]) for step in p["steps"] for s in step["at"]}
            for entry in grant["entries"]:
                with self.subTest(grant=grant["id"], source=entry["source"]):
                    self.assertLessEqual(set(entry.get("after", [])) | set(entry.get("before", [])), at)


class GrantLogicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rules = STATUS.load_rules()
        cls.rules, cls.guards = rules["rules"], rules["edge_guards"]
        cls.by_id = {r["id"]: r for r in cls.rules}
        cls.graph = STATUS.scene_graph()
        cls.grants = STATUS.load_grants()

    def evaluator(self, protagonist, scene, **state):
        return STATUS.Evaluator(self.graph, STATUS.State(protagonist, scene, **state), self.guards)

    def due(self, legacy, protagonist, cleared, upcoming, **state):
        ev = self.evaluator(protagonist, upcoming, **state)
        return [(i, e["commands"]) for i, e in STATUS.due(legacy, self.rules, self.grants, ev, cleared, upcoming)]

    def test_right_after_the_join_stage(self):
        katz = {"secret-katz": {"pilots": [31], "units": [35]}}
        self.assertEqual(self.due(katz, "brad", 124, 42), [("secret-katz", [["3D5A", 31, 0, 35, 500]])])
        self.assertEqual(self.due(katz, "brad", 41, 124), [])            # not yet: S124 still to play
        self.assertEqual(self.due(katz, "brad", 124, 42, pilots={31}), [])   # already has him
        self.assertEqual(self.due(katz, "ark", 124, 42), [])             # Brad's element only
        self.assertEqual(self.due({}, "brad", 124, 42), [])              # never finished

    def test_before_an_opening_join(self):
        reese = {"secret-reese": {"pilots": [30], "units": [328]}}
        self.assertEqual(self.due(reese, "serein", 39, 40), [("secret-reese", [["3D5A", 30, 0, 328, 500]])])

    def test_variant_follows_the_record(self):
        chara = {"secret-mashymre-chara": {"pilots": [246], "units": []}}
        self.assertEqual(self.due(chara, "ark", 103, 104), [("secret-mashymre-chara", [["3D5A", 246, 2, 94, 500]])])
        nu = {"secret-mp-nu": {"pilots": [], "units": [296]}}
        self.assertEqual(self.due(nu, "ark", 137, 138, vars={10: 1}), [("secret-mp-nu", [["3D5A", 999, 0, 296, 500]])])
        self.assertEqual(self.due(nu, "ark", 137, 138, vars={10: 1}, units={294}), [])

    def test_swaps_need_the_old_machine(self):
        swap = ["3D5A", 286, 0, 136, 123]
        self.assertFalse(STATUS.runnable(swap, {57}))
        self.assertTrue(STATUS.runnable(swap, {123}))
        self.assertTrue(STATUS.runnable(["3D5A", 284, 0, 999, 999], set()))
        self.assertTrue(STATUS.runnable(["3D6C", 80, 6], set()))

    def test_record_keeps_what_was_finished(self):
        ev = self.evaluator("brad", 42, vars={21: 2}, pilots={31, 27}, units={35})
        legacy = STATUS.record({}, self.rules, self.grants, ev)
        self.assertEqual(legacy["secret-katz"], {"pilots": [31], "units": [35]})
        # Only ever added to: a later game without him keeps the record.
        again = STATUS.record(legacy, self.rules, self.grants, self.evaluator("brad", 42))
        self.assertEqual(again["secret-katz"], {"pilots": [31], "units": [35]})

    def test_gato_shows_inherited(self):
        # The grant gives Gato and GP02A but leaves var 37, which his rule reads.
        legacy = {"secret-gato": {"pilots": [52], "units": [75]}}
        ev = self.evaluator("ark", 51, vars={10: 1, 37: 3}, pilots={52})
        self.assertNotEqual(ev.evaluate(self.by_id["secret-gato"])["state"], "done")
        self.assertTrue(STATUS.inherited(legacy, self.grants, self.by_id["secret-gato"], ev))
        self.assertFalse(STATUS.inherited(legacy, self.grants, self.by_id["secret-gato"], self.evaluator("ark", 51, vars={10: 1})))


if __name__ == "__main__":
    unittest.main()
