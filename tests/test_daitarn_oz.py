"""BUG06: pinned-ROM evidence and the runtime repair's wiring/scene boundary."""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("daitarn_audit", ROOT / "tools/recomp/gameplay/check_daitarn_oz.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class DaitarnWiringTests(unittest.TestCase):
    def test_original_handler_is_wrapped_and_always_runs(self):
        source = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        self.assertIn('"resident_func_800A158C": "srw64_original_party_pair_flag"', source)
        wrapper = re.search(r"void resident_func_800A158C\(.*?\n\}", hooks, re.S).group()
        self.assertIn("daitarn_join_event", wrapper)
        self.assertIn("srw64_original_party_pair_flag(ram,ctx)", wrapper)
        repair = hooks[hooks.index("static void repair_daitarn"):hooks.index('extern "C" {')]
        self.assertIn("srw64::campaign::active()", repair)
        self.assertIn("srw64::mini_stage::active()", repair)
        self.assertIn("c.r4=165;c.r5=216;c.r6=0;c.r7=500", repair)
        self.assertIn("resident_func_800AAD28(ram,&c)", repair)
        self.assertIn("c.r4=165;c.r5=216;c.r6=1", repair)
        self.assertIn("resident_func_800AC6C4(ram,&c)", repair)
        self.assertNotIn("rules::", repair)
        step = re.search(r"void load_0008F4B0_func_801CE19C\(.*?\n\}", hooks, re.S).group()
        self.assertIn("daitarn_old_save(rdram)", step)


@unittest.skipUnless((ROOT / "rom.z64").exists(), "local original ROM required")
class DaitarnRomTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom, cls.data = audit.load(ROOT / "rom.z64")

    def test_original_defect_and_normal_route_counterexamples(self):
        report = audit.audit(self.rom, self.data)
        self.assertEqual(report["fault"]["command_rom"], "0x1c5b24")
        self.assertEqual(set(report["routes"]), {"ark", "serein"})

    def test_old_save_allowlist_matches_guarded_original_scene_graph(self):
        graph = {}
        for row in self.data["stage_events"]:
            event = row["script"]
            for ins in event["instructions"]:
                if ins["opcode"] != 0x3D4B or ins["operands"][0] == 500:
                    continue
                for scene in event["scenes"]:
                    target = ins["operands"][0]
                    if scene in (123, 124) and target == 42:
                        continue  # the non-OZ return, gated by var10 != 1
                    graph.setdefault(scene, set()).add(target)
        header = (ROOT / "src/host/base_fixes.hpp").read_text()
        function = re.search(r"inline bool daitarn_after_join\(.*?\n\}", header, re.S).group()
        allowlist = {int(n) for n in re.findall(r"case (\d+):", function)}
        self.assertEqual(allowlist, audit.reach(56, graph))

    def test_original_earth_split_excludes_banjo_until_reunion(self):
        # 3D73 1 selects D_800D06A8; 3D73 0 selects D_800D06E4.
        def members(address):
            result = []
            offset = address - 0x80075610
            while (n := int.from_bytes(self.rom[offset:offset+2], "big")) != 0xFFFF:
                result.append(n)
                offset += 2
            return result
        self.assertIn(165, members(0x800D06A8))
        self.assertNotIn(165, members(0x800D06E4))
        opening = next(r["script"] for r in self.data["stage_events"]
                       if 104 in r["script"]["scenes"] and r["script"]["header_words"][0] == 12)
        self.assertEqual(opening["instructions"][1]["opcode"], 0x3D74)


if __name__ == "__main__":
    unittest.main()
