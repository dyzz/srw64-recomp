#include "game_fonts.hpp"
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace srw64::text {
std::filesystem::path game_font_path() {
    if(const char* path=std::getenv("SRW64_TEXT_FONT")) {
        auto result=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path)));
        if(!std::filesystem::is_regular_file(result))throw std::runtime_error("SRW64_TEXT_FONT is not a font file");
        return result;
    }
    for(const auto* path:{
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"})
        if(std::filesystem::is_regular_file(path))return path;
#ifdef _WIN32
    // Respect relocated Windows installations and Unicode directory names.
    if(const auto* windows=_wgetenv(L"WINDIR")) {
        const auto path=std::filesystem::path(windows)/L"Fonts"/L"msyh.ttc";
        if(std::filesystem::is_regular_file(path))return path;
    }
#endif
    throw std::runtime_error("No CJK font found. Install Noto Sans CJK or set SRW64_TEXT_FONT to a TTF/OTF/TTC file.");
}
std::shared_ptr<const FontSet> game_fonts(const std::string& locale) {
    if(locale!="zh-Hans" && locale!="ja" && locale!="en")
        throw std::runtime_error("Game text supports zh-Hans, ja and en only");
    const auto path=game_font_path();
    // Noto's standard CJK collection orders JP, KR, SC, TC, HK faces.
    const long face=path.filename()=="NotoSansCJK-Regular.ttc" && locale=="zh-Hans"?2:0;
    std::vector<FontSource> sources;
    // Arial Unicode draws ，！（ centred, as Traditional Chinese does; Simplified
    // Chinese puts them at the side. Hiragino Sans GB ships with macOS; Arial
    // Unicode stays behind it for anything it lacks.
    if(locale=="zh-Hans" && !std::getenv("SRW64_TEXT_FONT") && path.filename()=="Arial Unicode.ttf")
        for(const auto* chinese:{"/System/Library/Fonts/Hiragino Sans GB.ttc"})
            if(std::filesystem::is_regular_file(chinese))sources.push_back({chinese,0});
    sources.push_back({path,face});
    static std::mutex mutex;
    static std::map<std::vector<std::pair<std::filesystem::path,long>>,std::shared_ptr<const FontSet>> cache;
    std::vector<std::pair<std::filesystem::path,long>> key;
    for(const auto& source:sources)key.emplace_back(source.path,source.face_index);
    std::lock_guard lock(mutex);
    auto& fonts=cache[key];
    if(!fonts)fonts=std::make_shared<FontSet>(sources);
    return fonts;
}
}
