#pragma once
#include "localization/locale_fallbacks.hpp"
#include <map>
#include <string>

namespace srw64::ui {
// The shared UI registers "srw64-ui" (the fallback face for everything) and,
// per language, a family the documents switch to: HarmonyOS Sans Condensed for
// English and HarmonyOS Sans TC for Traditional Chinese (Taiwan's glyphs, ，。 centred)
// with the packaged fonts, or, on the system-font path, a Simplified Chinese face
// because Arial Unicode draws ，！（ centred as Traditional Chinese does.
inline std::string& chinese_font_family() { static std::string family; return family; }
inline std::string& traditional_font_family() { static std::string family; return family; }
inline std::string& english_font_family() { static std::string family; return family; }
// A language package's own face (docs/design/mod-language-packs.md), once the front end
// has loaded it; else the package's locale takes its fallback's family.
inline std::map<std::string, std::string>& package_font_families() { static std::map<std::string, std::string> families; return families; }
inline std::string locale_font_css(const std::string& requested) {
    if (const auto found = package_font_families().find(requested); found != package_font_families().end())
        return "body { font-family: " + found->second + "; }\n";
    const auto locale = localization::style(requested);
    const auto& family = locale == "zh-Hans" ? chinese_font_family() : locale == "zh-Hant" ? traditional_font_family()
                       : locale == "en" ? english_font_family() : std::string();
    return family.empty() ? std::string() : "body { font-family: " + family + "; }\n";
}
}
