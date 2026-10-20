"""Windows-only integration: execute the actual CMD and change a SUBST drive letter.

CTest supplies a ROM-free C++ probe; plain unittest discovery skips without it.
"""
from __future__ import annotations

import ctypes
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


@unittest.skipUnless(os.name == "nt" and PROBE is not None, "Windows and CTest's native probe required")
class WindowsLauncherTests(unittest.TestCase):
    def test_cmd_forwards_options_and_exit_status(self) -> None:
        with tempfile.TemporaryDirectory(prefix="marchwind-cmd-") as temporary:
            root = Path(temporary)
            game = root / "game (spaces) & portable"
            game.mkdir()
            shutil.copyfile(PROBE, game / "Marchwind64.exe")
            script = game / "Marchwind64.cmd"
            shutil.copyfile(ROOT / "tools/release/windows/Marchwind64.cmd", script)
            capture = root / "arguments.txt"
            data, rom = root / "custom data", root / "chosen ROM.z64"
            environment = {**os.environ, "MARCHWIND_TEST_ARGS": str(capture)}
            command = (f'"{os.environ["COMSPEC"]}" /d /s /c ""{script}" '
                       f'--user-dir "{data}" --language en --rom "{rom}" --mute"')
            result = subprocess.run(command, cwd=root, env=environment, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 37, result.stderr)
            self.assertEqual(capture.read_text(encoding="utf-8").splitlines(),
                             ["--play", "--user-dir", str(data), "--language", "en", "--rom", str(rom), "--mute"])
            # The shim does no ROM or AppData preflight, including for help/save commands.
            for argument in ("--help", "--export-save card.srm", "--import-save card.srm"):
                result = subprocess.run(f'"{os.environ["COMSPEC"]}" /d /s /c ""{script}" {argument}"',
                                        cwd=root, env=environment, capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 37, result.stderr)
                self.assertEqual(capture.read_text(encoding="utf-8").splitlines(), ["--play", *argument.split()])

    def test_drive_letter_change_retains_rom_settings_and_progress(self) -> None:
        used = ctypes.windll.kernel32.GetLogicalDrives()
        free = [f"{chr(code)}:" for code in range(ord("D"), ord("Z") + 1)
                if not used & (1 << (code - ord("A")))]
        if len(free) < 2:
            self.skipTest("two unused drive letters needed for SUBST relocation test")
        first, second = free[-2:]
        with tempfile.TemporaryDirectory(prefix="marchwind-drive-") as temporary:
            active: str | None = None
            try:
                subprocess.run(["subst", first, temporary], check=True, capture_output=True, timeout=10)
                active = first
                subprocess.run([str(PROBE), "--seed", first + "\\bundle"], check=True, timeout=30)
                subprocess.run(["subst", first, "/d"], check=True, capture_output=True, timeout=10)
                active = None
                subprocess.run(["subst", second, temporary], check=True, capture_output=True, timeout=10)
                active = second
                subprocess.run([str(PROBE), "--resume", second + "\\bundle"], check=True, timeout=30)
            finally:
                if active is not None:
                    subprocess.run(["subst", active, "/d"], check=True, capture_output=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
