"""The debug interface must reach every input the game takes (docs/debug-interface.md).

Static checks against the host sources, so a new key binding, hotkey or host
method cannot be added without a way to drive it through the interface.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "src/host"
sys.path.insert(0, str(ROOT / "tools"))
from recomp.debug.mcp_server import TOOLS  # noqa: E402
from recomp.run.native_inputs import BUTTONS  # noqa: E402


def debug_keys() -> list[str]:
    text = (HOST / "debug_protocol.hpp").read_text()
    names = re.search(r"key_names=\{(.*?)\};", text, re.S).group(1)
    return re.findall(r'"([a-z0-9]+)"', names)


class DebugCoverageTests(unittest.TestCase):
    def setUp(self):
        self.graphics = (HOST / "graphics.cpp").read_text()

    def test_every_bound_key_has_a_virtual_key_of_the_same_name(self):
        binds = re.findall(r"bind\(SDL_SCANCODE_(\w+), (\w+), ", self.graphics)
        self.assertEqual(len(binds), 18)  # 14 N64 buttons' keys and WASD for the stick
        for scancode, key in binds:
            self.assertEqual(scancode.lower(), key.lower(), scancode)
        bound = {scancode.lower() for scancode, _ in binds}
        self.assertTrue(bound <= set(debug_keys()))
        # Every scancode the host reads goes through a binding with a virtual key.
        used = set(re.findall(r"SDL_SCANCODE_(\w+)", self.graphics))
        self.assertEqual({name.lower() for name in used}, bound)

    def test_every_hotkey_has_a_virtual_press(self):
        handled = set(re.findall(r"event\.key\.keysym\.sym ?== ?SDLK_(\w+)", self.graphics))
        self.assertEqual(handled, {"F6", "F8", "ESCAPE"})
        virtual = set(re.findall(r"key ?== ?srw64::debug::(\w+)", self.graphics))
        self.assertTrue({"F6", "F8", "Escape"} <= virtual)
        # F7 shares the SDL path, including composition and repeat suppression.
        shared = (ROOT / "src/native/ui/frontend.cpp").read_text()
        self.assertIn("SDLK_F7", shared)
        self.assertIn("!input.has_composition()", shared)
        self.assertIn("!event.key.repeat", shared)
        self.assertIn("srw64::ui::event(e)", self.graphics)
        self.assertIn("srw64::debug::key_names[key]", self.graphics)
        self.assertIn("f7", debug_keys())

    def test_controller_buttons_match_the_input_compiler(self):
        server = (HOST / "debug_server.cpp").read_text()
        table = re.search(r"names\[\]=\{(.*?)\};", server, re.S).group(1)
        host = dict((name, int(value, 0)) for name, value in re.findall(r'\{"(\w+)",(0x[0-9a-fA-F]+|\d+)\}', table))
        self.assertEqual(host, BUTTONS)

    def test_mcp_describes_the_same_keys(self):
        keys_tool = next(tool for tool in TOOLS if tool["name"] == "srw64_keys")
        listed = keys_tool["description"].split("Keys: ")[1].rstrip(".").split()
        self.assertEqual(listed, debug_keys())

    def test_every_host_method_has_an_mcp_tool(self):
        server = (HOST / "debug_server.cpp").read_text()
        methods = set(re.findall(r'method=="([\w.]+)"', server)) - {"methods", "wait_vi"}  # wait is client-side
        mcp = (ROOT / "tools/recomp/debug/mcp_server.py").read_text()
        called = set(re.findall(r'client\.call\("([\w.]+)"', mcp))
        called |= {"quit", "keys"}  # through Session.quit and run_keys
        self.assertIn("run_keys(client", mcp)
        self.assertEqual(methods - called, set())


if __name__ == "__main__":
    unittest.main()
