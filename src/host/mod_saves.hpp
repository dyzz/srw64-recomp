#pragma once
// A save made with a package's new units, loaded without that package (docs/design/
// mod-names-art-data.md §2.3.2): a free unit number (mod_units::free_units) means a copy of
// another unit in the ROM, so the unit would come back as that one, with its weapons and
// crew. The load is turned back: a banner names the package and the game returns to the
// title (quick_load::request_title) as soon as it stands where that works.
//
// Which package a unit came from goes beside the record, as the story state does (story_mod
// sidecars, by the record's hash): <keep>/mod-units/<hash>.json. A save without the file
// still turns back, naming no package.
#include "guest_memory.hpp"
#include "mod_data.hpp"
#include "notices.hpp"
#include "quick_load.hpp"
#include "story_mod.hpp"
#include "json/json.hpp"
#include "localization/catalog.hpp"
#include "localization/locale_fallbacks.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <string>

namespace srw64::mod_saves {
using json=nlohmann::json;
namespace fs=std::filesystem;
inline constexpr uint32_t pool=0x8016A210, pool_size=0x54, pool_count=140;   // the player's unit instances
inline constexpr uint32_t game_mode=0x8015DA02;   // as quick_load.cpp reads it
inline constexpr const char* schema="srw64.mod-units-state.v1";

// The unit numbers of the player's instances in use (+0 non-zero, +2 the unit).
inline std::set<unsigned> units(const uint8_t* ram) {
    std::set<unsigned> out;
    for(uint32_t i=0;i<pool_count;++i) {
        const uint32_t at=pool+i*pool_size;
        if(guest::read(ram,at,1))out.insert(guest::read(ram,at+2,2));
    }
    return out;
}
inline bool free_unit(unsigned unit) {
    for(const auto u:mod_units::free_units)if(u==unit)return true;
    return false;
}
inline fs::path sidecar(uint64_t hash) {
    char name[32];std::snprintf(name,sizeof name,"%016llx.json",static_cast<unsigned long long>(hash));
    const auto& keep=story_mod::state().keep;
    const auto folder=keep?keep():fs::path();
    return folder.empty()?fs::path():folder/"mod-units"/name;
}
inline std::string name_in(const json& name,const std::string& locale) {
    if(name.is_string())return name.get<std::string>();
    if(!name.is_object())return {};
    for(const auto& key:localization::fallbacks(locale))
        if(const auto found=name.find(key);found!=name.end() && found->is_string())return found->get<std::string>();
    for(const auto& [_,value]:name.items())if(value.is_string())return value.get<std::string>();
    return {};
}

struct State {
    std::mutex mutex;
    json packages=json::object();     // hash -> {package: name}, for records still in this run
    std::atomic<bool> turn_back{false};
    // Game thread only: frame boundaries the title return has been usable, whether it is
    // asked, boundaries since, whether the banner went up.
    unsigned ready{},waited{};
    bool asked{},posted{};
    std::string text;                  // the banner, under mutex
};
// Frame boundaries (two to a VI at 120 Hz): the map settled before asking, and the time the
// press gets before a new one when it ended without the title.
inline constexpr unsigned settle=60, patience=120;
inline State& state(){static State value;return value;}

// After the game serializes a record: the packages of its new units, beside it.
inline void serialized(const uint8_t* ram,bool lasting,story_mod::Record kind) {
    const auto owners=mod_data::unit_owners();
    json packages=json::object();
    json added=json::array();
    for(const auto unit:units(ram))
        if(const auto o=owners.find(unit);o!=owners.end()){packages[o->second.package]=o->second.name;added.push_back(unit);}
    const auto hash=story_mod::record_hash(ram,kind);
    auto& s=state();
    {std::lock_guard lock(s.mutex);if(packages.empty())s.packages.erase(std::to_string(hash));else s.packages[std::to_string(hash)]=packages;}
    if(!lasting || packages.empty())return;
    if(const auto file=sidecar(hash);!file.empty()) {
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        std::ofstream(file)<<json({{"schema",schema},{"packages",packages},{"units",added}}).dump()<<'\n';
    }
}
// After the game restores a record: turn the load back when one of its units is a free number
// no active package adds.
inline void restored(const uint8_t* ram,story_mod::Record kind) {
    const auto owners=mod_data::unit_owners();
    json missing=json::array();
    for(const auto unit:units(ram))if(free_unit(unit) && !owners.count(unit))missing.push_back(unit);
    if(missing.empty())return;
    const auto hash=story_mod::record_hash(ram,kind);
    auto& s=state();
    json packages=json::object();
    {std::lock_guard lock(s.mutex);if(const auto p=s.packages.find(std::to_string(hash));p!=s.packages.end())packages=*p;}
    if(packages.empty())
        if(const auto file=sidecar(hash);!file.empty() && fs::exists(file)) {
            std::ifstream in(file);
            const auto document=json::parse(in,nullptr,false);
            if(!document.is_discarded() && document.value("schema","")==schema)packages=document.value("packages",json::object());
        }
    const auto& catalog=localization::catalog();
    std::string names;
    for(const auto& [id,name]:packages.items()) {
        const auto shown=name_in(name,catalog.locale);
        names+=(names.empty()?"":"、")+(shown.empty()?id:shown);
    }
    std::string text=catalog.ui(names.empty()?"mod_save_needs_unknown":"mod_save_needs");
    if(const auto at=text.find("{name}");at!=std::string::npos)text.replace(at,6,names);
    {std::lock_guard lock(s.mutex);s.text=text;}
    s.turn_back=true;
    std::fprintf(stderr,"SRW64_MOD_SAVE %s\n",json({{"action","turned_back"},{"record",kind==story_mod::Record::slot?"slot":"suspend"},
                                                    {"units",missing},{"packages",packages},{"sidecar",sidecar(hash).filename().string()}}).dump().c_str());
}
// Game thread, every frame boundary (after quick_load::frame): the title once the game has
// stood where the title return works for a moment (asked during the restore, mode 0x11, the
// map can let the press go by), asked again when a press ends without it; the banner when
// first asked and again at the title.
inline void frame(const uint8_t* ram) {
    auto& s=state();
    if(!s.turn_back.load()){s.ready=s.waited=0;s.asked=s.posted=false;return;}
    const auto post=[&]{std::lock_guard lock(s.mutex);notices::post("mod-save",s.text);};
    const unsigned mode=guest::read(ram,game_mode,1);
    if(mode==1) {   // the logo, then the title
        if(s.asked)post();
        s.turn_back=false;return;
    }
    if(!s.asked) {
        s.ready=quick_load::usable_title() && mode!=0x11?s.ready+1:0;
        if(s.ready<settle)return;
        quick_load::request_title();s.asked=true;s.waited=0;
        if(!s.posted){post();s.posted=true;}
        return;
    }
    if(++s.waited>=patience && quick_load::usable_title()){s.asked=false;s.ready=0;}
}
}
