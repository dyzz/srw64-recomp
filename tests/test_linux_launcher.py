"""Run the Linux launcher and native dialog adapter without a ROM or display server."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE: Path | None = None
if "--probe" in sys.argv:
    index = sys.argv.index("--probe")
    PROBE = Path(sys.argv[index + 1]).resolve()
    del sys.argv[index:index + 2]


@unittest.skipUnless(sys.platform.startswith("linux") and PROBE is not None, "Linux CTest native probe required")
class LinuxLauncherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="marchwind-linux-ui-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.helpers = self.root / "helpers"
        self.helpers.mkdir()
        self.capture = self.root / "calls.jsonl"
        self.environment = {**os.environ, "PATH": str(self.helpers), "MARCHWIND_UI_CAPTURE": str(self.capture)}

    def helper(self, name: str, code: int = 0, output: str = "") -> None:
        script = self.helpers / name
        script.write_text(f'''#!{sys.executable}
import json, os, sys
with open(os.environ["MARCHWIND_UI_CAPTURE"], "a", encoding="utf-8") as f:
    f.write(json.dumps(sys.argv, ensure_ascii=False) + "\\n")
sys.stdout.write({output!r})
sys.exit({code})
''', encoding="utf-8")
        script.chmod(0o755)

    def calls(self) -> list[list[str]]:
        if not self.capture.exists():
            return []
        return [json.loads(line) for line in self.capture.read_text(encoding="utf-8").splitlines()]

    def run_probe(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([str(PROBE), *arguments], env=self.environment, cwd=self.root,
                              capture_output=True, text=True, timeout=10)

    def test_picker_and_literal_argument_vectors(self) -> None:
        selected = self.root / "日本語 ROM & name.z64"
        self.helper("kdialog", output=str(selected) + "\n")
        result = self.run_probe("--ui-choose")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(selected))
        sentinel = self.root / "must-not-exist"
        message = f"progress $(touch {sentinel}) & ' <name>\nnext line"
        self.assertEqual(self.run_probe("--ui-confirm", message).returncode, 0)
        self.assertIn(message, self.calls()[-1])
        self.assertFalse(sentinel.exists())
        self.assertIn("--warningcontinuecancel", self.calls()[-1])
        self.helper("kdialog", code=1)
        self.assertEqual(self.run_probe("--ui-confirm", message).returncode, 1)
        self.assertEqual(self.run_probe("--ui-choose").stdout.strip(), "CANCEL")

    def test_zenity_fallback_and_no_helpers_never_accept(self) -> None:
        self.helper("zenity", output=str(self.root / "rom.z64") + "\n")
        self.assertEqual(self.run_probe("--ui-choose").returncode, 0)
        self.assertIn("--file-selection", self.calls()[-1])
        self.assertEqual(self.run_probe("--ui-confirm", "replace?").returncode, 0)
        self.assertIn("--default-cancel", self.calls()[-1])
        self.helper("kdialog", code=127)  # An unavailable backend may fall back; cancellation may not.
        self.assertEqual(self.run_probe("--ui-confirm", "replace?").returncode, 0)
        self.assertEqual(Path(self.calls()[-1][0]).name, "zenity")
        self.helper("kdialog", code=1)
        count = len(self.calls())
        self.assertEqual(self.run_probe("--ui-confirm", "replace?").returncode, 1)
        self.assertEqual(len(self.calls()), count + 1)
        self.helper("kdialog", code=2)  # KDialog also uses 2 for Cancel / window close.
        count = len(self.calls())
        self.assertEqual(self.run_probe("--ui-confirm", "replace?").returncode, 1)
        self.assertEqual(len(self.calls()), count + 1)
        self.assertEqual(self.run_probe("--ui-choose").stdout.strip(), "CANCEL")
        (self.helpers / "kdialog").unlink()
        (self.helpers / "zenity").unlink()
        result = self.run_probe("--ui-confirm", "replace?")
        self.assertEqual(result.returncode, 1)
        self.assertIn("No save replacement was authorized", result.stderr)
        self.assertEqual(self.run_probe("--ui-choose").stdout.strip(), "CANCEL")

    def test_invalid_picker_output_and_error_fallback(self) -> None:
        for output in ("relative.z64\n", "/one.z64\n/two.z64\n", "", "/bad\0path"):
            with self.subTest(output=repr(output)):
                self.helper("kdialog", output=output)
                self.assertEqual(self.run_probe("--ui-choose").returncode, 2)
        (self.helpers / "kdialog").unlink()
        self.helper("zenity")
        result = self.run_probe("--ui-error", "cannot synchronize <path>")
        self.assertEqual(result.returncode, 0)
        self.assertIn("cannot synchronize", result.stderr)
        self.assertIn("--error", self.calls()[-1])

    def test_shell_forwarding_exit_code_and_steam_keyboard(self) -> None:
        game = self.root / "game & spaces"
        game.mkdir()
        shutil.copyfile(PROBE, game / "srw64")
        (game / "srw64").chmod(0o755)
        script = game / "marchwind64.sh"
        shutil.copyfile(ROOT / "tools/release/linux/marchwind64.sh", script)
        script.chmod(0o755)
        capture = self.root / "arguments.txt"
        environment = {**os.environ, "MARCHWIND_TEST_ARGS": str(capture), "SteamDeck": "1"}
        environment.pop("SDL_ENABLE_STEAM_SCREEN_KEYBOARD", None)
        options = ["--user-dir", str(self.root / "custom data"), "--language", "ja", "--rom", "ROM $(literal).z64", "--mute"]
        result = subprocess.run([str(script), *options], cwd=self.root, env=environment, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 37, result.stderr)
        self.assertEqual(capture.read_text().splitlines(), ["--play", *options, "KEYBOARD=1"])
        environment["SDL_ENABLE_STEAM_SCREEN_KEYBOARD"] = "0"
        result = subprocess.run([str(script), "--help"], cwd=self.root, env=environment, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 37)
        self.assertEqual(capture.read_text().splitlines(), ["--play", "--help", "KEYBOARD=0"])
        environment["SRW64_ROM"] = str(self.root / "ROM & env.z64")
        result = subprocess.run([str(PROBE), "--named"], cwd=self.root, env=environment, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.stdout.strip(), environment["SRW64_ROM"])


if __name__ == "__main__":
    unittest.main()
