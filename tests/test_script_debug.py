import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.script_lab.script_debug import SCHEMA, assemble, report  # noqa: E402


class DebugScriptAssemblerTests(unittest.TestCase):
    def test_operand_layouts_come_from_the_layout_lock(self):
        result = assemble({"schema": SCHEMA, "name": "fade", "commands": [
            {"op": "3D3B", "args": [0]}, {"op": "3D38", "args": [60]}, {"op": "3DD0"}, {"op": "3E13", "args": [5, 1]}, {"op": "3E1D"}]})
        self.assertEqual(result["hex"], "000C0000000000000000" "3D3B0000" "3D38003C" "3DD0" "3E1300050001" "3E1D" "FFFF")
        self.assertEqual([r["offset"] for r in result["listing"]], [10, 14, 18, 20, 26, 28])
        self.assertEqual(result["listing"][2]["family"], "marker")

    def test_rejects_unknown_null_or_mis_sized_commands(self):
        for commands in ([{"op": "3D30"}], [{"op": "3D76"}], [{"op": "3D78"}], [{"op": "3D3B"}], [{"op": "3D38", "args": [70000]}]):
            with self.assertRaises(ValueError):
                assemble({"schema": SCHEMA, "commands": commands})
        with self.assertRaises(ValueError):
            assemble({"schema": "other", "commands": []})
        with self.assertRaises(ValueError):
            assemble({"schema": SCHEMA, "event_type": 15, "commands": []})

    def test_header_words_are_kept_verbatim(self):
        result = assemble({"schema": SCHEMA, "event_type": 7, "header": [1, 8, 4, 0], "commands": []})
        self.assertEqual(result["words"], [7, 1, 8, 4, 0, 0xFFFF])
        self.assertEqual(json.loads(json.dumps(result))["listing"][-1]["opcode"], "FFFF")


class ReportJoinTests(unittest.TestCase):
    def test_polls_are_joined_to_executed_commands_only(self):
        import tempfile
        root = Path(tempfile.mkdtemp())
        run = root / "inject-t"; run.mkdir()
        entry = 0x807F0000
        words = "000C00000000000000003D5B00053E030007000200093D5B00013E1D3DD03D380001FFFF"
        events = [{"schema": "srw64.script-inject-event.v1", "action": "applied", "sequence": 1, "vi": 10, "entry": entry,
                   "end": entry + len(words) // 2, "words_hex": words, "before": {"pc": entry + 10}},
                  {"schema": "srw64.script-inject-event.v1", "action": "complete", "sequence": 1, "vi": 16, "elapsed_vis": 6, "after": {"pc": 0}}]
        (run / "script-inject-events.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
        def poll(vi, before_pc, before_state, after_pc, after_state):
            row = {"schema": "srw64.script-poll.v2", "vi": vi, "before": {"pc": before_pc, "state": before_state, "words": []},
                   "after": {"pc": after_pc, "state": after_state, "words": []}}
            return "SRW64_SCRIPT_TRACE " + json.dumps(row) + "\n"
        # 3D5B completes inside its poll; 3E03 is false so its 3D5B is skipped; 3D38 runs for two polls.
        log = poll(10, entry + 10, 0, entry + 14, 0) + poll(12, entry + 14, 0, entry + 32, 2) + poll(14, entry + 32, 2, entry + 34, 0) + poll(16, entry + 34, 0, 0, 0x80)
        (root / "inject-t.native.log").write_text(log)
        injection = report(run)["injections"][0]
        self.assertEqual([(s["opcode"], s["start_vi"], s["end_vi"]) for s in injection["executed"]], [("3D5B", 10, 10), ("3D38", 12, 14)])
        self.assertEqual(injection["commands_not_executed"], ["3D5B@22"])
        self.assertTrue(injection["all_boundaries_observed"])
        self.assertTrue(all(s["operand_words_observed"] for s in injection["executed"]))


if __name__ == "__main__":
    unittest.main()
