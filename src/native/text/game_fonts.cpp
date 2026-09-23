#include "game_fonts.hpp"
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace srw64::text {
namespace {
std::filesystem::path utf8_path(const char* value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
}
// The packaged fonts (tools/content/prepare_fonts.py): HarmonyOS Sans and the
// four-glyph symbol font. Launchers always point SRW64_FONT_DIR at them; a
// missing file is an error, never a silent switch to a system font.
std::vector<FontSource> packaged(const std::filesystem::path& dir,const std::string& locale) {
    std::vector<std::string> names;
    if(locale=="en")names={"HarmonyOS_Sans_Condensed_Regular.ttf","HarmonyOS_Sans_SC_Regular.ttf","SRW64Symbols.ttf"};
    else names={"HarmonyOS_Sans_SC_Regular.ttf","SRW64Symbols.ttf"};
    std::vector<FontSource> sources;
    for(const auto& name:names) {
        const auto path=dir/name;
        if(!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Missing font "+path.string()+": run tools/content/prepare_fonts.py "
                "(HarmonyOS Sans comes from https://developer.huawei.com/images/download/general/HarmonyOS-Sans.zip)");
        sources.push_back({path,0});
    }
    return sources;
}
}
std::filesystem::path game_font_path() {
    if(const char* path=std::getenv("SRW64_TEXT_FONT")) {
        auto result=utf8_path(path);
        if(!std::filesystem::is_regular_file(result))throw std::runtime_error("SRW64_TEXT_FONT is not a font file");
        return result;
    }
    if(const char* dir=std::getenv("SRW64_FONT_DIR"); dir && *dir)return packaged(utf8_path(dir),"zh-Hans").front().path;
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
    throw std::runtime_error("No CJK font found. Run tools/content/prepare_fonts.py and set SRW64_FONT_DIR, or set SRW64_TEXT_FONT to a TTF/OTF/TTC file.");
}
std::shared_ptr<const FontSet> game_fonts(const std::string& locale) {
    if(locale!="zh-Hans" && locale!="ja" && locale!="en")
        throw std::runtime_error("Game text supports zh-Hans, ja and en only");
    std::vector<FontSource> sources;
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(!std::getenv("SRW64_TEXT_FONT") && dir && *dir)sources=packaged(utf8_path(dir),locale);
    else {
        // Unit tests and older probes without packaged fonts: one CJK face.
        const auto path=game_font_path();
        // Noto's standard CJK collection orders JP, KR, SC, TC, HK faces.
        const long face=path.filename()=="NotoSansCJK-Regular.ttc" && locale=="zh-Hans"?2:0;
        sources.push_back({path,face});
    }
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
