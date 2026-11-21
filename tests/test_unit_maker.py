"""web/src/lib/unit-package.ts, the unit MOD maker's package (docs/design/unit-mod-web-tool.md
W1), run under Node: what it writes passes tools/mods/validate.py, reads back the same, keeps
other makers' rows, and its own problem list catches what the validator would refuse."""
import base64
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "web/src/lib/unit-package.ts"
NODE = shutil.which("node")

spec = importlib.util.spec_from_file_location("validate", ROOT / "tools/mods/validate.py")
validate = importlib.util.module_from_spec(spec)
sys.modules["validate"] = validate
spec.loader.exec_module(validate)

MODEL = {
    "pilots": [{"slug": "kai", "copy": 166, "voice": 166, "love": [{"with": 166, "mutual": True}], "combo_as": 166,
                "portrait": {"image": "art/kai.png", "hd": "art/kai-hd.png"},
                "names": {"ja": {"name": "カイ", "full": "カイ・テスト"}, "zh-Hans": {"name": "凯", "full": ""}, "en": {"name": "Kai"}},
                "data": {"melee": 200, "spirits": [["focus", 1], ["valor", 5]]}}],
    "units": [{"slug": "z", "copy": 48, "weapons": [193, 194, "@duo"], "forms": ["@wr"], "crew": [166], "inherits_from": 48, "work": 3,
               "names": {"ja": "Zテスト", "zh-Hans": "Z测试"}, "models": {"en": "MSZ-T"}, "data": {"hp": 6000, "terrain": {"space": "A"}}},
              {"slug": "wr", "copy": 49, "weapons": [199], "names": {"ja": "WRテスト"}, "models": {}, "data": {}}],
    "weapons": [{"slug": "duo", "copy": 196, "combination": {"units": ["@z", 228], "parts": [196, 899], "multiplier": 1.5},
                 "names": {"ja": "ダブル", "en": "Double"}, "data": {"en": 20, "ammo": -1}}],
}


def run(script: str):
    result = subprocess.run([NODE, "--input-type=module", "-e", script], capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout)


def call(function: str, *args):
    imports = f"import * as m from {json.dumps(MODULE.as_uri())};"
    return run(f"{imports} const args = {json.dumps(list(args))}; console.log(JSON.stringify(m.{function}(...args)));")


