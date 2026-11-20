"""The compiler (web/src/lib/compiler, TypeScript), the one the page and the command line
share: run directly with node and through tools/mods/stage.py (which hands it the original
records a source copies and builds the maps) the images and what is wrong are the same.
Needs node 23 or later (it runs the TypeScript as it is)."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "mods"))
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "src"))
import stage  # noqa: E402

NODE = shutil.which("node")
SAY = {"ja": "「行くぞ」", "zh-Hans": "“走吧”", "en": "\"Let's go\""}


def node_version() -> int:
    try:
        return int(subprocess.run([NODE, "--version"], capture_output=True, text=True).stdout.strip().lstrip("v").split(".")[0])
    except (OSError, ValueError, TypeError):
        return 0


@unittest.skipUnless(NODE and node_version() >= 23, "node 23 or later is needed")
class WebCompilerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name)
        (self.source / "stages").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, stages: dict, **campaign) -> Path:
        for key, document in stages.items():
            (self.source / "stages" / f"{key}.json").write_text(json.dumps({"schema": stage.STAGE_SCHEMA, "map": 20, **document}), encoding="utf-8")
        (self.source / "campaign.json").write_text(json.dumps({"schema": stage.CAMPAIGN_SCHEMA, "id": "t.c", "name": {"ja": "テスト"}, "version": "1.2",
                                                               "start": next(iter(stages)), "route": 2, "stages": list(stages), **campaign}), encoding="utf-8")
        return self.source

    def web(self) -> dict:
        done = subprocess.run([NODE, str(ROOT / "web/scripts/compile-campaign.ts"), str(self.source)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def both(self):
        try:
            campaign, events, _ = stage.compile_campaign(self.source)
            ours = {"campaign": json.loads(json.dumps(campaign)), "story": json.loads(json.dumps(events))}
        except stage.StoryError as error:
            ours = {"error": str(error)}
        return ours, self.web()

    def opening(self, *more) -> dict:
        return {"when": "opening", "do": [{"bgm": 49}, {"op": "3D32", "args": [4]}, {"fade": "in"}, {"to_map": True}, *more, {"op": "3D48"}]}

    def test_the_same_images(self):
        units = [{"group": 1, "x": 22, "y": 5, "pilot": 28, "unit": 36, "faction": 0, "level": 2},
                 {"group": 0, "x": 6, "y": 5, "pilot": 298, "unit": 239, "faction": 1, "level": 0, "upgrade": 3, "behavior": 32768}]
        self.write({
            "one": {"title": {"ja": "一", "zh-Hans": "一"}, "units": {"place": units}, "setup": {"story_vars": {"9": 0}, "last_choice": 2},
                    "events": [
                        self.opening({"say": 28, "text": SAY}, {"deploy": 0}, {"deploy": 1}, {"say": 298, "mode": 1, "text": SAY}),
                        {"id": "wave", "when": {"turn": {"turn": 3, "side": "enemy"}}, "if": {"not_fired": ["boss"], "vars": {"9": 0}},
                         "do": [{"bgm": 27}, {"deploy": 2}, {"focus": 298}, {"say": 298, "text": SAY}, {"wait": 30},
                                {"if": {"var": 9, "is": 0}, "then": [{"set": {"9": 1}}], "else": [{"leave": 298}]}]},
                        {"id": "boss", "when": {"defeated": 298}, "set_flags": ["boss-down"], "do": [{"win": True}]},
                        {"id": "weak", "when": {"hp_below": {"who": 298, "percent": 30}}, "repeat": True, "do": [{"line": 17440}]},
                        {"id": "clash", "when": {"battle": {"between": [28, 298], "moment": "before"}}, "do": [{"sound": 12}]},
                        {"id": "few", "when": {"remaining": {"side": "enemy", "at_most": 3}}, "if": {"roster": {"pilots": [28], "units": [36]}}, "do": [{"deploy": 3}]},
                        {"id": "talk", "when": {"persuade": {"by": 28, "target": 298}}, "if": {"flags": ["boss-down"], "kills": {"28": 5}},
                         "do": [{"join": {"pilot": 298, "unit": 239, "level": {"like": 28}}}, {"leave_group": 2}]},
                        {"id": "late", "when": {"turn": {"turn": 13, "side": "player"}}, "do": [{"lose": True}]},
                        {"id": "more", "when": {"after": "end"}, "if": {"fired": ["talk"]}, "do": [{"join": {"pilot": 4, "unit": 27, "level": {"offset": -2}}}]},
                        {"when": "end", "do": [{"bgm": 49}, {"say": 28, "text": SAY}, {"close": True},
                                               {"choose": {"text": 18402, "options": [[{"next": "two"}], [{"next": "three"}]]}}]}],
                    "raw_events": [{"name": "raw", "type": 7, "header": [1, 0, 4, 0], "commands": [{"op": "3D4A", "note": "as the original"}]}]},
            "two": {"scene": 3, "events": [self.opening({"deploy": 0}), {"id": "gone", "when": {"enemies_gone": True}, "do": [{"win": True}]},
                                           {"when": "end", "do": [{"ending": True}]}],
                    "units": {"place": units[:1]}},
            "three": {"events": [self.opening(), {"when": "setup", "do": [{"wait": 10}]},
                                 {"id": "by", "when": {"enemies_gone": {"side": "enemy", "by_turn": 5}}, "do": [{"win": True}]},
                                 {"when": "end", "do": [{"next": "one"}]}]}})
        (self.source / "stages" / "one.meta.json").write_text(json.dumps({"schema": "srw64.stage-meta.v1", "cards": [{"kind": "win", "text": {"ja": "勝利"}}]}))
        (self.source / "status.json").write_text(json.dumps({"schema": "srw64.stage-status.v1", "rules": []}))
        self.write_campaign_status()
        ours, theirs = self.both()
        self.assertNotIn("error", ours, ours)
        self.assertNotIn("error", theirs, theirs)
        self.assertEqual(theirs["campaign"], ours["campaign"])
        self.assertEqual(theirs["story"], ours["story"])
        self.assertEqual(len(ours["story"]["patches"]), 3)

    def write_campaign_status(self):
        document = json.loads((self.source / "campaign.json").read_text())
        (self.source / "campaign.json").write_text(json.dumps({**document, "status": "status.json"}))

    def test_a_campaign_without_hosted_events(self):
        self.write({"one": {"events": [self.opening(), {"when": "end", "do": [{"ending": True}]}]}})
        ours, theirs = self.both()
        self.assertEqual(theirs, ours)
        self.assertIsNone(theirs["story"])

    def test_what_is_wrong_is_said_the_same(self):
        ok = {"events": [self.opening()]}
        cases = [
            {"one": {"events": [self.opening(), {"id": "x", "scene": 3, "when": {"defeated": 1}, "do": []}]}},
            {"one": {"events": [self.opening(), self.opening()]}},
            {"one": {"events": [{"when": "end", "do": []}]}},
            {"one": {"events": [self.opening(), {"when": "end", "do": [{"next": "nowhere"}]}]}},
            {"one": {"events": [self.opening(), {"when": "end", "do": [{"op": "3D4B", "args": [77]}]}]}},
            {"one": {**ok, "scene": 132}},
            {"one": {**ok, "scene": 3}, "two": {**ok, "scene": 3}},
            {"one": {**ok, "map": None}},
            {"one": {**ok, "maps": 1}},
            {"one": {"events": [{**self.opening(), "if": {"vars": {"1": 0}}}]}},
            {"one": {"events": [{"when": "start", "do": []}]}},
            {"one": {**ok, "units": {"place": [{"group": 0, "x": n, "y": 1, "pilot": 1, "unit": 1, "faction": 2} for n in range(31)]}}},
            {"one": {**ok, "units": {"place": [{"pilot": 1, "actor": 1}]}}},
            {"one": {"events": [self.opening({"join": {"pilot": 18, "unit": 27, "level": {"like": 4}}})]}},
            {"one": {"events": [self.opening(), {"id": "a", "when": {"turn": {"turn": 0}}, "do": []}]}},
            {"one": {"events": [self.opening(), {"id": "a", "when": {"defeated": 1}, "do": [{"say": 4, "text": {"zh-Hans": "没有日文"}}]}]}},
            {"one": {"events": [self.opening(), {"id": "a", "when": {"defeated": 1}, "do": [{"wait": 0}]}]}},
            {"one": {"events": [self.opening(), {"id": "a", "when": {"defeated": 1}, "if": {"fired": ["none"]}, "do": []}]}},
            {"one": {"events": [self.opening({"op": "3D38"})]}},
            {"one": {**ok, "units": {"place": [{"group": 70000, "x": 1, "y": 1}]}}},
            {"one": {**ok, "setup": {"story_vars": {"200": 1}}}},
        ]
        for stages in cases:
            with self.subTest(stages=json.dumps(stages, ensure_ascii=False)[:90]):
                for stale in (self.source / "stages").glob("*.json"):
                    stale.unlink()
                self.write(stages)
                ours, theirs = self.both()
                self.assertIn("error", ours)
                self.assertEqual(theirs, ours)


@unittest.skipUnless(NODE and node_version() >= 23, "node 23 or later is needed")
class WebConditionTests(WebCompilerTests):
    """The conditions only the TypeScript compiler knows (level-editor.md §5.3): new triggers,
    an event's new `if` keys as a test tree, a step's condition the host decides."""

    test_the_same_images = test_a_campaign_without_hosted_events = test_what_is_wrong_is_said_the_same = None

    def events(self, *rows, opening=()) -> dict:
        self.write({"one": {"events": [self.opening(*opening), *rows]}})
        out = self.web()
        self.assertNotIn("error", out, out)
        return out

    def test_new_triggers(self):
        out = self.events(
            {"id": "a", "when": {"defeated": {"who": 298, "by": 28}}, "do": []},
            {"id": "b", "when": {"downed": {"side": "player", "at_least": 2}}, "do": []},
            {"id": "c", "when": {"group_gone": 3}, "do": []},
            {"id": "d", "when": {"reach": {"who": 28, "area": [9, 4, 7, 2]}}, "do": []},
            {"id": "e", "when": {"reach": {"side": "enemy", "area": [1, 1, 3, 3]}}, "do": []},
            {"id": "f", "when": {"remaining": {"side": "player", "at_most": 1}}, "do": []},
            {"id": "g", "when": {"defeated": 18}, "do": []})
        got = {e["id"]: (e["trigger"], e["params"]) for e in out["story"]["events"]}
        self.assertEqual(got, {"one.a": ("defeated", [298, 28]), "one.b": ("downed", [0, -1, -1, 2]), "one.c": ("group_gone", [3]),
                               "one.d": ("reach", [28, -1, 7, 2, 9, 4]), "one.e": ("reach", [-1, 1, 1, 1, 3, 3]),
                               "one.f": ("remaining", [2, 1, 4]), "one.g": ("defeated", [18])})

    def test_an_events_new_conditions(self):
        out = self.events(
            {"id": "x", "when": {"turn": {"turn": 2}}, "do": []},
            {"id": "a", "when": {"turn": {"turn": 2}},
             "if": {"vars": {"9": 1}, "turn": {"to": 5}, "on_map": [18, 19], "hp": {"who": 18, "below": 50},
                    "any": [{"route": [0, 1]}, {"not": {"fired": ["x"]}}], "count": {"side": "enemy", "at_most": 3},
                    "in_area": {"side": "player", "area": [1, 2, 3, 4]}, "choice": 2, "funds": {"at_least": 5000},
                    "downed": {"group": 2, "at_least": 1}}, "do": []})
        event = out["story"]["events"][1]
        self.assertEqual(event["vars"], {"9": 1})                      # older keys stay in their fields
        self.assertEqual(event["test"], {"all": [
            {"turn": [0, 4]}, {"status": [18, "on_map"]}, {"status": [19, "on_map"]}, {"hp": [18, 50]},
            {"any": [{"route": [0, 1]}, {"not": {"fired": "one.x"}}]}, {"count": [1, 0, 3]}, {"in_area": [-1, 0, 1, 2, 3, 4]},
            {"choice": 2}, {"funds": [5000, 99999999]}, {"downed": [-1, 2, -1, 1]}]})
        self.assertNotIn("test", out["story"]["events"][0])

    def test_a_steps_condition(self):
        say = {"say": 28, "text": SAY}
        out = self.events(
            {"id": "a", "when": {"turn": {"turn": 2}}, "do": [
                {"if": {"roster": {"pilots": [18]}}, "then": [say], "else": [{"bgm": 3}]},
                {"if": {"var": 9, "is": 1}, "then": [{"bgm": 4}]}]},
            opening=[{"if": {"flags": ["met"], "not": {"on_map": 18}}, "then": [say]}])
        event = out["story"]["events"][0]
        self.assertEqual(event["listing"][:8], ["3E14 65532", "3E08 1", "3D3E 0", "3E14 1", "3E1D", "3E08 0", "3D3A 3", "3E1D"])
        self.assertEqual(event["listing"][8:10], ["3E14 0", "3E03 9,1,0"])                # a variable stays the original's test
        self.assertEqual(event["checks"], [[1, {"roster": {"pilots": [18], "units": []}}]])
        self.assertEqual(event["words"][1], 0xFFFC)
        stage = out["campaign"]["stages"][0]
        block = bytes.fromhex(stage["image"]["events_hex"])
        checks = [p for p in out["story"]["patches"] if "test" in p]
        self.assertEqual(len(checks), 1)
        self.assertEqual(checks[0]["test"], {"all": [{"flag": "met"}, {"not": {"status": [18, "on_map"]}}]})
        self.assertEqual(block[checks[0]["at"]:checks[0]["at"] + 2], b"\xff\xfc")
        self.assertEqual(block[checks[0]["at"] - 2:checks[0]["at"]], b"\x3e\x14")

    def test_a_units_behaviour(self):
        place = [{"group": 1, "x": 22, "y": 5, "pilot": 28, "unit": 36, "faction": 0},
                 {"group": 0, "x": 21, "y": 5, "pilot": 287, "unit": 266, "faction": 1, "ai": {"mode": "goto", "wait": 2, "to": [9, 4]}, "drop": 5, "entry": 3},
                 {"group": 0, "x": 6, "y": 20, "pilot": 298, "unit": 239, "faction": 1, "ai": {"mode": "guard", "target": 287}},
                 {"group": 0, "x": 7, "y": 20, "pilot": 298, "unit": 239, "faction": 1, "ai": {"mode": "accurate", "wait": 31}}]
        self.write({"one": {"units": {"place": place}, "events": [self.opening()]}})
        out = self.web()
        self.assertNotIn("error", out, out)
        rows = [{k: r[k] for k in ("raw14", "raw16", "raw18", "behavior", "extra", "raw26")} for r in out["campaign"]["stages"][0]["image"]["deployments"]]
        self.assertEqual(rows, [{"raw14": 0, "raw16": 0, "raw18": 0xFFFF, "behavior": 0, "extra": 0, "raw26": 0},    # leaves no part
                                {"raw14": 2, "raw16": 3, "raw18": 5, "behavior": 3, "extra": 9, "raw26": 4},
                                {"raw14": 0, "raw16": 5, "raw18": 0xFFFF, "behavior": 0, "extra": 287, "raw26": 0},
                                {"raw14": 31, "raw16": 1, "raw18": 0xFFFF, "behavior": 0, "extra": 0, "raw26": 0}])
        for row, said in [({"ai": {"mode": "hunt"}}, "要写 target"), ({"ai": {"mode": "goto", "target": 4}}, "要写 to"),
                          ({"ai": {"mode": "dance"}}, "ai 写"), ({"ai": {"mode": "attack"}, "raw16": 1}, "同时写了 ai"), ({"drop": 99}, "drop")]:
            with self.subTest(said=said):
                self.write({"one": {"units": {"place": [{**place[1], "ai": None, "drop": None, "entry": 0, **row}]}, "events": [self.opening()]}})
                self.assertIn(said, self.web().get("error", ""))

    def test_what_is_wrong(self):
        for row, said in [
            ({"when": {"reach": {"who": 1, "side": "enemy", "area": [1, 1, 2, 2]}}}, "who（人物号）或 side"),
            ({"when": {"reach": {"who": 1, "area": [1, 1, 2]}}}, "[x0, y0, x1, y1]"),
            ({"when": {"downed": {"at_least": 2}}}, "side、group、who 至少一个"),
            ({"when": {"group_gone": 3}, "repeat": True}, "不能 repeat"),
            ({"when": {"defeated": {"who": 1, "by": 0}}}, "defeated.by"),
            ({"when": {"turn": {"turn": 1}}, "if": {"turn": {"from": 5, "to": 2}}}, "turn.from 不能大于"),
            ({"when": {"turn": {"turn": 1}}, "if": {"any": [{"route": 1}]}}, "两个以上"),
            ({"when": {"turn": {"turn": 1}}, "if": {"weather": 1}}, "if 只能写"),
            ({"when": {"turn": {"turn": 1}}, "do": [{"if": {"fired": ["none"]}, "then": []}]}, "不存在的事件 one.none"),
            ({"when": {"turn": {"turn": 1}}, "do": [{"if": {"weather": 1}, "then": []}]}, "if 不认识 weather"),
        ]:
            with self.subTest(said=said):
                self.write({"one": {"events": [self.opening(), {"id": "a", "do": [], **row}]}})
                self.assertIn(said, self.web().get("error", ""))


@unittest.skipUnless(NODE and node_version() >= 23 and (ROOT / "rom.z64").exists(), "node 23 or later and the ROM are needed")
class WebMapToolTests(unittest.TestCase):
    """web/src/lib/compiler/map.ts: its layout, atlas and palette read back with the reader of
    the original's maps (the colours are its own quantizer's, so not the command line's bit
    for bit)."""

    def build(self, scale: int, like: int = 20, terrain=None) -> dict:
        import struct
        from test_map_tool import COLUMNS, ROWS, picture
        import map_tool
        sys.path.insert(0, str(ROOT / "src"))
        rom = (ROOT / "rom.z64").read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            image = picture(scale)
            (temp / "picture.rgba").write_bytes(image.convert("RGBA").tobytes())
            rows = terrain or [[1 if 7 <= y <= 8 else 5 for _ in range(COLUMNS)] for y in range(ROWS)]
            (temp / "map.json").write_text(json.dumps({"like": like, "scale": scale, "terrain": rows}))
            templates = {}
            for number in range(map_tool.MAPS):
                if struct.unpack_from(">5H2B", rom, map_tool.MAP_RECORDS + number * 12)[5] == 0:
                    row = map_tool.template(rom, number)
                    templates[str(number)] = {"map": number, "layout": row["layout"], "atlas": row["atlas"], "palette": row["palette"],
                                              "palette_hex": row["palette_bytes"].hex(), "cycles": row["cycles"]}
            (temp / "templates.json").write_text(json.dumps(templates))
            done = subprocess.run([NODE, str(ROOT / "web/scripts/build-map.ts"), str(temp / "map.json"), str(temp / "picture.rgba"),
                                   str(image.width), str(image.height), str(temp / "templates.json")], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def test_the_original_reads_it(self):
        import struct
        from PIL import ImageChops
        from srw64_native.original_images import decode_indexed, decode_map
        from test_map_tool import COLUMNS, ROWS, picture
        import map_tool
        built = self.build(2)
        self.assertNotIn("error", built, built)
        image, meta = built["image"], built["meta"]
        self.assertEqual((image["schema"], image["like"], image["columns"], image["rows"], image["hd"], image["replaces"]),
                         (map_tool.IMAGE_SCHEMA, 20, COLUMNS, ROWS, True, {"layout": 6284, "atlas": 6228, "palette": 6243}))
        layout, atlas, palette = (bytes.fromhex(image[f"{name}_hex"]) for name in ("layout", "atlas", "palette"))
        drawn, info = decode_map(layout, decode_indexed(atlas, palette))
        self.assertEqual((info["grid_columns"], info["grid_rows"]), (COLUMNS, ROWS))
        wanted = picture(1)
        self.assertLess(max(abs(a - b) for a, b in zip(drawn.convert("RGB").tobytes(), wanted.tobytes())), 9)   # five bits a channel
        cell = lambda x, y: struct.unpack_from(">2H", layout, 8 + (y * COLUMNS + x) * 4)
        self.assertEqual(cell(4, 3)[1], cell(6, 3)[1])                                   # a house and its mirror image: one tile
        self.assertEqual((cell(4, 3)[0] ^ cell(6, 3)[0]) & 0xE000, map_tool.FLIP_H)
        self.assertNotIn(0, {cell(x, y)[1] for y in range(ROWS) for x in range(COLUMNS)})   # tile 0 is not drawn
        terrain = [[cell(x, y)[0] & 0xFF for x in range(COLUMNS)] for y in range(ROWS)]
        self.assertEqual((terrain[0][5], terrain[1][5], terrain[5][1], terrain[2][2], terrain[7][2], terrain[9][5]), (99, 99, 99, 5, 1, 5))
        like = map_tool.template((ROOT / "rom.z64").read_bytes(), 20)
        cycled = {i for first, count in like["cycles"] for i in range(first, first + count)}
        self.assertFalse(set(atlas[8:]) & (cycled | {0}))
        for index in cycled:
            self.assertEqual(palette[8 + index * 2:10 + index * 2], like["palette_bytes"][8 + index * 2:10 + index * 2])
        self.assertEqual((meta["schema"], meta["layout"], meta["width"], meta["height"], meta["scale"], len(meta["reference_palette"])),
                         ("srw64.hd-map-runtime.v0", 6284, COLUMNS * 16, ROWS * 16, 2, 256))
        self.assertIsNone(self.build(1)["meta"])

    def test_what_is_wrong_is_said_the_same(self):
        from test_map_tool import COLUMNS
        import map_tool
        import struct
        rom = (ROOT / "rom.z64").read_bytes()
        colony = next(m for m in range(map_tool.MAPS) if struct.unpack_from(">5H2B", rom, map_tool.MAP_RECORDS + m * 12)[5])
        self.assertIn("terrain 要是", self.build(2, terrain=[[0] * COLUMNS])["error"])
        self.assertIn("不能当模板", self.build(2, like=colony)["error"])


if __name__ == "__main__":
    unittest.main()
