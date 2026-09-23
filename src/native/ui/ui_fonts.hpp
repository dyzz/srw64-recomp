#pragma once
#include <string>

namespace srw64::ui {
// The shared UI registers "srw64-ui" (the fallback face for everything) and,
// per language, a family the documents switch to: HarmonyOS Sans Condensed for
// English with the packaged fonts, or, on the system-font path, a Simplified
// Chinese face because Arial Unicode draws ，！（ centred as Traditional Chinese does.
inline std::string& chinese_font_family() { static std::string family; return family; }
inline std::string& english_font_family() { static std::string family; return family; }
inline std::string locale_font_css(const std::string& locale) {
    const auto& family = locale == "zh-Hans" ? chinese_font_family() : locale == "en" ? english_font_family() : std::string();
    return family.empty() ? std::string() : "body { font-family: " + family + "; }\n";
}
}
