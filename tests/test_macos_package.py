"""ROM-free package policy tests; Mach-O/Apple-tool smoke runs separately on macOS."""
import importlib.util
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package_macos", ROOT / "tools/release/package_macos.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class MacOSPackageTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.binary = self.root / "game"
        self.binary.write_bytes(bytes.fromhex("cffaedfe") + b"synthetic fixture only")
        self.output = self.root / "SRW64 Recompiled.app"
        self.commands = []

    def run_tool(self, command):
        self.commands.append(command)
        if "otool" in command[0]:
            return "Load command 0\n cmd LC_BUILD_VERSION\n platform 1\n minos 14.0\n sdk 15.0\n"
        return ""

    def stage(self, **kwargs):
        with patch.object(PACKAGE.sys, "platform", "darwin"), patch.object(PACKAGE, "run", self.run_tool):
            return PACKAGE.stage_bundle(self.binary, self.output, **kwargs)

    def test_versions_and_load_commands(self):
        self.assertEqual(PACKAGE.version_tuple("14.2"), (14, 2, 0))
        for bad in ("14.beta", "", "14.0.0.1", "-1", "14;bad"):
            with self.assertRaises(ValueError):
                PACKAGE.version_tuple(bad)
        paths, versions = PACKAGE.load_commands(
            " cmd LC_RPATH\n path /opt/homebrew/lib (offset 12)\n"
            " cmd LC_VERSION_MIN_MACOSX\n version 13.0\n"
            " cmd LC_BUILD_VERSION\n minos 14.0\n")
        self.assertEqual(paths, ["/opt/homebrew/lib"])
        self.assertEqual(versions, ["13.0", "14.0"])

    def test_explicit_allowlist_and_plist(self):
        (self.root / "rom.z64").write_bytes(b"must not copy")
        (self.root / "save.bin").write_bytes(b"must not copy")
        (self.root / "font.ttf").write_bytes(b"must not copy")
        license_file = self.root / "LICENSE"
        license_file.write_text("test notice")
        result = self.stage(notices=(license_file,))
        info = plistlib.loads((result / "Contents/Info.plist").read_bytes())
        self.assertEqual(info["CFBundleExecutable"], PACKAGE.EXECUTABLE)
        self.assertTrue(info["NSHighResolutionCapable"])
        files = {p.relative_to(result).as_posix() for p in result.rglob("*") if p.is_file()}
        self.assertEqual(files, {"Contents/MacOS/srw64-gfx-host", "Contents/Info.plist",
                                 "Contents/Resources/Distribution.txt", "Contents/Resources/licenses/00-LICENSE"})
        signing = [c for c in self.commands if "codesign" in c[0]]
        self.assertEqual(signing[-2][-1].endswith(".app"), True)
        self.assertIn("--verify", signing[-1])
        self.assertFalse(any("--deep" in c for c in signing if "--sign" in c))

    def test_dialogue_text_is_copied_as_text_only(self):
        dialogue = self.root / "dialogue"
        (dialogue / "zh-Hans/story").mkdir(parents=True)
        (dialogue / "zh-Hans/story/scene-0001.txt").write_text("@17412\n译文\n", encoding="utf-8")
        (dialogue / "zh-Hans/notes.json").write_text("{}")
        result = self.stage(dialogue=dialogue)
        files = {p.relative_to(result).as_posix() for p in result.rglob("*") if p.is_file()}
        self.assertIn("Contents/Resources/dialogue/zh-Hans/story/scene-0001.txt", files)
        self.assertFalse(any(f.endswith("notes.json") for f in files))

    def test_fonts_ship_with_their_licences(self):
        fonts = self.root / "fonts"
        fonts.mkdir()
        for name in ("HarmonyOS_Sans_SC.ttf", "LICENSE-HarmonyOS-Sans.txt", "notes.json"):
            (fonts / name).write_bytes(b"x")
        result = self.stage(fonts=fonts)
        files = {p.relative_to(result).as_posix() for p in result.rglob("*") if p.is_file()}
        self.assertIn("Contents/Resources/fonts/HarmonyOS_Sans_SC.ttf", files)
        self.assertIn("Contents/Resources/fonts/LICENSE-HarmonyOS-Sans.txt", files)
        self.assertNotIn("Contents/Resources/fonts/notes.json", files)

    def test_explicit_runtime_library_and_dependency_search(self):
        libraries = self.root / "linked libs"
        libraries.mkdir()
        actual = libraries / "libSDL3.0.16.dylib"
        actual.write_bytes(self.binary.read_bytes())
        alias = libraries / "libSDL3.dylib"
        alias.symlink_to(actual.name)
        unrelated = libraries / "not-selected.dylib"
        unrelated.write_bytes(self.binary.read_bytes())
        result = self.stage(search_dirs=(libraries,), runtime_libraries=(alias,))
        self.assertEqual((result / "Contents/MacOS/libSDL3.dylib").read_bytes(), actual.read_bytes())
        self.assertFalse((result / "Contents/MacOS/libSDL3.dylib").is_symlink())
        self.assertFalse((result / "Contents/MacOS/not-selected.dylib").exists())
        self.assertTrue(any(str(libraries) in arg and arg.startswith("-DSEARCH_DIRS:")
                            for command in self.commands for arg in command))
        self.assertTrue(any(arg.startswith("-DEXTRA_LIBS:") and arg.endswith("libSDL3.dylib")
                            for command in self.commands for arg in command))

    def test_runtime_library_rejects_non_macho_and_duplicate_names(self):
        invalid = self.root / "bad.dylib"
        invalid.write_text("not a library")
        with self.assertRaisesRegex(ValueError, "Mach-O dylib"):
            self.stage(runtime_libraries=(invalid,))
        invalid.write_bytes(self.binary.read_bytes())
        with self.assertRaisesRegex(ValueError, "unique dylib names"):
            self.stage(runtime_libraries=(invalid, invalid))
        self.assertFalse(self.output.exists())

    def test_existing_destination_is_never_overwritten(self):
        self.output.mkdir()
        (self.output / "keep").write_text("keep")
        with self.assertRaises(FileExistsError):
            self.stage()
        self.assertEqual((self.output / "keep").read_text(), "keep")
        self.assertFalse(self.commands)

    def test_failed_closure_is_not_published(self):
        def fail(command):
            raise subprocess.CalledProcessError(1, command, output="missing dependency")
        with patch.object(PACKAGE.sys, "platform", "darwin"), patch.object(PACKAGE, "run", fail):
            with self.assertRaises(subprocess.CalledProcessError):
                PACKAGE.stage_bundle(self.binary, self.output)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".srw64-stage-*")))
        self.assertTrue(self.binary.exists())

    def test_deployment_mismatch_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "requires macOS"):
            self.stage(minimum="13.0")
        self.assertFalse(self.output.exists())

    def test_scripts_and_fonts_rejected(self):
        font = self.root / "font.ttf"
        font.write_bytes(b"not a license")
        with self.assertRaises(ValueError):
            self.stage(notices=(font,))
        self.binary.write_text("#!/bin/sh\npython launcher.py\n")
        with self.assertRaisesRegex(ValueError, "Mach-O"):
            self.stage()

    def test_external_rpath_removed_after_fixup(self):
        with patch.object(PACKAGE, "run") as run:
            run.return_value = "cmd LC_RPATH\npath /opt/homebrew/lib (offset 12)\ncmd LC_RPATH\npath @loader_path/../Frameworks (offset 12)\n"
            PACKAGE.strip_external_rpaths([self.binary])
            self.assertEqual(run.call_args_list[-1].args[0], ["/usr/bin/install_name_tool", "-delete_rpath", "/opt/homebrew/lib", str(self.binary)])

    def test_external_symlink_rejected(self):
        self.output.mkdir()
        try:
            (self.output / "external").symlink_to(self.binary)
        except OSError:
            self.skipTest("symlink creation unavailable")
        with self.assertRaisesRegex(ValueError, "symlink escapes"):
            PACKAGE.macho_files(self.output)

    def test_non_macos_rejected(self):
        with patch.object(PACKAGE.sys, "platform", "linux"):
            with self.assertRaisesRegex(ValueError, "must run on macOS"):
                PACKAGE.stage_bundle(self.binary, self.output)


if __name__ == "__main__":
    unittest.main()
