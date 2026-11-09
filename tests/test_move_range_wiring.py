"""BUG10: move above 15 goes to the host's grid; up to 15 the original runs (docs/gameplay/base-fixes.md)."""
from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOOKED = {
    "801C3E50": ("srw64_original_move_fill", "move_fill"),
    "801E0B94": ("srw64_original_move_query", "move_query"),
    "801E0C68": ("srw64_original_move_path", "move_path"),
    "801C5810": ("srw64_original_move_step", "move_step"),
    "80202848": ("srw64_original_ai_near_unit", "ai_near_unit"),
    "80202A84": ("srw64_original_ai_near_target", "ai_near_target"),
    "801F3268": ("srw64_original_unit_on_range", "unit_on_range"),
    "801C4560": ("srw64_original_range_paint", "range_paint"),
}


class MoveRangeWiringTests(unittest.TestCase):
    def setUp(self):
        self.generate = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        self.hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        self.glue = (ROOT / "src/host/move_range.cpp").read_text()

    def wrapper(self, name: str) -> str:
        return re.search(rf"void load_000AB160_func_{name}\(.*?\n\}}", self.hooks, re.S).group(0)

    def function(self, name: str) -> str:
        return re.search(rf"^bool {name}\(.*?^\}}", self.glue, re.S | re.M).group(0)

    def test_each_entry_falls_back_to_the_original(self):
        for address, (original, hook) in HOOKED.items():
            self.assertIn(f'"load_000AB160_func_{address}": "{original}"', self.generate)
            body = self.wrapper(address)
            self.assertIn(f"srw64_game_hooks.{hook}(rdram, ctx)", body)
            self.assertIn(f"{original}(rdram, ctx)", body)

    def test_every_clear_of_the_tables_ends_the_wide_grid(self):
        # A weapon's range starts with 801C3D34; a stale grid would answer for it.
        self.assertIn('"load_000AB160_func_801C3D34": "srw64_original_move_clear"', self.generate)
        body = self.wrapper("801C3D34")
        self.assertLess(body.index("move_cleared"), body.index("srw64_original_move_clear"))
        self.assertIn("mode = Mode::None", self.glue[self.glue.index("void cleared()"):][:60])

    def test_up_to_fifteen_is_left_to_the_original(self):
        fill = self.function("fill")
        self.assertRegex(fill, r"int move = int32_t\(ctx->r5\);\s*if \(move <= kWindow\) return false;")
        # After the EN limit too: the original applies the same limit itself.
        self.assertEqual(fill.count("if (move <= kWindow) return false;"), 2)
        for name in ("step", "near_unit", "near_target"):
            self.assertIn("if (mode != Mode::Move) return false;", self.function(name), name)
        for name in ("query", "unit_on_range"):
            self.assertRegex(self.function(name), r"if \(mode == Mode::None \|\|", name)

    def test_the_original_search_never_gets_more_than_its_layers(self):
        path = self.function("path")
        self.assertRegex(path, r"if \(mode != Mode::Move\) \{\s*if \(int32_t\(ctx->r4\) > kWindow\) ctx->r4 = kWindow;\s*return false;")

    def test_the_draw_restores_what_it_borrows(self):
        body = self.wrapper("801E4760")
        begin, end, drawn = (body.index(s) for s in ("move_draw_begin", "move_draw_end", "move_range_drawn"))
        self.assertLess(begin, end)
        self.assertLess(end, drawn)
        restore = self.glue[self.glue.index("void draw_end"):][:300]
        for table in ("saved_reach", "kAnchorX", "kAnchorY"):
            self.assertIn(table, restore)

    def test_a_weapon_range_up_to_fifteen_is_left_to_the_original(self):
        paint = self.function("paint")
        self.assertRegex(paint, r"if \(mode != Mode::Attack\) \{\s*if \(radius <= kWindow\) return false;")
        # A map weapon's flow is not taken over: it must never reach the tables past 15.
        self.assertRegex(paint, r"if \(!ordinary\) \{\s*mode = Mode::None;\s*if \(radius > kWindow\) ctx->r6 = kWindow;\s*return false;")

    def test_only_the_weapon_table_draw_slides_the_window(self):
        body = self.wrapper("801E49A4")
        self.assertIn("(uint32_t(ctx->r7) & 0xFF) == 0 && srw64_game_hooks.move_draw_begin", body)
        self.assertLess(body.index("srw64_original_range_draw"), body.index("move_draw_end"))

    def test_the_answer_to_the_confirm_query_stays_with_this_screen(self):
        query = self.function("query")
        self.assertIn("kStop", query)
        self.assertIn("kState", query)

    def test_data_mod_move_limit_is_untouched(self):
        source = (ROOT / "src/host/mod_data.hpp").read_text()
        self.assertNotRegex(source, r"\bmin\([^)]*15\)")
        self.assertIn('{"move",0x06,1,false,1,30}', source)


if __name__ == "__main__":
    unittest.main()
