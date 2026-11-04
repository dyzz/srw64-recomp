#include "game_fonts.hpp"
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace srw64::text {
namespace {
std::filesystem::path utf8_path(const char* value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
}
// The packaged fonts (tools/content/prepare_fonts.py): the variable HarmonyOS
// Sans faces (TC first for Traditional Chinese, SC behind it for kana) and the symbol font. Launchers always point SRW64_FONT_DIR at them;
// a missing file is an error, never a silent switch to a system font.
struct PackageFonts {std::string fallback;std::vector<std::filesystem::path> text;std::filesystem::path title;};
std::mutex package_mutex;
std::map<std::string,PackageFonts> package_fonts;
std::optional<PackageFonts> package(const std::string& locale) {
    {
        std::lock_guard lock(package_mutex);
        const auto found=package_fonts.find(locale);
        if(found!=package_fonts.end())return found->second;
    }
    // Korean, ours (docs/design/korean.md), as a package's language would have it: Droid Sans
    // Fallback (Apache-2.0, content/fonts) in front of the English faces, and the chapter
    // title cards' brush face, a subset of Nanum Brush Script (tools/content/build_korean_fonts.py).
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(locale!="ko" || std::getenv("SRW64_TEXT_FONT") || !dir || !*dir)return std::nullopt;
    const auto path=utf8_path(dir)/"DroidSansFallback.ttf",title=utf8_path(dir)/"SRW64BrushKO.ttf";
    if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Missing font "+path.string()+": run tools/content/prepare_fonts.py");
    return PackageFonts{"en",{path},std::filesystem::is_regular_file(title)?title:std::filesystem::path()};
}
std::vector<FontSource> packaged(const std::filesystem::path& dir,const std::string& locale,int weight) {
    std::vector<std::string> names;
    // SRW64Prompts holds the hints' button icons in the Private Use Area (text/button_prompts.hpp).
    if(locale=="en")names={"HarmonyOS_Sans_Condensed.ttf","HarmonyOS_Sans_SC.ttf","SRW64Symbols.ttf","SRW64Prompts.ttf"};
    else if(locale=="zh-Hant")names={"HarmonyOS_Sans_TC.ttf","HarmonyOS_Sans_SC.ttf","SRW64Symbols.ttf","SRW64Prompts.ttf"};
    else names={"HarmonyOS_Sans_SC.ttf","SRW64Symbols.ttf","SRW64Prompts.ttf"};
    std::vector<FontSource> sources;
    // Each file is looked for once: every text layout asks for the fonts, and on the Switch a
    // lookup is a request to the SD card's file system (a stack sampled in the world map's
    // dialogue was in one).
    static std::mutex checked_mutex;
    static std::set<std::filesystem::path> checked;
    for(const auto& name:names) {
        const auto path=dir/name;
        std::lock_guard lock(checked_mutex);
        if(checked.contains(path)){sources.push_back({path,0,name.starts_with("SRW64")?0:weight});continue;}
        if(!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Missing font "+path.string()+": run tools/content/prepare_fonts.py "
                "(HarmonyOS Sans 2.040 comes from https://developer.huawei.com/consumer/cn/design/resource/)");
        checked.insert(path);
        // The symbol and prompt fonts have one weight.
        sources.push_back({path,0,name.starts_with("SRW64")?0:weight});
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
    if(const char* dir=std::getenv("SRW64_FONT_DIR"); dir && *dir)return packaged(utf8_path(dir),"zh-Hans",0).front().path;
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
void set_locale_fonts(const std::string& locale,const std::string& fallback,std::vector<std::filesystem::path> text,std::filesystem::path title) {
    std::lock_guard lock(package_mutex);
    package_fonts[locale]={fallback,std::move(text),std::move(title)};
}
std::vector<std::filesystem::path> locale_fonts(const std::string& locale) {
    const auto fonts=package(locale);
    return fonts?fonts->text:std::vector<std::filesystem::path>();
}
std::vector<FontSource> game_font_sources(const std::string& locale,int weight) {
    if(const auto fonts=package(locale)) {
        // A package's faces have whatever weights they have: their default instance.
        std::vector<FontSource> sources;
        for(const auto& path:fonts->text)sources.push_back({path,0,0});
        for(auto& source:game_font_sources(fonts->fallback,weight))sources.push_back(std::move(source));
        return sources;
    }
    if(locale=="ko")return game_font_sources("en",weight);  // the system-font path: the CJK face has Hangul
    if(locale!="zh-Hans" && locale!="zh-Hant" && locale!="ja" && locale!="en")
        throw std::runtime_error("Game text supports zh-Hans, zh-Hant, ja, en and ko only");
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(!std::getenv("SRW64_TEXT_FONT") && dir && *dir)return packaged(utf8_path(dir),locale,weight);
    // Unit tests and older probes without packaged fonts: one CJK face, one weight.
    const auto path=game_font_path();
    // Noto's standard CJK collection orders JP, KR, SC, TC, HK faces.
    const long face=path.filename()!="NotoSansCJK-Regular.ttc"?0:locale=="zh-Hans"?2:locale=="zh-Hant"?3:0;
    return {{path,face}};
}
std::vector<FontSource> title_font_sources(const std::string& locale,int weight) {
    if(const auto fonts=package(locale)) {
        std::vector<FontSource> sources;
        if(!fonts->title.empty())sources.push_back({fonts->title,0,0});
        for(const auto& path:fonts->text)sources.push_back({path,0,0});
        for(auto& source:title_font_sources(fonts->fallback,weight))sources.push_back(std::move(source));
        return sources;
    }
    auto sources=game_font_sources(locale,weight);
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(std::getenv("SRW64_TEXT_FONT") || !dir || !*dir)return sources;
    // Ma Shan Zheng has Simplified characters only: Traditional cards take Bakudai's brush
    // (SRW64BrushTC, tools/content/build_title_fonts.py).
    const auto path=utf8_path(dir)/(locale=="ja"?"SRW64ChapterTitle.ttf":locale=="zh-Hant"?"SRW64BrushTC.ttf":"MaShanZheng-Regular.ttf");
    if(std::filesystem::is_regular_file(path))sources.insert(sources.begin(),{path,0,0});
    return sources;
}
std::vector<FontSource> banner_font_sources(const std::string& locale,int weight) {
    auto sources=game_font_sources(locale,weight);
    // The Korean battle banners: a subset of Gmarket Sans Bold (tools/content/build_korean_fonts.py).
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(locale!="ko" || std::getenv("SRW64_TEXT_FONT") || !dir || !*dir)return sources;
    const auto path=utf8_path(dir)/"SRW64BannerKO.ttf";
    if(std::filesystem::is_regular_file(path))sources.insert(sources.begin(),{path,0,0});
    return sources;
}
std::shared_ptr<const FontSet> game_fonts(const std::string& locale,int weight) {
    const auto sources=game_font_sources(locale,weight);
    static std::mutex mutex;
    static std::map<std::vector<std::tuple<std::filesystem::path,long,int>>,std::shared_ptr<const FontSet>> cache;
    std::vector<std::tuple<std::filesystem::path,long,int>> key;
    for(const auto& source:sources)key.emplace_back(source.path,source.face_index,source.weight);
    std::lock_guard lock(mutex);
    auto& fonts=cache[key];
    if(!fonts)fonts=std::make_shared<FontSet>(sources);
    return fonts;
}
}