@unittest.skipUnless(NODE, "node is not installed")
class UnitMakerTests(unittest.TestCase):
    def test_files_pass_the_validator(self):
        files = call("toFiles", MODEL)
        self.assertEqual(set(files), {"data/new_units.json", "data/pilots.json", "data/units.json", "data/weapons.json", "dialogue/names.json"})
        self.assertEqual(validate.new_units_problem(files["data/new_units.json"], {"art/kai.png", "art/kai-hd.png"}), "")
        for name in ("pilots.json", "units.json", "weapons.json"):
            self.assertEqual(validate.data_problem(name, files[f"data/{name}"]), "", name)
        self.assertEqual(validate.names_problem(files["dialogue/names.json"]), "")
        doc = files["data/new_units.json"]
        self.assertNotIn("voice", doc["pilots"]["kai"])        # the copy's voice is the default
        self.assertEqual(files["dialogue/names.json"]["pilots"]["@kai"]["zh-Hans"], {"name": "凯"})   # an empty full name is left out
        self.assertEqual(files["dialogue/names.json"]["models"], {"@z": {"en": "MSZ-T"}})
        self.assertNotIn("@wr", files["data/units.json"])        # nothing changed

    def test_read_back(self):
        files = call("toFiles", MODEL)
        again = run(f"import * as m from {json.dumps(MODULE.as_uri())}; const f = {json.dumps(files)};"
                    "console.log(JSON.stringify(m.toFiles(m.fromFiles(f).model, m.fromFiles(f).kept)));")
        self.assertEqual(again, files)

    def test_other_rows_kept(self):
        kept = {"data/units.json": {"5": {"hp": 100}, "@gone": {"hp": 1}},
                "dialogue/names.json": {"schema": "srw64.mod-names.v1", "units": {"5": {"ja": "名前"}, "@gone": {"ja": "x"}}, "unit": {"ja": "部隊"}}}
        files = call("toFiles", MODEL, kept)
        self.assertEqual(files["data/units.json"], {"5": {"hp": 100}, "@z": {"hp": 6000, "terrain": {"space": "A"}}})
        self.assertEqual(files["dialogue/names.json"]["units"], {"5": {"ja": "名前"}, "@z": {"ja": "Zテスト", "zh-Hans": "Z测试"}, "@wr": {"ja": "WRテスト"}})
        self.assertEqual(files["dialogue/names.json"]["unit"], {"ja": "部隊"})

    def test_problems(self):
        self.assertEqual(call("problems", MODEL), [])
        bad = json.loads(json.dumps(MODEL))
        bad["units"][0]["forms"] = ["@nope"]
        bad["units"][1]["weapons"] = []
        bad["weapons"][0]["combination"]["parts"] = [196]
        bad["pilots"][0]["names"] = {"ja": {"name": "<カイ>"}}
        bad["units"].append({"slug": "Z", "copy": 1, "weapons": [1], "names": {"ja": "x"}, "models": {}, "data": {}})
        codes = {(p["where"], p["code"]) for p in call("problems", bad)}
        self.assertTrue({("units.z", "forms_bad"), ("units.wr", "weapons_count"), ("weapons.duo", "combination_parts"),
                         ("pilots.kai", "name_chars"), ("units.Z", "slug_bad")} <= codes, codes)
        # The validator refuses the same things.
        files = call("toFiles", bad)
        self.assertNotEqual(validate.new_units_problem(files["data/new_units.json"]), "")
        self.assertNotEqual(validate.names_problem(files["dialogue/names.json"]), "")

    def test_final_dancouga_reads_back(self):
        # The sample package (sprites, scenes, an animation record, a combination, a crew):
        # read and written again, every file the maker owns comes out the same.
        folder = ROOT / "config/recomp/mods/final-dancouga"
        files = {path: json.loads((folder / path).read_text(encoding="utf-8"))
                 for path in ("data/new_units.json", "data/units.json", "data/weapons.json", "dialogue/names.json")}
        again = run(f"import * as m from {json.dumps(MODULE.as_uri())}; const f = {json.dumps(files, ensure_ascii=False)};"
                    "const r = m.fromFiles(f); console.log(JSON.stringify(m.toFiles(r.model, r.kept)));")
        self.assertEqual(again, files)

    def test_rename(self):
        script = (f"import * as m from {json.dumps(MODULE.as_uri())}; const model = {json.dumps(MODEL)};"
                  "m.rename(model, 'weapons', 'duo', 'pair'); m.rename(model, 'units', 'z', 'zeta'); console.log(JSON.stringify(model));")
        model = run(script)
        self.assertEqual(model["units"][0]["weapons"][-1], "@pair")
        self.assertEqual(model["weapons"][0]["combination"]["units"][0], "@zeta")
        self.assertEqual(call("problems", model), [])

    def test_combine_icon_moves(self):
        # A combined unit without "over" leaves it out; an icon number out of range and an
        # original combined unit past 362 are problems the validator also refuses.
        model = {**MODEL, "units": [{**MODEL["units"][0], "forms": [], "combine": {"parts": ["@wr", 228], "over": []}, "icon": 978,
                                     "data": {"move_types": ["air", "space"], "funds": 3000, "exp": 120}}, MODEL["units"][1]]}
        files = call("toFiles", model)
        self.assertEqual(files["data/new_units.json"]["units"]["z"]["combine"], {"parts": ["@wr", 228]})
        self.assertEqual(validate.new_units_problem(files["data/new_units.json"]), "")
        self.assertEqual(validate.data_problem("units.json", files["data/units.json"]), "")
        self.assertEqual(call("problems", model), [])
        bad = {**model, "units": [{**model["units"][0], "icon": 2000, "combine": {"parts": ["@wr", 228], "over": [400]}}, MODEL["units"][1]]}
        self.assertEqual({p["code"] for p in call("problems", bad)}, {"icon_bad", "combine_over"})
        bad_files = call("toFiles", bad)
        self.assertNotEqual(validate.new_units_problem(bad_files["data/new_units.json"]), "")
        # Without "over", a part that combines in the original (219, Dancouga's eagle) is refused.
        taken = {**model, "units": [{**model["units"][0], "combine": {"parts": [219, 228], "over": []}}, MODEL["units"][1]]}
        self.assertEqual({p["code"] for p in call("problems", taken)}, {"combine_recipe_taken"})
        self.assertIn("219", validate.new_units_problem(call("toFiles", taken)["data/new_units.json"]))
        over = {**model, "units": [{**model["units"][0], "combine": {"parts": [219, 228], "over": [224]}}, MODEL["units"][1]]}
        self.assertEqual(call("problems", over), [])

    def test_quotes(self):
        # A character's own lines: empty ones left out, the file the validator takes, read back,
        # other characters' entries kept; the voices they name.
        quotes = {"attack": [{"ja": "行くぞ！", "zh-Hans": "上了！", "voice": "kai-1.ogg"}, {"ja": "", "en": ""}], "evade": [],
                  "special": [{"weapon": "@duo", "sometimes": True, "lines": [{"ja": "合わせろ！"}]},
                              {"opponent": 298, "situation": "evade", "lines": [{"ja": "遅い！"}]},
                              {"opponent": 298, "situation": "attack", "sometimes": True, "lines": [{"ja": "そこだ！"}]}]}
        model = {**MODEL, "pilots": [{**MODEL["pilots"][0], "quotes": quotes}]}
        kept = {"dialogue/battle_quotes.json": {"schema": "srw64.battle-quotes.v1", "pilots": {"other@x": {"attack": [{"ja": "あ"}]}, "@kai": {"light": [{"ja": "古い"}]}}}}
        files = call("toFiles", model, kept)
        doc = files["dialogue/battle_quotes.json"]
        why, voices = validate.battle_quotes_problem(doc)
        self.assertEqual((why, voices), ("", {"kai-1.ogg"}))
        self.assertEqual(doc["pilots"]["@kai"], {"attack": [{"ja": "行くぞ！", "zh-Hans": "上了！", "voice": "kai-1.ogg"}],
                                                  "special": [{"weapon": "@duo", "sometimes": True, "lines": [{"ja": "合わせろ！"}]},
                                                              {"opponent": 298, "situation": "evade", "lines": [{"ja": "遅い！"}]},
                                                              {"opponent": 298, "lines": [{"ja": "そこだ！"}]}]})
        self.assertIn("other@x", doc["pilots"])
        self.assertEqual(call("voicesOf", model), ["kai-1.ogg"])
        back = run(f"import * as m from {json.dumps(MODULE.as_uri())}; const f = {json.dumps({**files, **{k: v for k, v in kept.items() if k not in files}}, ensure_ascii=False)};"
                   "const r = m.fromFiles(f); console.log(JSON.stringify(m.toFiles(r.model, r.kept)));")
        self.assertEqual(back["dialogue/battle_quotes.json"], doc)
        self.assertEqual(call("problems", model), [])
        long = {**MODEL, "pilots": [{**MODEL["pilots"][0], "quotes": {"attack": [{"zh-Hans": "没有日文"}, {"ja": "あ" * 43}],
                                                                           "special": [{"lines": [{"ja": "x"}]}, {"opponent": 400, "lines": [{"ja": "x"}]}]}}]}
        self.assertEqual({p["code"] for p in call("problems", long)}, {"quote_ja", "quote_long", "quote_special", "quote_opponent"})

    def test_sprites(self):
        # The maker's paths for the sample package's pictures are the ones it has, fields kept.
        doc = json.loads((ROOT / "config/recomp/mods/final-dancouga/data/new_units.json").read_text(encoding="utf-8"))
        for name, entry in doc["sprites"].items():
            self.assertEqual(call("placeSprite", entry, name, 1, True), entry, name)
        several = call("placeSprite", {"image": "", "origin": [10, 20], "mirror": False, "ticks": [3, 4], "loop": 1}, "beam", 2, True)
        self.assertEqual(several, {"frames": ["art/battle/beam-1.png", "art/battle/beam-2.png"], "origin": [10, 20], "mirror": False,
                                   "ticks": [3, 4], "loop": 1, "hd": ["art/battle/beam-1-hd.png", "art/battle/beam-2-hd.png"]})
        files = set(several["frames"]) | set(several["hd"])
        self.assertEqual(validate.sprites_problem({"beam": several}, files), "")
        # All ticks 1 is the default and goes; without HD there is none.
        self.assertEqual(call("placeSprite", {"image": "", "origin": [0, 0], "ticks": [1]}, "a", 1, False), {"image": "art/battle/a.png", "origin": [0, 0]})
        # "around" stays (a picture in a beam's place); none is the default.
        self.assertEqual(call("placeSprite", {"image": "", "origin": [0, 0], "around": [0, 2]}, "a", 1, False)["around"], [0, 2])
        self.assertNotIn("around", call("placeSprite", {"image": "", "origin": [0, 0], "around": [0, 0]}, "a", 1, False))

        model = {**MODEL, "units": [{**MODEL["units"][0], "pose": "@stand"}, MODEL["units"][1]],
                 "sprites": {"stand": {"image": "art/battle/stand.png", "origin": [59, 90]}}, "scenes": {"cut": "@stand", "old": [1, 2, 3]}}
        self.assertEqual(call("problems", model), [])
        renamed = run(f"import * as m from {json.dumps(MODULE.as_uri())}; const model = {json.dumps(model)};"
                      "m.renameSprite(model, 'stand', 'pose'); console.log(JSON.stringify(model));")
        self.assertEqual((renamed["units"][0]["pose"], renamed["scenes"], list(renamed["sprites"])), ("@pose", {"cut": "@pose", "old": [1, 2, 3]}, ["pose"]))
        broken = {**model, "sprites": {"stand": {"frames": ["a.png", "b.png"], "origin": [0, 0.5], "ticks": [1], "loop": 2, "hd": ["a-hd.png"]}},
                  "scenes": {"cut": "@gone"}, "units": [{**MODEL["units"][0], "pose": "@missing"}, MODEL["units"][1]]}
        codes = {p["code"] for p in call("problems", broken)}
        self.assertEqual(codes, {"sprite_hd", "sprite_origin", "sprite_ticks", "sprite_loop", "pose_missing", "scene_missing"})


