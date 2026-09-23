#pragma once
#include "portable_text.hpp"
namespace srw64::text {
// Application policy only. FontSet itself never searches the filesystem.
// With SRW64_FONT_DIR (the packaged fonts): HarmonyOS Sans SC for Chinese and
// Japanese, Condensed then SC for English, the symbol font behind both.
// Without it (unit tests, older probes): one system CJK face.
std::shared_ptr<const FontSet> game_fonts(const std::string& locale);
std::filesystem::path game_font_path();
}
