"""The native インターミッション menu: hooks, labels and the ROM data it relies on."""
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom.z64"


class IntermissionSourceTests(unittest.TestCase):
    def test_build_and_step_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, hook in (("801CDFB0", "srw64_original_intermission_menu_build", "intermission_build"),
                                        ("801CE19C", "srw64_original_intermission_menu_step", "intermission_step")):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"srw64_game_hooks.{hook}", body)

    def test_labels_and_menu_text_exist(self):
        from srw64_native.profile import UI_KEYS
        keys = {key for key in UI_KEYS if key.startswith("intermission_")} | {"funds_edit_hint"}
        self.assertEqual(len(keys), 5)
        page = (ROOT / "src/native/ui/frontend.cpp").read_text()
        for key in keys:
            self.assertIn(f'"{key}"', page)
        # Title, nine items, 総ターン数/資金, クリア, パイロット, 妖精.
        texts = {f"base:t00_{n:05d}" for n in [*range(0xFCC, 0xFD7), 0xFE0, 0xFEC, 0x1002]}
        for locale in ("ja", "zh-Hans", "en"):
            document = json.loads((ROOT / f"content/locales/{locale}.json").read_text())
            self.assertTrue(all(document["ui"].get(key) for key in keys), locale)
            if locale != "ja":
                self.assertLessEqual(texts, {row["key"] for row in document["entries"]}, locale)
        self.assertIn("{n}", json.loads((ROOT / "content/locales/ja.json").read_text())["ui"]["intermission_episode"])


