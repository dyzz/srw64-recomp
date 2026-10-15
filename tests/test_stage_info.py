"""The in-game stage info data is exported from the guide data and must stay in step with it."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("export_stage_info", ROOT / "tools/content/export_stage_info.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def shape(data):
    """Everything but the words: what each language must agree on."""
    return {scene: [{"card": c["card"], "route": c["route"],
                     "secrets": [(s["ref"], s.get("protagonists")) for s in c["secrets"]],
                     "enemies": [(e["part"] is None, e.get("protagonists"), e.get("routes"), e.get("third_party"),
                                  e["retreat"], "note" in e) for e in c["enemies"]]}
                    for c in cards] for scene, cards in data["scenes"].items()}


class StageInfoTests(unittest.TestCase):
    def test_files_match_guide(self):
        for path, text in MODULE.outputs().items():
            with self.subTest(file=path.name):
                self.assertEqual(path.read_text(encoding="utf-8"), text,
                                 f"{path.name} is stale; run tools/content/export_stage_info.py")

    def test_languages_agree(self):
        base = shape(MODULE.export("zh-Hans"))
        for lang in MODULE.LANGS[1:]:
            with self.subTest(lang=lang):
                self.assertEqual(shape(MODULE.export(lang)), base)

    def test_scenes(self):
        stages = {}
        for line in (ROOT / "assets/original-data/records/stages.jsonl").read_text(encoding="utf-8").splitlines():
            record = json.loads(line)
            stages[int(record["key"].rsplit(":", 1)[1]) - 281] = record["label"]
        data = MODULE.export("zh-Hans")
        for scene, cards in data["scenes"].items():
            with self.subTest(scene=scene):
                self.assertTrue(all(c["title_ja"] == stages[int(scene)] for c in cards))
                # Two branch stages are shared by two routes; the page picks the card by route.
                self.assertEqual(len({c["route"] for c in cards}), len(cards))
        self.assertEqual(sorted(s for s, c in data["scenes"].items() if len(c) > 1), ["123", "124"])

    def test_stage_meta_files(self):
        """One file per stage, every text in all three languages; the format a custom
        campaign's stage carries too (docs/design/custom-campaign.md)."""
        meta = MODULE.stage_meta()
        self.assertEqual(len(meta), 124)
        def texts(value, where):
            self.assertIsInstance(value, dict, where)
            self.assertEqual(sorted(value), sorted(MODULE.LANGS), where)
            self.assertTrue(all(isinstance(v, str) and v for v in value.values()), where)
        for scene, doc in meta.items():
            with self.subTest(scene=scene):
                self.assertEqual((doc["schema"], doc["scene"]), ("srw64.stage-meta.v1", scene))
                texts(doc["title"], "title")
                for card in doc["cards"]:
                    texts(card["lane"], "lane")
                    for s in card["secrets"]:
                        texts(s["title"], s["ref"]); texts(s["text"], s["ref"])
                    for e in card["enemies"]:
                        texts(e["from"], "from")
                        if e["part"] is not None: texts(e["part"], "part")
                        if "note" in e: texts(e["note"], "note")

    def test_no_badges_or_qualifiers_left(self):
        for lang in MODULE.LANGS:
            text = json.dumps(MODULE.export(lang)["scenes"], ensure_ascii=False)
            with self.subTest(lang=lang):
                for badge in ("{≠}", "{+}", "{?}"):
                    self.assertNotIn(badge, text)
        for cards in MODULE.export("zh-Hans")["scenes"].values():
            for card in cards:
                for enemy in card["enemies"]:
                    first = enemy.get("note", "").split("；")[0]
                    self.assertNotIn(first, MODULE.NOTE_QUALIFIERS, enemy)


if __name__ == "__main__":
    unittest.main()


STATUS_SPEC = importlib.util.spec_from_file_location("stage_status", ROOT / "tools/content/stage_status.py")
STATUS = importlib.util.module_from_spec(STATUS_SPEC)
STATUS_SPEC.loader.exec_module(STATUS)


