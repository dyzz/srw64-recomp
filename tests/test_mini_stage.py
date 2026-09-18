import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.script_lab.mini_stage import AUX_BLOCK, EVENT_BLOCK, SCHEMA, compile_stage, encode_record, report  # noqa: E402

RECORDS = ROOT / "assets/original-data/records/stage_auxiliary.jsonl"


class MiniStageCompilerTests(unittest.TestCase):
    def test_events_are_word_aligned_and_pointed_into_the_scene_buffer(self):
        image = compile_stage({"schema": SCHEMA, "name": "t", "map": 20, "events": [
            {"type": 12, "commands": [{"op": "3D3B", "args": [1]}, {"op": "3D48"}]},
            {"type": 7, "header": [1, 0, 4, 0], "commands": [{"op": "3D4A"}]}],
            "deployments": [{"group": 0, "x": 3, "y": 4, "actor": 300, "unit": 266, "faction": 1, "level_offset": 2}]})
        self.assertEqual([(e["type"], e["offset"], e["size"], e["pointer"]) for e in image["events"]],
                         [(12, 0, 18, EVENT_BLOCK), (7, 20, 14, EVENT_BLOCK + 20)])
        self.assertEqual(image["events_hex"][:20], "000C0000000000000000")
        self.assertEqual(image["events_hex"][20:], "3D3B00013D48FFFF0000" "00070001000000040000" "3D4AFFFF0000")
        self.assertEqual(image["aux_hex"], "0000000300040" "12C" "0002010A0000000000000000000100000000" "0000" "03E70000")
        self.assertEqual(image["deployments"][0]["actor"], 300)
        self.assertEqual((image["map"], image["slot"], image["aux_block"]), (20, None, AUX_BLOCK))

    def test_limits_and_unknown_fields_are_rejected(self):
        with self.assertRaises(ValueError):
            compile_stage({"schema": SCHEMA, "events": [{"type": 12, "commands": []}] * 64})
        with self.assertRaises(ValueError):
            compile_stage({"schema": SCHEMA, "events": [{"type": 12, "commands": [{"op": "3D38", "args": [1]}] * 1700}]})
        with self.assertRaises(ValueError):
            encode_record({"group": 0, "hp": 1})
        with self.assertRaises(ValueError):
            encode_record({"x": 70000})
        with self.assertRaises(ValueError):
            compile_stage({"schema": "other", "events": []})

    @unittest.skipUnless(RECORDS.exists(), "catalog not extracted")
    def test_smoke_stage_copies_scene_one_deployments(self):
        image = compile_stage(json.loads((ROOT / "config/recomp/mini-stages/smoke.json").read_text()))
        self.assertEqual(len(image["deployments"]), 20)
        self.assertEqual(image["deployments"][12], {"group": 1, "x": 22, "y": 5, "actor": 28, "byte8": 0, "level_offset": 0, "unit": 36,
                                                    "upgrade": 0, "raw14": 0, "raw16": 0, "raw18": 65535, "faction": 0, "behavior": 0, "extra": 0, "raw26": 0})
        self.assertTrue(image["aux_hex"].endswith("03E70000"))
        self.assertEqual([e["type"] for e in image["events"]], [12, 13, 0, 7, 7, 2, 14])


