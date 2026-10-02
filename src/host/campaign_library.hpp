#pragma once
#include "campaign.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// Installed custom campaigns, offered as additional scenarios (DLC) on the title screen
// (docs/design/custom-campaign.md). Each is a compiled campaign at <dir>/<id>/campaign.json
// in one of SRW64_CAMPAIGN_DIRS (the user's campaigns folder first, then the bundled
// ones); a campaign plays from its own saves, so changing campaign restarts the program
// like changing discs: the title screen writes SRW64_CAMPAIGN_SWITCH and the game quits,
// and the launcher starts again with --campaign, or without it for the main game.
namespace srw64::campaign_library {
namespace fs=std::filesystem;
struct Entry {
    std::string id, version, author;
    fs::path path;
    nlohmann::json name, description;   // a string or {locale: string}
    unsigned stages{};
    bool saves{};                        // its save library holds a card
};

inline std::vector<fs::path> directories() {
    std::vector<fs::path> dirs;
    const char* list=std::getenv("SRW64_CAMPAIGN_DIRS");
    if(!list)return dirs;
#ifdef _WIN32
    constexpr char separator=';';
#else
    constexpr char separator=':';
#endif
    std::string text=list;
    for(size_t start=0;start<=text.size();) {
        const auto end=text.find(separator,start);
        const auto item=text.substr(start,end==std::string::npos?std::string::npos:end-start);
        if(!item.empty())dirs.emplace_back(item);
        if(end==std::string::npos)break;
        start=end+1;
    }
    return dirs;
}

// The text of a name or description in a locale: the locale, then ja, then any.
inline std::string text(const nlohmann::json& field,const std::string& locale) {
    if(field.is_string())return field.get<std::string>();
    if(!field.is_object())return {};
    for(const auto& key:{locale,std::string("ja")})
        if(field.contains(key) && field.at(key).is_string())return field.at(key).get<std::string>();
    for(const auto& [key,value]:field.items())if(value.is_string())return value.get<std::string>();
    return {};
}

// Every installed campaign, the first directory's copy of an id winning; a file that is
// not a compiled campaign is left out.
inline std::vector<Entry> list() {
    std::vector<Entry> entries;
    std::set<std::string> seen;
    const char* saves_root=std::getenv("SRW64_CAMPAIGN_SAVES");
    for(const auto& dir:directories()) {
        std::error_code error;
        if(!fs::is_directory(dir,error))continue;
        std::vector<fs::path> found;
        for(const auto& item:fs::directory_iterator(dir,error))if(fs::is_regular_file(item.path()/"campaign.json",error))found.push_back(item.path()/"campaign.json");
        std::sort(found.begin(),found.end());
        for(const auto& path:found) {
            std::ifstream file(path);
            const auto document=nlohmann::json::parse(file,nullptr,false);
            if(document.is_discarded() || document.value("schema","")!="srw64.campaign-image.v1" || !document.contains("id"))continue;
            Entry entry;
            entry.id=document.at("id").get<std::string>();
            if(!seen.insert(entry.id).second)continue;
            entry.path=path;entry.version=document.value("version","");entry.author=document.value("author","");
            entry.name=document.value("name",nlohmann::json(entry.id));
            entry.description=document.value("description",nlohmann::json());
            entry.stages=unsigned(document.value("stages",nlohmann::json::array()).size());
            entry.saves=saves_root && fs::is_regular_file(fs::path(saves_root)/entry.id/"saves"/"cartridge.sram",error);
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

// The launcher listens for a switch; debug runs without one show no DLC entry.
inline bool switching(){const char* p=std::getenv("SRW64_CAMPAIGN_SWITCH");return p && *p;}

// Asks the launcher to start the given campaign next (empty: the main game). The caller
// then closes the game, which saves and quits as usual.
inline void request(const fs::path& campaign) {
    const char* path=std::getenv("SRW64_CAMPAIGN_SWITCH");
    if(!path || !*path)throw std::runtime_error("campaign switching needs SRW64_CAMPAIGN_SWITCH");
    std::ofstream(path)<<nlohmann::json{{"schema","srw64.campaign-switch.v1"},{"campaign",campaign.string()}}.dump()<<'\n';
}
}