class StatusTests(unittest.TestCase):
    """Saves a player could have, and what the page should say about them."""

    @classmethod
    def setUpClass(cls):
        rules = STATUS.load_rules()
        cls.rules = {r["id"]: r for r in rules["rules"]}
        cls.guards = rules["edge_guards"]
        cls.graph = STATUS.scene_graph()

    def status(self, rule, protagonist, scene, **state):
        s = STATUS.State(protagonist, scene, **state)
        return STATUS.Evaluator(self.graph, s, self.guards).evaluate(self.rules[rule])

    def test_graph_splits_routes(self):
        brad = self.graph["brad"]
        self.assertEqual(brad[30], [31, 46])
        self.assertNotIn(41, STATUS.reachable(brad, 47))
        self.assertIn(124, STATUS.reachable(brad, 41))
        self.assertNotIn(124, STATUS.reachable(self.graph["ark"], 41))

    def test_side_branch_returns_to_its_route(self):
        # Four's last step is in S128 (Independence Army) or S57 (OZ); Ark's branch S123 sits after both splits.
        ia = self.status("secret-four", "ark", 123, vars={10: 0, 11: 0, 2: 2})
        self.assertEqual(ia["state"], "missed")      # S128 is behind and S57 is OZ only
        oz = self.status("secret-four", "ark", 123, vars={10: 1, 2: 2})
        self.assertEqual(oz["state"], "step")         # S57 is next
        # Allenby can still be won back in S129 after the Independence Army branch.
        self.assertEqual(self.status("secret-allenby", "brad", 124, vars={10: 0, 11: 0, 3: 0})["state"], "step")

    def test_katz(self):
        self.assertEqual(self.status("secret-katz", "brad", 25)["state"], "not_started")
        self.assertEqual(self.status("secret-katz", "ark", 25)["state"], "na")
        self.assertEqual(self.status("secret-katz", "brad", 42, vars={10: 0, 11: 0, 20: 0, 21: 3})["state"], "missed")
        settle = self.status("secret-katz", "brad", 124, vars={10: 0, 20: 1, 21: 0}, kills={27: 80})
        self.assertEqual((settle["state"], settle["step"], settle["counter"]), ("step", 2, {"value": 80, "of": 100}))
        self.assertEqual(self.status("secret-katz", "brad", 42, vars={21: 2}, pilots={31})["state"], "done")
        self.assertEqual(self.status("secret-katz", "brad", 65, vars={10: 0, 11: 1})["state"], "na")

    def test_garalia_window(self):
        self.assertEqual(self.status("secret-garalia", "ark", 20)["state"], "not_started")
        self.assertEqual(self.status("secret-garalia", "ark", 21)["state"], "missed")
        self.assertEqual(self.status("secret-garalia", "ark", 21, vars={4: 0})["state"], "done")

    def test_paths_follow_the_route(self):
        # Selain's Rish: before the split the Independence path leads; on OZ only the OZ steps count.
        self.assertEqual(self.status("secret-reese", "serein", 25)["of"], 3)
        oz = self.status("secret-reese", "serein", 49, vars={10: 1})
        self.assertEqual((oz["state"], oz["of"]), ("not_started", 2))
        self.assertEqual(self.status("secret-reese", "serein", 36, vars={10: 0, 11: 0, 14: 0})["state"], "missed")

    def test_choices(self):
        self.assertEqual(self.status("secret-route-oz", "ark", 30)["state"], "pending")
        self.assertEqual(self.status("secret-route-oz", "ark", 31, vars={10: 0})["label"]["en"], "Independence Army route")
        self.assertEqual(self.status("secret-route-treize", "ark", 74, vars={10: 0, 11: 1})["label"]["en"], "With Treize")
        self.assertEqual(self.status("secret-route-treize", "ark", 72, vars={10: 0, 11: 1})["label"]["en"], "On your own")
        self.assertEqual(self.status("secret-route-beast-base", "ark", 81, vars={10: 0})["label"]["en"],
                         "Stayed with the main force")
        self.assertEqual(self.status("secret-route-gato-base", "ark", 108, vars={10: 1, 37: 3})["label"]["en"], "Base lost")

    def test_counts(self):
        trio = self.status("secret-rx-trio", "ark", 15, units={55, 58})
        self.assertEqual((trio["state"], trio["value"], trio["of"]), ("count", 2, 3))
        self.assertEqual(self.status("secret-endless-waltz", "brad", 90)["value"], 0)

    def test_every_rule_evaluates(self):
        for protagonist in STATUS.PROTAGONISTS:
            for scene in STATUS.reachable(self.graph[protagonist], STATUS.STARTS[protagonist]):
                for rule in self.rules:
                    with self.subTest(protagonist=protagonist, scene=scene, rule=rule):
                        self.assertIn(self.status(rule, protagonist, scene)["state"],
                                      {"done", "step", "not_started", "missed", "na", "info", "count", "chosen", "pending"})
