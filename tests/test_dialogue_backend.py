"""Keep the CPU text contract independent of graphics and platform globals."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DialogueBackendBoundaryTests(unittest.TestCase):
    def test_portable_text_and_pixels_have_no_platform_headers(self):
        for name in ("src/host/native_dialogue_text.cpp", "src/native/text/unicode.hpp",
                     "src/native/presentation/raster_image.hpp", "src/host/dialogue_raster.hpp",
                     "src/host/dialogue_scene.cpp", "src/native/text/game_fonts.cpp"):
            source = (ROOT / name).read_text(encoding="utf-8")
            self.assertNotRegex(source, r'#include\s*[<"](?:CoreText|CoreGraphics|Metal|plume|SDL|Cocoa)')
            self.assertNotIn("MTL::", source)

    def test_cpu_backend_has_no_gpu_state_or_file_output(self):
        source = (ROOT / "src/host/dialogue_scene.cpp").read_text(encoding="utf-8")
        for name in ("MTL::", "plume::", "plume_metal.h", "std::ofstream", "presented_frame("):
            self.assertNotIn(name, source)
        self.assertIn("localization::Scope locale(frame.catalog)", source)

    def test_compositor_does_not_typeset(self):
        source = (ROOT / "src/host/macos/dialogue_plume.cpp").read_text(encoding="utf-8")
        for name in ("CoreText/", "CoreGraphics/", "CTTypesetter", "CGContext", "CFString"):
            self.assertNotIn(name, source)
        self.assertIn("rasterize_frame(*frame,framebuffer->getWidth(),framebuffer->getHeight())", source)
        self.assertIn("compositor->upload(*list,raster.image)", source)

    def test_dialogue_tests_are_outside_renderer_link_group(self):
        source = (ROOT / "src/host/CMakeLists.txt").read_text(encoding="utf-8")
        groups = re.findall(r"foreach\(SRW64_HOST_TARGET[^)]*\)", source)
        self.assertTrue(groups)
        for group in groups:
            self.assertNotIn("srw64-dialogue-test", group)
        self.assertIn("target_link_libraries(srw64-dialogue-test PRIVATE srw64_dialogue_cpu)", source)
        cpu = (ROOT / "cmake/DialogueCpu.cmake").read_text(encoding="utf-8")
        self.assertNotIn("dialogue_metal.cpp", cpu)
        self.assertNotRegex(cpu, r"target_link_libraries\([^)]*\b(?:rt64|SDL2)")


if __name__ == "__main__":
    unittest.main()
