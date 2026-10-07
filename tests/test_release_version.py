"""The release version is written in three places; they must agree (docs/guide/release.md)."""
from __future__ import annotations

import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ReleaseVersionTest(unittest.TestCase):
    def test_cmake_pyproject_and_android_agree(self):
        cmake = re.search(r"^project\(.* VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(), re.M).group(1)
        python = tomllib.loads((ROOT / "pyproject.toml").read_text())["project"]["version"]
        manifest = (ROOT / "tools/release/android/app/AndroidManifest.xml").read_text()
        name = re.search(r'android:versionName="([^"]+)"', manifest).group(1)
        code = int(re.search(r'android:versionCode="(\d+)"', manifest).group(1))
        self.assertEqual(python, cmake)
        self.assertEqual(name, cmake)
        major, minor, patch = (int(part) for part in cmake.split("."))
        self.assertEqual(code, major * 10000 + minor * 100 + patch)


if __name__ == "__main__":
    unittest.main()