class MiniStageReportTests(unittest.TestCase):
    def test_polls_are_joined_per_event_and_unfired_events_are_reported(self):
        import tempfile
        root = Path(tempfile.mkdtemp())
        run = root / "flow-t"; run.mkdir()
        image = compile_stage({"schema": SCHEMA, "name": "t", "events": [
            {"type": 12, "commands": [{"op": "3D3B", "args": [1]}, {"op": "3D48"}]},
            {"type": 7, "header": [1, 0, 4, 0], "commands": [{"op": "3D4A"}]}]})
        opening = image["events"][0]["pointer"]
        (run / "mini-stage-events.jsonl").write_text(json.dumps({"action": "applied", "scene": 1, "vi": 100}) + "\n")
        def poll(vi, before_pc, before_state, after_pc, after_state):
            return "SRW64_SCRIPT_TRACE " + json.dumps({"schema": "srw64.script-poll.v2", "vi": vi,
                "before": {"pc": before_pc, "state": before_state, "words": []}, "after": {"pc": after_pc, "state": after_state, "words": []}}) + "\n"
        # 3D3B runs for two polls (PC parked past its opcode), 3D48 completes in one, then FFFF ends the event.
        log = poll(100, opening + 10, 0, opening + 12, 2) + poll(102, opening + 12, 2, opening + 14, 0) + poll(104, opening + 14, 0, opening + 16, 0) + poll(106, opening + 16, 0, 0, 0x80)
        (root / "flow-t.native.log").write_text(log)
        result = report(run, image)
        first, second = result["events"]
        self.assertTrue(first["fired"]); self.assertEqual((first["first_vi"], first["last_vi"], first["polls"]), (100, 106, 4))
        self.assertEqual([(s["opcode"], s["start_vi"], s["end_vi"]) for s in first["executed"]], [("3D3B", 100, 102), ("3D48", 104, 104)])
        self.assertEqual(first["commands_not_executed"], [])
        self.assertFalse(second["fired"]); self.assertEqual(second["commands_not_executed"], ["3D4A@10"])
        self.assertEqual(result["host_events"][0]["scene"], 1)
        self.assertTrue((run / "mini-stage-report.json").exists())


@unittest.skipUnless(RECORDS.exists(), "catalog not extracted")
class CopiedEventTests(unittest.TestCase):
    def test_copied_event_keeps_the_original_bytes_and_stops_at_its_terminator(self):
        """Replaying an original event is only evidence if the bytes are unchanged."""
        from recomp.script_lab.mini_stage import copied_event
        from recomp.script_lab.script_debug import layout
        key = "base:stage_events:0019c1b0"  # scene 1 opening
        words, listing = copied_event(key, layout())
        raw = bytes.fromhex(json.loads(next(
            l for l in (RECORDS.parent / "stage_events.jsonl").read_text().splitlines()
            if f'"{key}"' in l))["raw_hex"])
        original = [int.from_bytes(raw[i:i + 2], "big") for i in range(0, len(raw) - 1, 2)]
        self.assertEqual(words, original[:len(words)], "copied words must match the record byte for byte")
        self.assertEqual(words[0], 12, "scene 1's opening is a type 12 event")
        self.assertEqual(listing[-1]["opcode"], "FFFF", "the copy stops at the event terminator")
        # Trailing bytes after the terminator are padding and must not be copied.
        self.assertLessEqual(len(words), len(original))
        image = compile_stage({"schema": SCHEMA, "name": "copy", "map": 20,
                               "events": [{"copy_from": key}],
                               "deployments": [{"group": 0, "x": 1, "y": 1, "actor": 28, "unit": 36}]})
        self.assertEqual(image["events"][0]["type"], 12)
        self.assertEqual(bytes.fromhex(image["events_hex"])[:2], b"\x00\x0c")


@unittest.skipUnless(RECORDS.exists(), "catalog not extracted")
class ShippedStagesTests(unittest.TestCase):
    def test_every_shipped_stage_compiles_within_the_scene_buffers(self):
        """The probe stages are evidence fixtures: a stage that stops compiling would
        silently invalidate the runtime findings recorded against it."""
        stages = sorted((ROOT / "config/recomp/mini-stages").glob("*.json"))
        self.assertTrue(stages)
        for path in stages:
            with self.subTest(stage=path.name):
                image = compile_stage(json.loads(path.read_text()))
                self.assertEqual(image["schema"], "srw64.mini-stage-image.v1")
                self.assertLessEqual(len(image["events_hex"]) // 2, 0x1A00)
                self.assertLessEqual(len(image["aux_hex"]) // 2, 0x2000)
                self.assertTrue(image["events"])
                # Every probe stage must reach its own commands without the player:
                # the opening event is the only one a bounded run always runs.
                opening = [e for e in image["events"] if e["type"] == 12]
                self.assertEqual(len(opening), 1, "a stage needs exactly one opening event")


if __name__ == "__main__":
    unittest.main()
