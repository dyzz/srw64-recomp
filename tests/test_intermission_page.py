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