@unittest.skipUnless(NODE, "node is not installed")
class ThunderstoreTests(unittest.TestCase):
    """web/src/lib/thunderstore.ts writes what tools/mods/thunderstore.py writes."""

    def test_same_as_the_script(self):
        spec = importlib.util.spec_from_file_location("thunderstore", ROOT / "tools/mods/thunderstore.py")
        script = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(script)
        module = (ROOT / "web/src/lib/thunderstore.ts").as_uri()
        long = "长" * 260
        for manifest in (
            {"schema": "srw64.mod.v1", "id": "test.webunits", "version": "1.2.0", "name": {"zh-Hans": "测试", "en": "Test"},
             "description": {"zh-Hans": "一台  新机体\n和人物", "en": "A new unit\tand a character"}, "license": "CC-BY-4.0",
             "authors": ["A", "B"], "game": ">=0.5.4", "requires": {"srw64.hd": ">=1.0.0"}},
            {"schema": "srw64.mod.v1", "id": "plain", "version": "0.1.0", "name": "Plain", "description": long, "license": "MIT", "game": ">=0.5.4"},
        ):
            file = f"{manifest['id']}-{manifest['version']}.srw64mod"
            parsed, problem = validate.parse_manifest(manifest)
            self.assertIsNone(problem)
            name = script.default_name(manifest["id"])
            got = run(f"import * as ts from {json.dumps(module)}; const m = {json.dumps(manifest, ensure_ascii=False)};"
                      f"console.log(JSON.stringify({{ name: ts.defaultName(m.id), readme: ts.readme(m, {json.dumps(file)}),"
                      f" manifest: ts.manifestFor(m, ts.defaultName(m.id)), zip: ts.zipName('Team', 'n', m.version) }}));")
            self.assertEqual(got["name"], name)
            self.assertEqual(got["readme"], script.readme(parsed, file))
            self.assertEqual(got["manifest"], script.thunderstore_manifest(parsed, name, "", {}))
            self.assertEqual(got["zip"], f"Team-n-{manifest['version']}.zip")


