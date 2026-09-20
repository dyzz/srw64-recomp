#pragma once
#include "portable_text.hpp"
namespace srw64::text {
// Application policy only. FontSet itself never searches the filesystem.
// One CJK outline face covers the supported Chinese/Japanese/English UI.
std::shared_ptr<const FontSet> game_fonts(const std::string& locale);
std::filesystem::path game_font_path();
}
