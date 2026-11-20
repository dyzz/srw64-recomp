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
