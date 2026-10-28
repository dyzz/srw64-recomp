"""tools/release/recomplauncher.json, RecompLauncher's description of the program, ships at the
top of every desktop package next to the executable (docs/guide/release.md). Its program paths,
setting keys and values must be what the packages and the game actually use."""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / "tools/release/recomplauncher.json"


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class RecompLauncherFileTest(unittest.TestCase):
    def setUp(self):
        self.data = json.loads(FILE.read_text(encoding="utf-8"))

    def test_format(self):
        self.assertEqual(self.data["format"], 1)
        self.assertNotIn("game", self.data, "an N64 ROM has no game.kind yet; the program asks for it itself")
        self.assertEqual(self.data["start"]["args"], [], "the desktop start takes no arguments")

    def test_program_paths_match_the_packages(self):
        programs = self.data["start"]["program"]
        linux = source("tools/release/build_linux.py")
        folder = re.search(r"^FOLDER = '([^']+)'", linux, re.M).group(1)
        self.assertIn(f"arcname={'FOLDER'}", linux.replace(" ", ""))
        self.assertIn("marchwind64.sh", programs)
        self.assertIn(f"{folder}/marchwind64.sh", programs)
        workflow = source(".github/workflows/build.yml")
        out = re.search(r'\$out = "([^"]+)"', workflow).group(1)
        self.assertIn('Copy-Item build/recomp/gfx-build/srw64-gfx-host.exe "$out/Marchwind64.exe"', workflow)
        self.assertIn("Marchwind64.exe", programs)
        self.assertIn(f"{out}/Marchwind64.exe", programs)
        executable = re.search(r'^EXECUTABLE = "([^"]+)"', source("tools/release/package_macos.py"), re.M).group(1)
        self.assertIn(f"Marchwind64.app/Contents/MacOS/{executable}", programs)

    def test_every_package_ships_the_file(self):
        self.assertIn("recomplauncher.json", source("tools/release/build_linux.py"))
        self.assertIn("Copy-Item tools/release/recomplauncher.json $out", source(".github/workflows/build.yml"))
        release = source("tools/release/build_release.py")
        self.assertIn('"tools/release/recomplauncher.json", app_dir / "recomplauncher.json"', release)
        self.assertIn('zip_folder(steps, "zip-app", output, "app", app_zip, contents=True)', release)

    def test_settings_file_and_folders(self):
        runtime = source("src/native/app/runtime.cpp")
        (config,) = self.data["config"]
        self.assertEqual((config["id"], config["format"]), ("main", "json"))
        paths = [entry["path"] for entry in config["file"]]
        for folder in ("SRW64Recomp", "srw64-recomp", "Library/Application Support/SRW64Recomp"):
            self.assertIn(f'"{folder}"', runtime)
            self.assertTrue(any(folder in path for path in paths), folder)
        self.assertTrue(all(path.endswith("/presentation.json") for path in paths))
        self.assertNotIn("if", config["file"][-1], "the last entry is the fallback")
        self.assertIn("user-data", paths[0], "a user-data folder beside the program is the portable mode (desktop.cpp)")
        folders = [p.rsplit("/", 1)[0] for p in paths]
        saves = [entry["path"] for entry in self.data["folders"]["saves"]]
        self.assertEqual([p.rsplit("/", 1)[0] for p in saves], folders)
        self.assertTrue(all(path.endswith("/saves") for path in saves))
        # The HD pack, filters, bezels and mods go into the user folder itself (launch.cpp looks
        # for hd/ there first); the launcher only installs one file per system, so the HD pack
        # stays a download the player unzips there or opens with the game.
        self.assertEqual([entry["path"] for entry in self.data["folders"]["data"]], folders)
        self.assertEqual([a["open"] for a in self.data["actions"]], ["data", "saves"])
        self.assertIn("hd/", self.data["actions"][0]["help"])
        self.assertNotIn("new", config, "a file with schema but no locale is refused by launch.cpp")

    def test_setting_values_are_the_games(self):
        settings = source("src/native/ui/presentation_settings.cpp")
        catalogs = {"zh-Hans", "zh-Hant", "ja", "en"}
        by_id = {setting["id"]: setting for setting in self.data["settings"]}
        self.assertEqual(set(by_id), {"language", "ui_size", "refresh_rate", "map_unit_icons"})
        locales = {choice["set"]["locale"] for choice in by_id["language"]["choices"]}
        self.assertEqual(locales, catalogs)
        for key, expected in (("ui_size", {"standard", "large", "largest"}), ("refresh_rate", {"original", "60", "display"}),
                              ("map_unit_icons", {"hd", "original"})):
            values = {choice["set"][key] for choice in by_id[key]["choices"]}
            self.assertEqual(values, expected, key)
            self.assertIn(f'"{key}"', settings)
            for value in expected:
                self.assertIn(f'"{value}"', settings, f"{key}={value}")
            self.assertIn(by_id[key]["defaults"][key], expected)
        for setting in self.data["settings"]:
            for choice in setting["choices"]:
                self.assertEqual(set(choice["set"]), {setting["id"] if setting["id"] != "language" else "locale"})

    def test_exit_codes_are_the_desktop_starts(self):
        desktop = source("src/native/app/desktop.cpp")
        for code in self.data["exit_codes"]:
            self.assertRegex(desktop, rf"return {int(code)};")
        self.assertEqual(self.data["looks"], {"graphics": "d3d12", "lines": 240})


if __name__ == "__main__":
    unittest.main()
