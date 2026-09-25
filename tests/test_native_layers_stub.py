"""The Linux host links native_layers_stub.cpp in place of the Metal HD layers.

Every function those headers declare needs a stub, or only the Linux build notices
(docs/guide/linux-build.md).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ("native_marker", "native_map", "native_portrait", "native_background", "native_sprite")
DECLARATION = re.compile(r"^[A-Za-z_][\w:<>, ]*[\s*&]+(\w+)\([^;{]*\);", re.MULTILINE)


class NativeLayerStubTests(unittest.TestCase):
    def test_stub_defines_every_declared_function(self):
        stub = (ROOT / "src/host/native_layers_stub.cpp").read_text(encoding="utf-8")
        missing = []
        for header in HEADERS:
            source = (ROOT / "src/host" / f"{header}.hpp").read_text(encoding="utf-8")
            names = {name for name in DECLARATION.findall(source) if name != "operator"}
            self.assertTrue(names, header)
            missing += [f"{header}.hpp: {name}" for name in sorted(names)
                        if not re.search(rf"\b{name}\([^;]*\)\s*\{{", stub)]
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
