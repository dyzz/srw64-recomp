#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

// A custom campaign (docs/design/custom-campaign.md): mini stages chained through the
// scenes they borrow. Each stage stands in for one original scene, so the scene byte
// 8010F5F0 that 3D4B writes and the saves keep identifies the stage. This header keeps
// the campaign's identity and chapter titles; mini_stage.hpp holds the stage images.
namespace srw64::campaign {
struct Info {
    std::string id, version;
    nlohmann::json name;                           // a string or {locale: string}
    uint32_t start{};                              // scene of the first stage
    std::map<uint32_t,nlohmann::json> titles;      // scene -> title, a string or {locale: string}
    std::map<uint32_t,std::string> keys;           // scene -> stage key, for logs
    // The stage info page (stage_info.hpp): each stage's metadata (srw64.stage-meta.v1)
    // and the campaign's status rules (srw64.stage-status.v1), when it has them.
    std::map<uint32_t,nlohmann::json> meta;
    nlohmann::json status;
};
struct State {std::mutex mutex;std::optional<Info> info;};
inline State& state(){static State s;return s;}

inline bool active(){auto& s=state();std::lock_guard lock(s.mutex);return s.info.has_value();}
inline std::optional<Info> info(){auto& s=state();std::lock_guard lock(s.mutex);return s.info;}
inline void set(std::optional<Info> info){auto& s=state();std::lock_guard lock(s.mutex);s.info=std::move(info);}
// The running campaign's id ("" in the main story), one stage's metadata (null when it
// has none) and its status rules (null), without copying the rest.
inline std::string id(){auto& s=state();std::lock_guard lock(s.mutex);return s.info?s.info->id:std::string();}
inline nlohmann::json stage_meta(uint32_t scene) {
    auto& s=state();std::lock_guard lock(s.mutex);
    if(!s.info)return nullptr;
    const auto found=s.info->meta.find(scene);
    if(found==s.info->meta.end())return nullptr;
    auto meta=found->second;
    // Without a title of its own, the stage's chapter title from campaign.json.
    if(!meta.contains("title"))
        if(const auto title=s.info->titles.find(scene);title!=s.info->titles.end())meta["title"]=title->second;
    return meta;
}
inline nlohmann::json status(){auto& s=state();std::lock_guard lock(s.mutex);return s.info?s.info->status:nlohmann::json();}

// The chapter title a stage gives its scene, in the given locale (zh-Hant falls back to
// zh-Hans); falls back to ja, then to any language the title has. Nothing when no
// campaign borrows the scene.
inline std::optional<std::string> title(uint32_t scene,const std::string& locale) {
    auto& s=state();std::lock_guard lock(s.mutex);
    if(!s.info)return std::nullopt;
    const auto found=s.info->titles.find(scene);
    if(found==s.info->titles.end())return std::nullopt;
    const auto& title=found->second;
    if(title.is_string())return title.get<std::string>();
    for(const auto& key:{locale,std::string(locale=="zh-Hant"?"zh-Hans":"ja"),std::string("ja")})
        if(title.contains(key) && title.at(key).is_string())return title.at(key).get<std::string>();
    for(const auto& [key,value]:title.items())if(value.is_string())return value.get<std::string>();
    return std::nullopt;
}

// A notice in the current language (UI key; {n} is the scene). native_dialogue.cpp,
// which holds the catalog.
std::string notice(const std::string& key,uint32_t scene=0);

// Scenes a stage must not borrow: the 13 "（前）" scenes and 132 shorten the
// intermission menu (D_801DC6D4) and 109-122 are the Link Battler stages (D_801DCABC).
inline bool borrowable(uint32_t scene) {
    if(scene>142 || (scene>=109 && scene<=122))return false;
    for(uint32_t shortened:{38u,48u,68u,73u,79u,81u,83u,86u,88u,93u,95u,96u,104u,132u})
        if(scene==shortened)return false;
    return true;
}
}