@unittest.skipUnless(ROM.exists(), "needs the original ROM")
class IntermissionRomTests(unittest.TestCase):
    """The page lays its panels out from these tables instead of drawing them."""

    def setUp(self):
        self.rom = ROM.read_bytes()

    def resident(self, address, size):
        offset = address - 0x80076610 + 0x1000
        return self.rom[offset:offset + size]

    def rectangles(self, layout):
        entry = self.resident(0x800C8BB8 + layout * 24, 24)
        words = struct.unpack(">12I", self.resident(struct.unpack(">I", entry[8:12])[0], 48))
        return [((low >> 12 & 0xFFF) // 4, (low & 0xFFF) // 4, (high >> 12 & 0xFFF) // 4, (high & 0xFFF) // 4)
                for high, low in zip(words[::2], words[1::2]) if high >> 24 == 0xF6]

    def test_panel_rectangles(self):
        title, info, footer = (120, 25, 199, 39), (145, 49, 287, 79), (34, 193, 287, 207)
        self.assertEqual(self.rectangles(0x69), [title, (33, 41, 103, 183), info, footer])
        self.assertEqual(self.rectangles(0x8D), [title, (33, 41, 103, 71), info, footer])
        self.assertEqual(self.rectangles(0x86), [(109, 121, 155, 160)])

    def test_restricted_scenes_and_backgrounds(self):
        offset = 0x801DC6D4 - 0x801C4500 + 0x8F4B0
        self.assertEqual(list(self.rom[offset:offset + 14]),
                         [0x26, 0x30, 0x44, 0x49, 0x4F, 0x51, 0x53, 0x56, 0x58, 0x5D, 0x5F, 0x60, 0x68, 0x84])
        rows = [struct.unpack(">3Hh", self.resident(0x800C59AC + n * 8, 8)) for n in range(9)]
        self.assertEqual(rows[:8], [(0x155E + n, 0x1566 + n, 0x156E + n, 1) for n in range(8)])
        self.assertEqual(rows[8], rows[0])


if __name__ == "__main__":
    unittest.main()


class UpgradePageSourceTests(unittest.TestCase):
    def test_list_and_stat_hooks_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, hook in (("801CF388", "srw64_original_upgrade_list_open", "upgrade_list_build"),
                                        ("801CF564", "srw64_original_upgrade_list_step", "upgrade_list_step"),
                                        ("801D03D0", "srw64_original_weapon_list_open", "upgrade_list_build"),
                                        ("801D04A4", "srw64_original_weapon_list_step", "upgrade_list_step"),
                                        ("801CF680", "srw64_original_upgrade_open", "upgrade_stats_build"),
                                        ("801C80E0", "srw64_original_upgrade_stats_view", "upgrade_stats_view"),
                                        ("801CF988", "srw64_original_upgrade_stats_step", "upgrade_stats_step"),
                                        ("801D0600", "srw64_original_weapon_screen_open", "weapon_list_build"),
                                        ("801D087C", "srw64_original_weapon_screen_step", "weapon_list_step"),
                                        ("801D0C7C", "srw64_original_upgrade_weapon_view", "weapon_confirm_build"),
                                        ("801D1100", "srw64_original_upgrade_weapon_step", "weapon_confirm_step")):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"srw64_game_hooks.{hook}", body)
        # The cap scopes of the rule stay around the stat build and step.
        for address in ("801CF680", "801CF988"):
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            self.assertIn("upgrades::Scope scope(rdram, upgrades::Policy::decide);", body[:body.index("\n}\n")])

    def test_parts_hooks_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, screen in (("801D4A00", "srw64_original_parts_list_open", "parts_build(rdram, ctx, 7)"),
                                          ("801D4A98", "srw64_original_parts_list_step", "parts_step(rdram, ctx, 7, srw64_original_parts_list_step)"),
                                          ("801D4BEC", "srw64_original_parts_slots_open", "parts_build(rdram, ctx, 18)"),
                                          ("801D4C94", "srw64_original_parts_slots_step", "parts_step(rdram, ctx, 18, srw64_original_parts_slots_step)"),
                                          ("801D5168", "srw64_original_parts_holders_open", "parts_build(rdram, ctx, 19)"),
                                          ("801D51EC", "srw64_original_parts_holders_step", "parts_step(rdram, ctx, 19, srw64_original_parts_holders_step)")):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"srw64_game_hooks.{screen}", body)
        parts = (ROOT / "src/host/parts_page.cpp").read_text()
        self.assertIn("if(original_screens())return false;", parts)
        # The screen table names these six routines for screens 7, 18 and 19.
        if not ROM.exists():
            self.skipTest("needs the original ROM")
        rom = ROM.read_bytes()
        table = 0x801DC9D0 - 0x801C4500 + 0x8F4B0
        entries = [struct.unpack(">II", rom[table + n * 8:table + n * 8 + 8]) for n in range(23)]
        self.assertEqual(entries[7], (0x801D4A00, 0x801D4A98))
        self.assertEqual(entries[18], (0x801D4BEC, 0x801D4C94))
        self.assertEqual(entries[19], (0x801D5168, 0x801D51EC))

    def test_ability_hooks_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, screen in (("801D14BC", "srw64_original_ability_unit_list_open", 4), ("801D1554", "srw64_original_ability_unit_list_step", 4),
                                          ("801D16D8", "srw64_original_ability_unit_open", 13), ("801D2030", "srw64_original_ability_unit_step", 13),
                                          ("801D2144", "srw64_original_ability_weapons_open", 14), ("801D21F8", "srw64_original_ability_weapons_step", 14),
                                          ("801D22E0", "srw64_original_ability_pilot_list_open", 5), ("801D2378", "srw64_original_ability_pilot_list_step", 5),
                                          ("801D2480", "srw64_original_ability_pilot_open", 15), ("801D24C8", "srw64_original_ability_pilot_step", 15)):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"ability_{'build' if original.endswith('_open') else 'step'}(rdram, ctx, {screen}", body)
        self.assertIn("if(original_screens())return false;", (ROOT / "src/host/ability_page.cpp").read_text())
        if ROM.exists():
            rom = ROM.read_bytes()
            table = 0x801DC9D0 - 0x801C4500 + 0x8F4B0
            entries = [struct.unpack(">II", rom[table + n * 8:table + n * 8 + 8]) for n in range(23)]
            self.assertEqual([entries[n] for n in (4, 13, 14, 5, 15)], [(0x801D14BC, 0x801D1554), (0x801D16D8, 0x801D2030), (0x801D2144, 0x801D21F8), (0x801D22E0, 0x801D2378), (0x801D2480, 0x801D24C8)])

    def test_swap_hooks_are_renamed_and_replaced(self):
        generator = (ROOT / "tools/recomp/toolchain/generate_cpu.py").read_text()
        hooks = (ROOT / "src/host/game_hooks.cpp").read_text()
        for address, original, screen in (("801D25A4", "srw64_original_swap_pilots_open", 6), ("801D263C", "srw64_original_swap_pilots_step", 6),
                                          ("801D2758", "srw64_original_swap_targets_open", 16), ("801D2A24", "srw64_original_swap_targets_step", 16),
                                          ("801D2B64", "srw64_original_swap_confirm_open", 17), ("801D3A90", "srw64_original_swap_confirm_step", 17),
                                          ("801D4164", "srw64_original_swap_fairies_open", 20), ("801D41FC", "srw64_original_swap_fairies_step", 20),
                                          ("801D42FC", "srw64_original_swap_fairy_targets_open", 21), ("801D4578", "srw64_original_swap_fairy_targets_step", 21)):
            self.assertIn(f'"load_0008F4B0_func_{address}": "{original}"', generator)
            body = hooks[hooks.index(f"void load_0008F4B0_func_{address}("):]
            body = body[:body.index("\n}\n")]
            self.assertIn(f"{original}(rdram, ctx)", body)
            self.assertIn(f"swap_{'build' if original.endswith('_open') else 'step'}(rdram, ctx, {screen}", body)
        self.assertIn("if(original_screens())return false;", (ROOT / "src/host/swap_page.cpp").read_text())
        if ROM.exists():
            rom = ROM.read_bytes()
            table = 0x801DC9D0 - 0x801C4500 + 0x8F4B0
            entries = [struct.unpack(">II", rom[table + n * 8:table + n * 8 + 8]) for n in range(23)]
            self.assertEqual([entries[n] for n in (6, 16, 17, 20, 21)], [(0x801D25A4, 0x801D263C), (0x801D2758, 0x801D2A24), (0x801D2B64, 0x801D3A90), (0x801D4164, 0x801D41FC), (0x801D42FC, 0x801D4578)])

    def test_screens_follow_the_intermission_ui_setting(self):
        # Every build entry point hands the screen back to the original when the
        # setting says so, closing any page still open; the setting is persisted.
        upgrade = (ROOT / "src/host/upgrade_page.cpp").read_text()
        self.assertEqual(upgrade.count("if(original_screens())return false;"), 4)
        self.assertIn("if(settings::native_intermission_ui())return false;", upgrade)
        menu = (ROOT / "src/host/intermission_page.cpp").read_text()
        self.assertIn('if(!settings::native_intermission_ui()){std::lock_guard lock(mutex);hide("original");return false;}', menu)
        settings = (ROOT / "src/native/ui/presentation_settings.cpp").read_text()
        self.assertIn('{"intermission_ui",native_intermission?"native":"original"}', settings)
        self.assertIn('saved.value("intermission_ui","native")', settings)
        self.assertIn('saved.value("intermission_ui","native")', (ROOT / "src/native/app/launch.cpp").read_text())
        page = (ROOT / "src/native/ui/frontend.cpp").read_text()
        self.assertIn('"intermission-ui:"', page)
        self.assertIn('params.contains("intermission_ui")', (ROOT / "src/host/debug_server.cpp").read_text())
        self.assertIn('"intermission_ui": {"type": "string", "enum": ["native", "original"]}', (ROOT / "tools/recomp/debug/mcp_server.py").read_text())

    def test_labels_exist_in_every_language(self):
        from srw64_native.profile import UI_KEYS
        keys = {key for key in UI_KEYS if key.startswith("upgrade_")}
        self.assertEqual(len(keys), 8)
        page = (ROOT / "src/native/ui/frontend.cpp").read_text()
        for key in keys:
            self.assertIn(f'"{key}"', page)
        for locale in ("ja", "zh-Hans", "en"):
            ui = json.loads((ROOT / f"content/locales/{locale}.json").read_text())["ui"]
            self.assertTrue(all(ui.get(key) for key in keys), locale)
