#pragma once
#include <string>

namespace srw64::ui {
// The shared UI registers "srw64-ui" (a CJK face, the fallback for everything)
// and, when one is installed, a Simplified Chinese face: Arial Unicode draws
// ，！（ centred as Traditional Chinese does. Documents add this rule for zh-Hans.
inline std::string& chinese_font_family() { static std::string family; return family; }
inline std::string locale_font_css(const std::string& locale) {
    const auto& family = chinese_font_family();
    return locale == "zh-Hans" && !family.empty() ? "body { font-family: " + family + "; }\n" : std::string();
}
}
