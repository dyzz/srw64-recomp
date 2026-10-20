"""The Android launcher's ROM check (RomCheck.java) without Android (requires a JDK)."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/release/android/app/java/org/srw64/game/RomCheck.java"


class AndroidRomIdentityTests(unittest.TestCase):
    def test_constants_match_the_baseline(self):
        rom = json.loads((ROOT / "config/srw64-jp-rev0.json").read_text(encoding="utf-8"))["rom"]
        text = SOURCE.read_text(encoding="utf-8")
        self.assertEqual(re.search(r'SHA256 = "([0-9a-f]{64})"', text).group(1), rom["sha256"])
        self.assertEqual(int(re.search(r"SIZE = (\d+)L", text).group(1)), rom["size"])

    def test_refusals_read_as_on_computers(self):
        """explain() is rom_refused_message's Chinese, a newline, then its English, per problem."""
        native = (ROOT / "src/native/app/rom_order.hpp").read_text(encoding="utf-8")
        body = native[native.index("inline std::string rom_refused_message"):]
        english = body[body.index('if (locale == "en") {'):body.index('return std::string("Unable to start')]
        chinese = body[body.index('return std::string("Unable to start'):body.index('return std::string("无法启动')]
        literal = re.compile(r'"((?:[^"\\]|\\.)*)"')
        en, zh = literal.findall(english)[1:], literal.findall(chinese)[2:]   # past "en" / the English return's two
        self.assertEqual(len(en), 4)
        self.assertEqual(len(zh), 4)
        text = SOURCE.read_text(encoding="utf-8")
        for index, case in enumerate(("MISSING", "NOT_N64", "WRONG_SIZE", "default")):
            label = "default:" if case == "default" else f"case {case}:"
            block = text[text.index(label, text.index("static String explain")):]
            parts = literal.findall(block[:block.index(";\n")])
            self.assertEqual(parts, [zh[index] + "\\n", en[index]], case)

@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK unavailable")
class AndroidRomCheckTests(unittest.TestCase):
    def test_orders_and_refusals(self):
        rom = next((p for p in (ROOT / "assets/rom.z64", ROOT / "rom.z64") if p.is_file()), None)
        with tempfile.TemporaryDirectory(prefix="srw64-android-rom-") as directory:
            root = Path(directory)
            classes = root / "classes"
            classes.mkdir()
            subprocess.run(["javac", "-d", str(classes), str(SOURCE), str(ROOT / "tests/android/RomCheckTest.java")],
                           check=True, capture_output=True)
            arguments = [str(root)] + ([str(rom)] if rom else [])
            result = subprocess.run(["java", "-Xmx512m", "-cp", str(classes), "org.srw64.game.RomCheckTest", *arguments],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
