#pragma once
// A unit's picture for the native pages (battle_assets.py): the pose the game reads for it
// (0x84E40, six bytes a unit, a package's bytes over the ROM's), so a MOD unit on a free
// number shows its own pose rather than the copy's; the unit number's row when that pose
// has none.
#include "mod_data.hpp"
#include "upgrade_rules.hpp"
#include "table_window.hpp"
#include "json/json.hpp"
#include <string>

namespace srw64::unit_pose_art {
inline constexpr uint32_t poses=0x84E40, pose_size=6, units=363;

// The picture of a pose (scene, atlas, palette) in the battle assets: battle_assets.py lists
// them by pose ("poses"); the ROM importer the players' builds use (rom_import.cpp) lists only
// the units by number, each with its pose's "resources", so a MOD unit on a new number that
// copies a ROM unit's pose (Super Earthgain, 306's) found nothing on Windows (2026-11-22).
inline const nlohmann::json* by_pose(const nlohmann::json& assets,unsigned scene,unsigned atlas,unsigned palette) {
    if(!scene)return nullptr;
    if(const auto list=assets.find("poses");list!=assets.end() && list->is_object()) {
        const auto key=std::to_string(scene)+"-"+std::to_string(atlas)+"-"+std::to_string(palette);
        if(const auto found=list->find(key);found!=list->end())return &*found;
    }
    if(const auto list=assets.find("units");list!=assets.end() && list->is_object())
        for(const auto& [id,art]:list->items()) {
            const auto r=art.find("resources");
            if(r!=art.end() && r->is_array() && r->size()==3 && (*r)[0]==scene && (*r)[1]==atlas && (*r)[2]==palette)return &art;
        }
    return nullptr;
}

inline nlohmann::json of(const nlohmann::json& assets,unsigned number) {
    try {
        // A package's own sprite (mod_units.hpp sprites): its picture file.
        const auto word=[&](unsigned k){return upgrades::game_read(table_window::row(table_window::poses,number)+k*2,2);};
        if(const auto sprite=mod_data::sprite(word(0),word(1),word(2)))return mod_units::art(*sprite);
    } catch(const std::exception&) {}
    try {
        const auto at=[&](unsigned k){return unsigned(upgrades::game_read(table_window::row(table_window::poses,number)+k*2,2));};
        if(const auto found=by_pose(assets,at(0),at(1),at(2)))return *found;
    } catch(const std::exception&) {}
    if(assets.contains("units"))
        if(const auto found=assets["units"].find(std::to_string(number));found!=assets["units"].end())return *found;
    return nlohmann::json::object();
}
}
