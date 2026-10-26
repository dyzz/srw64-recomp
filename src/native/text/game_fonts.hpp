#pragma once
#include "portable_text.hpp"
namespace srw64::text {
// Application policy only. FontSet itself never searches the filesystem.
// With SRW64_FONT_DIR (the packaged fonts): HarmonyOS Sans SC for Chinese and
// Japanese, Condensed then SC for English, the symbol font behind both.
// Without it (unit tests, older probes): one system CJK face.
// weight picks the variable fonts' named instance (FontSource::weight); 0 is Regular.
std::shared_ptr<const FontSet> game_fonts(const std::string& locale,int weight=0);
// The same chain, for callers that keep their own FontSet (a worker thread).
std::vector<FontSource> game_font_sources(const std::string& locale,int weight=0);
// The chapter title cards' brush face ahead of that chain: the cards' own lettering for
// Japanese, Ma Shan Zheng for Simplified Chinese and English, Bakudai's brush (SRW64BrushTC)
// for Traditional Chinese (tools/content/build_title_fonts.py).
// Without it in SRW64_FONT_DIR the cards keep the plain chain.
std::vector<FontSource> title_font_sources(const std::string& locale,int weight=0);
std::filesystem::path game_font_path();
}
