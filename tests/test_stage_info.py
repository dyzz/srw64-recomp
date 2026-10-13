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
