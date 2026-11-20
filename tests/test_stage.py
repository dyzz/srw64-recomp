"""tools/mods/stage.py: a campaign written stage by stage (docs/design/level-editor.md §3)
becomes the campaign image and the story image the host plays."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "mods"))
import stage  # noqa: E402
import story  # noqa: E402
from recomp.script_lab import mini_stage  # noqa: E402

RECORDS = ROOT / "assets/original-data/records/stage_auxiliary.jsonl"
SAY = {"ja": "「行くぞ」", "zh-Hans": "“走吧”"}


def words(image: dict, kind: int) -> list[int]:
    """The words of the stage image's event of that type, header included."""
    event, = [e for e in image["events"] if e["type"] == kind]
    raw = bytes.fromhex(image["events_hex"])[event["offset"]:event["offset"] + event["size"]]
    return [int.from_bytes(raw[i:i + 2], "big") for i in range(0, len(raw), 2)]


class StageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name)
        (self.source / "stages").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, stages: dict, **campaign) -> Path:
        for key, document in stages.items():
            (self.source / "stages" / f"{key}.json").write_text(json.dumps({"schema": stage.STAGE_SCHEMA, "map": 20, **document}), encoding="utf-8")
        (self.source / "campaign.json").write_text(json.dumps({"schema": stage.CAMPAIGN_SCHEMA, "id": "t.c", "start": next(iter(stages)), "route": 2,
                                                               "stages": list(stages), **campaign}), encoding="utf-8")
        return self.source

    def opening(self, *more) -> dict:
        return {"when": "opening", "do": [{"to_map": True}, *more]}

    def test_a_stage_splits_between_the_original_slots_and_the_host(self):
        campaign, events, _ = stage.compile_campaign(self.write({
            "one": {"title": "一", "events": [
                self.opening({"say": 4, "text": SAY}, {"deploy": 0}),
                {"id": "t3", "when": {"turn": {"turn": 3, "side": "enemy"}}, "if": {"not_fired": ["boss"]}, "do": [{"say": 5, "text": SAY}, {"lose": True}]},
                {"id": "boss", "when": {"defeated": 18}, "do": [{"win": True}]},
                {"when": "end", "do": [{"next": "two"}]}]},
            "two": {"events": [self.opening(), {"when": "end", "do": [{"say": 4, "text": SAY}, {"ending": True}]}]}}))
        self.assertEqual((campaign["schema"], campaign["id"], campaign["start_scene"]), (stage.IMAGE_SCHEMA, "t.c", 1))
        self.assertEqual([(s["key"], s["scene"], s["image"]["slot"], s["title"]) for s in campaign["stages"]], [("one", 1, 1, "一"), ("two", 2, 2, None)])
        one, two = (s["image"] for s in campaign["stages"])
        # The opening: its header, 3DD0, 3D4D, the line's place, 3D45 0, FFFF.
        self.assertEqual(words(one, 12), [12, 0, 0, 0, 0, 0x3DD0, 0x3D4D, 0x3D3E, story.TEXT, 0x3D45, 0, 0xFFFF])
        self.assertEqual(words(one, 14), [14, 0, 0, 0, 0, 0x3DD0, 0x3D4B, 2, 0xFFFF])
        self.assertEqual(words(two, 14), [14, 0, 0, 0, 0, 0x3DD0, 0x3D3E, story.TEXT, 0x3D71, 0xFFFF])
        self.assertNotIn("source", one)
        # The others go to the host, named after their stage, on the stage's scene.
        self.assertEqual((events["schema"], events["campaign"]), (story.IMAGE_SCHEMA, "t.c"))
        self.assertEqual([(e["id"], e["scene"], e["trigger"], e["params"]) for e in events["events"]],
                         [("one.t3", 1, "turn", [2, 2]), ("one.boss", 1, "defeated", [18])])
        self.assertEqual(events["events"][0]["not_fired"], ["one.boss"])
        self.assertEqual(events["events"][0]["words"][-2:], [0x3D4C, 0xFFFF])
        self.assertEqual(events["events"][1]["words"], [0x3D4A, 0xFFFF])
        # Lines: the host's event's first, then the three events' in the stages' order;
        # each of those has its place in its scene's event block.
        self.assertEqual([t["speaker"] for t in events["texts"]], [5, 4, 4])
        opening = next(e for e in one["events"] if e["type"] == 12)
        end = next(e for e in two["events"] if e["type"] == 14)
        self.assertEqual(events["patches"], [{"scene": 1, "at": opening["offset"] + 16, "text": 1},
                                             {"scene": 2, "at": end["offset"] + 14, "text": 2}])

    def test_scenes_are_handed_out_in_order_around_the_ones_asked_for(self):
        stages = {key: {"events": [self.opening()]} for key in ("a", "b", "c", "d")}
        stages["c"]["scene"] = 2
        campaign, events, _ = stage.compile_campaign(self.write(stages))
        self.assertEqual([s["scene"] for s in campaign["stages"]], [1, 3, 2, 10])   # 4-9 are not handed out
        self.assertIsNone(events)                                                 # nothing for the host

    def test_more_events_than_the_original_holds(self):
        many = [{"id": f"e{n}", "when": {"turn": {"turn": n + 1, "side": "player"}}, "do": [{"wait": 30}]} for n in range(90)]
        campaign, events, _ = stage.compile_campaign(self.write({"one": {"events": [self.opening(), *many]}}))
        self.assertEqual((len(campaign["stages"][0]["image"]["events"]), len(events["events"])), (1, 90))

    def test_a_choice_and_raw_events(self):
        campaign, _, _ = stage.compile_campaign(self.write({
            "one": {"events": [self.opening(), {"when": "end", "do": [
                {"choose": {"text": 18402, "options": [[{"next": "two"}], [{"next": "one"}]]}}]}],
                    "raw_events": [{"type": 7, "header": [1, 0, 4, 0], "commands": [{"op": "3D4A"}]}]},
            "two": {"events": [self.opening()]}}))
        one = campaign["stages"][0]["image"]
        self.assertEqual(words(one, 14)[5:], [0x3DD0, 0x3D44, 0, 2, 18402, 0x3E10, 0x3D4B, 2, 0x3E1D, 0x3E11, 0x3D4B, 1, 0x3E1D, 0xFFFF])
        self.assertEqual(words(one, 7), [7, 1, 0, 4, 0, 0x3D4A, 0xFFFF])

    def test_units_are_placed_by_this_formats_names(self):
        campaign, _, _ = stage.compile_campaign(self.write({"one": {"events": [self.opening()], "units": {"place": [
            {"group": 0, "x": 3, "y": 4, "pilot": 300, "unit": 266, "faction": 1, "level": 2}]}}}))
        record, = campaign["stages"][0]["image"]["deployments"]
        self.assertEqual((record["actor"], record["level_offset"], record["unit"], record["x"]), (300, 2, 266, 3))

    def test_what_is_wrong_is_said(self):
        ok = {"events": [self.opening()]}
        cases = {
            "场景号由编译器分配": {"one": {"events": [self.opening(), {"id": "x", "scene": 3, "when": {"defeated": 1}, "do": []}]}},
            "只能有一个": {"one": {"events": [self.opening(), self.opening()]}},
            "要有开场事件": {"one": {"events": [{"when": "end", "do": []}]}},
            "没有关卡": {"one": {"events": [self.opening(), {"when": "end", "do": [{"next": "nowhere"}]}]}},
            "指到战役外面": {"one": {"events": [self.opening(), {"when": "end", "do": [{"op": "3D4B", "args": [77]}]}]}},
            "不能借": {"one": {**ok, "scene": 132}},
            "都写了 scene": {"one": {**ok, "scene": 3}, "two": {**ok, "scene": 3}},
            "要写 map": {"one": {**ok, "map": None}},
            "不认识的字段": {"one": {**ok, "maps": 1}},
            "条件写在 do 里": {"one": {"events": [{**self.opening(), "if": {"vars": {"1": 0}}}]}},
            "只能是 opening": {"one": {"events": [{"when": "start", "do": []}]}},
            "超过 30 个": {"one": {**ok, "units": {"place": [{"group": 0, "x": n, "y": 1, "pilot": 1, "unit": 1, "faction": 2} for n in range(31)]}}},
            "同时写了": {"one": {**ok, "units": {"place": [{"pilot": 1, "actor": 1}]}}},
            "放到": {"one": {"events": [self.opening({"join": {"pilot": 18, "unit": 27, "level": {"like": 4}}})]}},
        }
        for message, stages in cases.items():
            with self.subTest(message=message), self.assertRaises(stage.StoryError) as caught:
                stage.compile_campaign(self.write(stages))
            self.assertIn(message, str(caught.exception))
        with self.assertRaises(stage.StoryError):
            stage.compile_campaign(self.write({"one": ok}, start="two"))
        with self.assertRaises(stage.StoryError) as caught:
            stage.compile_campaign(self.write({"one": ok}, route=None))
        self.assertIn("没有主角路线", str(caught.exception))
        campaign, _, _ = stage.compile_campaign(self.write({"one": ok, "two": {**ok, "setup": {"route": 0}}}))
        self.assertEqual([s["image"]["route"] for s in campaign["stages"]], [2, 0])   # the campaign's, or the stage's own
        with self.assertRaises(story.StoryError):   # a campaign's actions stay out of the main story
            story.compile_story({"schema": story.SCHEMA, "events": [{"id": "a", "scene": 1, "when": {"defeated": 1}, "do": [{"win": True}]}]})

    @unittest.skipUnless(RECORDS.exists(), "original data not extracted")
    def test_the_sample_campaign(self):
        source = ROOT / "config/recomp/campaigns/sample"
        self.assertTrue(stage.is_source(source))
        campaign, events, _ = stage.compile_campaign(source)
        self.assertEqual([(s["key"], s["scene"], s["image"]["map"]) for s in campaign["stages"]],
                         [("prologue", 1, 20), ("crossroads", 2, 19), ("ending-a", 3, 0), ("ending-b", 4, 21)])
        self.assertEqual([mini_stage.next_scenes(s["image"]) for s in campaign["stages"]], [[2], [3, 4], [], []])
        self.assertEqual(campaign["stages"][1]["title"]["zh-Hans"], "第2话　岔路")
        # Each stage's turn 1 wins by an event of the host's; nothing new is said.
        self.assertEqual([(e["id"], e["trigger"], e["params"], e["words"][-2:]) for e in events["events"]],
                         [(f"{key}.turn-1", "turn", [0, 1], [0x3D4A, 0xFFFF]) for key in ("prologue", "crossroads", "ending-a", "ending-b")])
        self.assertEqual((events["texts"], "patches" in events), ([], False))
        # The stage info page's metadata rides with its stage; the status rules get the graph.
        self.assertEqual((campaign["stages"][1]["meta"]["schema"], campaign["stages"][1]["meta"]["scene"]), ("srw64.stage-meta.v1", 2))
        self.assertNotIn("meta", campaign["stages"][0])
        self.assertEqual(campaign["status"]["graph"]["brad"], {"1": [2], "2": [3, 4], "3": [], "4": []})
        # The opening and end events are the bytes the earlier source format compiled to
        # (the prologue's, as its mini stage source had them).
        opening = [{"op": "3DD0"}, {"op": "3D3A", "args": [49]}, {"op": "3D32", "args": [4]}, {"op": "3D32", "args": [0]},
                   {"op": "3D3B", "args": [1]}, {"op": "3D3F", "args": [17410]}, {"op": "3D3E", "args": [17411]},
                   {"op": "3D3B", "args": [0]}, {"op": "3D4D"}, {"op": "3D65", "args": [17, 31]}, {"op": "3D3B", "args": [1]},
                   {"op": "3D45", "args": [0]}, {"op": "3D3A", "args": [27]}, {"op": "3D45", "args": [1]}, {"op": "3D45", "args": [2]},
                   {"op": "3D3E", "args": [17464]}, {"op": "3D48"}]
        ending = [{"op": "3DD0"}, {"op": "3D3A", "args": [49]}, {"op": "3D32", "args": [1]}, {"op": "3D3B", "args": [1]},
                  {"op": "3D3E", "args": [17486]}, {"op": "3D47"}, {"op": "3D4B", "args": [2]}]
        before = mini_stage.compile_stage({"schema": mini_stage.SCHEMA, "map": 20, "deployments_from": "base:stage_auxiliary:001e4268",
                                           "events": [{"type": 12, "commands": opening}, {"type": 14, "commands": ending}]})
        prologue = campaign["stages"][0]["image"]
        self.assertEqual((prologue["events_hex"], prologue["aux_hex"]), (before["events_hex"], before["aux_hex"]))


if __name__ == "__main__":
    unittest.main()
