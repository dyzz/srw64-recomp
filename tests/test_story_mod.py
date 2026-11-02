"""tools/mods/story.py: a story MOD source becomes the image the host loads
(src/host/story_mod.hpp, docs/design/mod-packages.md §7), and pack.py compiles it into
the package."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "config/recomp/mods/master-asia"


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"tools/mods/{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


story = load("story")
pack = load("pack")


def source(*events):
    return {"schema": story.SCHEMA, "events": list(events)}


def event(**fields):
    row = {"id": "e", "scene": 129, "when": {"defeated": 18}, "do": [{"set": {"131": 3}}]}
    row.update(fields)
    return row


class StoryTests(unittest.TestCase):
    def test_example(self):
        image = story.compile_story(json.loads((EXAMPLE / "story/master.json").read_text(encoding="utf-8")))
        self.assertEqual(image["schema"], story.IMAGE_SCHEMA)
        persuade, joins = image["events"]
        self.assertEqual((persuade["trigger"], persuade["params"], persuade["vars"]), ("persuade", [4, 18], {"131": 0}))
        self.assertEqual((joins["trigger"], joins["fired"]), ("after_end", ["persuade"]))
        self.assertEqual(joins["words"][-6:], [0x3D5A, 18, 0, 27, 500, 0xFFFF])
        self.assertEqual(len(image["texts"]), 7)
        # Each line's place holds the placeholder; its index is in "texts".
        for at, index in persuade["texts"] + joins["texts"]:
            self.assertLess(index, 7)
        self.assertEqual([persuade["words"][at] for at, _ in persuade["texts"]], [story.TEXT] * 5)
        self.assertEqual(image["texts"][1]["speaker"], 18)

    def test_if_else_as_the_original(self):
        image = story.compile_story(source(event(do=[{"if": {"var": 128, "is": 2}, "then": [{"deploy": 12}], "else": [{"deploy": 11}]}])))
        self.assertEqual(image["events"][0]["words"],
                         [0x3E14, 0, 0x3E03, 128, 2, 1, 0x3D45, 12, 0x3E14, 1, 0x3E1D, 0x3E08, 0, 0x3D45, 11, 0x3E1D, 0xFFFF])
        image = story.compile_story(source(event(do=[{"if": {"var": 131, "is": 1}, "then": [{"bgm": 11}]}])))
        self.assertEqual(image["events"][0]["words"], [0x3E14, 0, 0x3E03, 131, 1, 0, 0x3D3A, 11, 0x3E1D, 0xFFFF])

    def test_triggers(self):
        cases = [({"turn": {"turn": 3, "side": "enemy"}}, "turn", [2, 2]),
                 ({"battle": {"between": [4, 0], "moment": "before"}}, "battle_before", [4, 0]),
                 ({"enemies_gone": True}, "enemies_gone", [4, 254]),
                 ({"remaining": {"side": "third", "at_most": 2}}, "remaining", [1, 2, 4]),
                 ({"hp_below": {"who": 18, "percent": 30}}, "hp_below", [18, 30]),
                 ({"after": "opening"}, "after_opening", [])]
        for when, name, params in cases:
            got = story.compile_story(source(event(when=when)))["events"][0]
            self.assertEqual((got["trigger"], got["params"]), (name, params), when)

    def test_mistakes(self):
        bad = [
            source(event(when={"turn": {"turn": 0}})),
            source(event(when={"after": "middle"})),
            source(event(do=[{"say": 4, "text": {"zh-Hans": "无日文"}}])),
            source(event(do=[{"say": 4, "text": {"ja": "あ<STOP>い"}}])),
            source(event(do=[{"say": 4, "text": {"ja": "あ" * 201}}])),
            source(event(do=[{"jump": 3}])),
            source(event(do=[{"set": {"131": 4}}])),
            source(event(do=[{"op": "3D45", "args": [1, 2]}])),
            source(event(), event()),
            source(event(**{"if": {"fired": ["nope"]}})),
            source(event(when={"after": "end"}, repeat=True)),
            {"schema": story.SCHEMA, "events": [], "extra": 1},
        ]
        for document in bad:
            with self.assertRaises(story.StoryError, msg=json.dumps(document, ensure_ascii=False)[:120]):
                story.compile_story(document)

    def test_pack_compiles_the_story(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder, _, manifest = pack.build_package(EXAMPLE, Path(tmp), archive=False)
            self.assertEqual(manifest["id"], "srw64.master-asia")
            image = json.loads((folder / "story/master.json").read_text(encoding="utf-8"))
            self.assertEqual(image["schema"], story.IMAGE_SCHEMA)
            self.assertTrue((folder / "data/pilots.json").is_file())


if __name__ == "__main__":
    unittest.main()