@unittest.skipUnless(NODE, "node is not installed")
class StoryTests(unittest.TestCase):
    """web/src/lib/story-image.ts compiles joins and reinforcements as tools/mods/story.py does."""

    def test_same_as_story_py(self):
        sys.path.insert(0, str(ROOT / "tools"))
        sys.path.insert(0, str(ROOT / "tools/mods"))
        import story
        module = (ROOT / "web/src/lib/story-image.ts").as_uri()
        joins = [{"id": "kai", "scene": 78, "at": "end", "pilot": "@kai", "unit": "@z", "level": -2,
                  "info": {"title": {"ja": "カイ", "zh-Hans": "凯"}, "text": {"ja": "クリアで加入"}, "inherit": True}},
                 {"id": "early", "scene": 12, "at": "opening", "pilot": 166, "unit": 228, "level": 3,
                  "info": {"title": {"ja": "早い"}, "text": {"ja": "開幕"}, "inherit": True}},
                 {"id": "plain", "scene": 5, "at": "end", "pilot": 18, "unit": "@z", "level": 0}]
        waves = [{"id": "rival", "scene": 78, "turn": 2, "phase": "player", "units": [
                     {"x": 10, "y": 9, "pilot": "@rival", "unit": "@rz", "side": "enemy", "level": 1, "upgrade": 0},
                     {"x": 12, "y": 10, "pilot": 289, "unit": 21, "side": "third_hostile", "level": 0, "upgrade": 3}]},
                 {"id": "late", "scene": 78, "turn": 5, "phase": "enemy", "units": [
                     {"x": 0, "y": 63, "pilot": 300, "unit": 100, "side": "player", "level": 20, "upgrade": 5}]}]
        got = run(f"import * as s from {json.dumps(module)}; const j = {json.dumps(joins, ensure_ascii=False)}, w = {json.dumps(waves)};"
                  "const image = s.compile(j, w); console.log(JSON.stringify({ image, source: s.source(j, w), back: s.read(image),"
                  " problems: s.storyProblems(j, w), empty: s.compile([], []) }));")
        self.assertEqual(got["image"], story.compile_story(got["source"]))
        self.assertEqual(got["back"], {"joins": joins, "waves": waves})
        self.assertEqual(got["problems"], [])
        self.assertIsNone(got["empty"])
        bad = run(f"import * as s from {json.dumps(module)};"
                  "console.log(JSON.stringify(s.storyProblems([{id: 'Bad', scene: 300, at: 'end', pilot: 999, unit: 'x', level: 200, info: {title: {}, text: {ja: 'a'}, inherit: false}}],"
                  " [{id: 'w', scene: 1, turn: 0, phase: 'player', units: [{x: 64, y: 0, pilot: 1, unit: 1, side: 'enemy', level: 0, upgrade: 0}]}])));")
        self.assertEqual({p["code"] for p in bad}, {"story_id", "story_scene", "join_pilot", "join_unit", "join_level", "info_ja", "wave_turn", "wave_unit"})


