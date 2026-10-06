"""The Linux package's add-to-Steam helper reads Steam's binary shortcuts.vdf."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("add_to_steam", ROOT / "tools/release/linux/add_to_steam.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def string(key, value):
    return b"\x01" + key.encode() + b"\0" + value.encode() + b"\0"


def shortcut(index, app_id, name, exe):
    return (b"\x00" + str(index).encode() + b"\0" + b"\x02appid\0" + struct.pack("<I", app_id)
            + string("AppName", name) + string("Exe", exe) + string("icon", "")
            + b"\x02IsHidden\0" + struct.pack("<I", 0) + b"\x07LastPlayTime\0" + struct.pack("<Q", 5)
            + b"\x00tags\0\x08" + b"\x08")


class AddToSteamTests(unittest.TestCase):
    def test_finds_the_shortcut_that_starts_the_launcher(self):
        launcher = Path("/home/deck/Games/SRW64/marchwind64.sh")
        data = (b"\x00shortcuts\0" + shortcut(0, 2186587176, "Other", '"/usr/bin/other"')
                + shortcut(1, 3870819196, "超级机器人大战64", f'"{launcher}"') + b"\x08\x08")
        with tempfile.TemporaryDirectory() as folder:
            vdf = Path(folder) / "shortcuts.vdf"
            vdf.write_bytes(data)
            self.assertEqual(MODULE.shortcut_ids(vdf, launcher), [3870819196])
            self.assertEqual(MODULE.shortcut_ids(vdf, Path("/elsewhere/marchwind64.sh")), [])
        parsed, end = MODULE.parse_vdf(data)
        self.assertEqual(end, len(data))
        self.assertEqual(parsed["shortcuts"]["1"]["AppName"], "超级机器人大战64")
        self.assertEqual(parsed["shortcuts"]["0"]["LastPlayTime"], 5)

    def test_a_shortcut_to_the_old_launcher_counts(self):
        # Made before the rename: it starts srw64.sh in the same folder.
        launcher = Path("/home/deck/Games/SRW64/marchwind64.sh")
        old = launcher.with_name("srw64.sh")
        data = (b"\x00shortcuts\0" + shortcut(0, 3870819196, "超级机器人大战64", f'"{old}"') + b"\x08\x08")
        with tempfile.TemporaryDirectory() as folder:
            vdf = Path(folder) / "shortcuts.vdf"
            vdf.write_bytes(data)
            self.assertEqual(MODULE.shortcut_ids(vdf, launcher), [])
            self.assertEqual(MODULE.shortcut_ids(vdf, launcher, old), [3870819196])
            self.assertEqual(MODULE.shortcut_ids(vdf, launcher, Path("/elsewhere/srw64.sh")), [])
        self.assertEqual(MODULE.OLD_LAUNCHER, MODULE.LAUNCHER.with_name("srw64.sh"))
        self.assertEqual(MODULE.LAUNCHER.name, "marchwind64.sh")

    def test_unreadable_shortcuts_mean_none(self):
        with tempfile.TemporaryDirectory() as folder:
            vdf = Path(folder) / "shortcuts.vdf"
            self.assertEqual(MODULE.shortcut_ids(vdf, Path("/x")), [])
            vdf.write_bytes(b"\x00shortcuts\0\x05bad")
            self.assertEqual(MODULE.shortcut_ids(vdf, Path("/x")), [])

    def test_every_game_language_has_a_steam_name(self):
        self.assertEqual(set(MODULE.NAMES), {"zh-Hans", "en", "ja"})
        self.assertIn(MODULE.FIRST_LOCALE, MODULE.NAMES)


if __name__ == "__main__":
    unittest.main()