@unittest.skipUnless(NODE, "node is not installed")
class AnimationTests(unittest.TestCase):
    """web/src/lib/weapon-animation.ts and the exported records (web/src/data/weapon-animations.json)."""

    def test_records_and_cutins(self):
        module = (ROOT / "web/src/lib/weapon-animation.ts").as_uri()
        bank = ROOT / "web/src/data/weapon-animations.json"
        got = run(f"import * as a from {json.dumps(module)}; import fs from 'node:fs';"
                  f"const bank = JSON.parse(fs.readFileSync({json.dumps(str(bank))}, 'utf8'));"
                  "const beam = a.original(bank, 228), cannon = a.original(bank, 875);"
                  "cannon.actors.push(a.cutin('@cut', 'fire', false)); cannon.actors[0].x = 10; cannon.actors[0].y = 92; cannon.actors[0].h4 = 50;"
                  "const chained = a.cutin(1000, 'after', true), along = a.cutin(1001, 'with', false, 0x56);"
                  "console.log(JSON.stringify({ beam, cannon, counts: [bank.records.length, bank.weapons.length, bank.scenes.length, bank.poses.length],"
                  " h4: [chained.h4, along.h4, a.cutinTiming(0x4044), a.cutinTiming(0x8057)], box: a.actorBox(beam.actors[2], bank.scenes[56]), pose: bank.poses[55],"
                  " problems: [a.animationProblems(cannon, new Set(['cut'])), a.animationProblems(cannon, new Set()), a.animationProblems({ copy: 3 }, new Set())],"
                  " size: a.recordSize(cannon) }));")
        self.assertEqual(got["counts"], [1115, 1329, 1053, 365])
        # The original records as battle_graphics reads them (docs/data/battle-animation.md).
        self.assertEqual((got["beam"]["camera"], got["beam"]["hit"], got["beam"]["action"], got["beam"]["defender"], got["beam"]["sounds"]), (1, 6, 113, 125, [-2, 1, 5, 8]))
        self.assertEqual([x["behavior"] for x in got["beam"]["actors"]], [359, 124, 118])
        self.assertEqual(got["box"], {"x0": -321, "y0": -44, "x1": -65, "y1": -12})
        self.assertEqual(got["pose"][:4], [-49, -82, 47, 14])          # the HD pose 768 = 96 x 8
        self.assertEqual(got["h4"], [0x4056, 0x8056, {"timing": "fire", "phase": 0x44}, {"timing": "with", "phase": 0x57}])
        # The record written (终极断空我's 断空砲 the way the editor makes it) is one the validator takes.
        doc = {"schema": "srw64.new-units.v2", "scenes": {"cut": "@x"}, "sprites": {"x": {"image": "x.png", "origin": [64, 48], "mirror": False}},
               "weapons": {"w": {"copy": 875, "animation": got["cannon"]}}, "units": {"u": {"copy": 224, "weapons": ["@w"]}}}
        self.assertEqual(validate.new_units_problem(doc, {"x.png"}), "")
        self.assertEqual(got["problems"], [[], ["anim_scene"], []])
        self.assertEqual(got["size"], 8 + 2 * 4 + 2 + 16 * 3 + 2)
        bad = run(f"import * as a from {json.dumps(module)};"
                  "console.log(JSON.stringify(a.animationProblems({ camera: -1, hit: 0, action: 0, defender: 0, sounds: [1,2,3,4,5,6,7,8,9],"
                  " actors: [{ scene: 2000, x: 0, y: 0, z: 0, h4: 70000, h5: 0, h6: 0, behavior: 2 }] }, new Set())));")
        self.assertEqual(bad, ["anim_header", "anim_sounds", "anim_scene", "anim_numbers", "anim_behavior"])
        # Cut-ins go after the cut-ins at the head, before the effects; the package's own pictures
        # only on cut-ins for now.
        place = run(f"import * as a from {json.dumps(module)};"
                    "const c = a.cutin(1000, 'fire', false), e = { scene: 20, x: 0, y: 0, z: 0, h4: 0, h5: 190, h6: 0, behavior: 111 };"
                    "const own = { camera: 1, hit: 0, action: 83, defender: 125, sounds: [], actors: [{ ...e, scene: '@x' }] };"
                    "console.log(JSON.stringify([a.cutinPlace([]), a.cutinPlace([e]), a.cutinPlace([c, c, e]), a.animationProblems(own, new Set(['x']), true),"
                    " a.animationProblems({ ...own, actors: [a.cutin('@x', 'fire', false)] }, new Set(['x']), true), a.CUTIN_TICKS]));")
        self.assertEqual(place, [0, 0, 2, ["anim_own_effect"], [], 30])

    def test_generic_weapons(self):
        # The maker offers only these for now: each an original weapon whose animation has no
        # routine that computes with scene numbers or shows the lines after the speaker's.
        module = (ROOT / "web/src/lib/weapon-animation.ts").as_uri()
        generic = run(f"import * as a from {json.dumps(module)}; console.log(JSON.stringify(a.GENERIC_WEAPONS));")
        bank = json.loads((ROOT / "web/src/data/weapon-animations.json").read_text())
        self.assertEqual(sorted(map(int, bank["lines"])), [34, 139, 160, 194, 248, 303, 307, 323, 342, 343])
        risky = set(bank["complex"]) | set(map(int, bank["lines"]))
        self.assertGreaterEqual(len(generic), 10)
        for weapon in generic:
            behaviours = {actor[7] for actor in bank["records"][bank["weapons"][weapon]][5]}
            self.assertFalse(behaviours & risky, weapon)

    def test_scenes_around(self):
        # The beam (118) moves its actor to the scene resources +1 and +2: a package picture
        # there needs copies around it (the sprite's "around"), which the validator takes.
        module = (ROOT / "web/src/lib/weapon-animation.ts").as_uri()
        bank = ROOT / "web/src/data/weapon-animations.json"
        got = run(f"import * as a from {json.dumps(module)}; import fs from 'node:fs';"
                  f"const bank = JSON.parse(fs.readFileSync({json.dumps(str(bank))}, 'utf8'));"
                  "const beam = a.original(bank, 228); beam.actors[2].scene = '@b';"
                  "const has = (r) => () => r;"
                  "const fifty = a.original(bank, 57); fifty.actors[7].scene = '@b';"
                  "console.log(JSON.stringify([bank.steps['118'], bank.steps['125'] ?? null, bank.steps['351'], a.sceneSpan(bank.steps, 118), a.sceneSpan(bank.steps, 351),"
                  " a.animationProblems(beam, new Set(['b']), false, bank, has([0, 0])), a.animationProblems(beam, new Set(['b']), false, bank, has([0, 2])),"
                  " a.animationProblems(beam, new Set(['b']), false, bank, has(undefined)), bank.complex.includes(205), a.lockedPictures(a.original(bank, 228), bank.complex),"
                  " a.lockedPictures(fifty, bank.complex), a.animationProblems(fifty, new Set(['b']), false, bank, has([4, 9]))]));")
        self.assertEqual(got[:5], [[1, 2], None, [-1, 2, 4], {"low": 0, "high": 2}, {"low": -1, "high": 4}])
        self.assertEqual(got[5:8], [["anim_scene_set"], [], []])
        # Weapon 57's routines compute with scene numbers (205, 211): it keeps the original's pictures.
        self.assertEqual(got[8:], [True, False, True, ["anim_locked"]])
        doc = {"schema": "srw64.new-units.v2", "sprites": {"b": {"image": "b.png", "origin": [256, 16], "around": [0, 2]}}, "scenes": {"b": "@b"}}
        self.assertEqual(validate.new_units_problem(doc, {"b.png"}), "")
        doc["sprites"]["b"]["around"] = [0, 33]
        self.assertIn("around", validate.new_units_problem(doc, {"b.png"}))
        # The host's table is the exported one; an actor of the beam on the picture needs it.
        exported = json.loads(bank.read_text())
        self.assertEqual({str(b): list(r) for b, r in validate._STEPS.items()},
                         {k: [-min(0, *v), max(0, *v)] for k, v in exported["steps"].items()})
        self.assertEqual(sorted(validate._COMPLEX), exported["complex"])
        beam = json.loads(json.dumps(doc))
        beam["sprites"]["b"]["around"] = [0, 1]
        beam["weapons"] = {"w": {"copy": 228, "animation": {"camera": 1, "hit": 6, "action": 113, "defender": 125, "sounds": [],
                                                           "actors": [{"scene": "@b", "behavior": 118}]}}}
        self.assertIn("around\": [0, 2]", validate.new_units_problem(beam, {"b.png"}))
        beam["sprites"]["b"]["around"] = [0, 2]
        self.assertEqual(validate.new_units_problem(beam, {"b.png"}), "")
        # In 57's record (205 computes with scene numbers) a picture is refused, a cut-in's taken.
        beam["weapons"]["w"]["animation"]["actors"] = [{"scene": 479, "behavior": 205}, {"scene": "@b", "behavior": 125}]
        self.assertIn("原版的图", validate.new_units_problem(beam, {"b.png"}))
        beam["weapons"]["w"]["animation"]["actors"][1]["behavior"] = 240
        self.assertEqual(validate.new_units_problem(beam, {"b.png"}), "")


@unittest.skipUnless(NODE, "node is not installed")
class PixelsTests(unittest.TestCase):
    def test_foot(self):
        # The default origin of the sample's stand (59, 90 by hand) comes out within a few pixels.
        from PIL import Image
        image = Image.open(ROOT / "config/recomp/mods/final-dancouga/art/battle/stand.png").convert("RGBA")
        pixels = (ROOT / "web/src/lib/pixels.ts").as_uri()
        data = base64.b64encode(image.tobytes()).decode()
        x, y = run(f"import {{ foot }} from {json.dumps(pixels)}; const a = new Uint8Array(Buffer.from({json.dumps(data)}, 'base64'));"
                   f"console.log(JSON.stringify(foot(a, {image.width}, {image.height})));")
        self.assertLessEqual(abs(x - 59), 4)
        self.assertLessEqual(abs(y - 90), 1)
        self.assertEqual(run(f"import {{ foot }} from {json.dumps(pixels)}; console.log(JSON.stringify(foot(new Uint8Array(16), 2, 2)));"), [1, 1])

    def test_reduce(self):
        # A 96×96 gradient with a transparent band: at most 63 colours, RGBA5551 channels, alpha cut at half.
        pixels = (ROOT / "web/src/lib/pixels.ts").as_uri()
        script = (f"import {{ reduce, five }} from {json.dumps(pixels)};"
                  "const w = 96, a = new Uint8ClampedArray(w * w * 4);"
                  "for (let y = 0; y < w; y++) for (let x = 0; x < w; x++) { const i = (y * w + x) * 4;"
                  "  a[i] = x * 2.6; a[i + 1] = y * 2.6; a[i + 2] = (x + y) * 1.3; a[i + 3] = y < 10 ? 100 : 255; }"
                  "const n = reduce(a, 63); const colours = new Set(); let bad = 0, clear = 0;"
                  "for (let i = 0; i < a.length; i += 4) { if (!a[i + 3]) { clear += a[i] + a[i + 1] + a[i + 2] === 0 ? 1 : 0; continue; }"
                  "  colours.add(a[i] * 65536 + a[i + 1] * 256 + a[i + 2]); for (const c of [a[i], a[i + 1], a[i + 2]]) if (five(c) !== c) bad++; }"
                  "console.log(JSON.stringify({ n, colours: colours.size, bad, clear }));")
        result = run(script)
        self.assertLessEqual(result["colours"], 63)
        self.assertEqual(result["n"], result["colours"])
        self.assertGreater(result["colours"], 30)
        self.assertEqual(result["bad"], 0)
        self.assertEqual(result["clear"], 96 * 10)


if __name__ == "__main__":
    unittest.main()
