#pragma once
// A unit's picture for the native pages (battle_assets.py): the pose the game reads for it
// (0x84E40, six bytes a unit, a package's bytes over the ROM's), so a MOD unit on a free
// number shows its own pose rather than the copy's; the unit number's row when that pose
// has none.
#include "upgrade_rules.hpp"
#include "json/json.hpp"
#include <string>

namespace srw64::unit_pose_art {
inline constexpr uint32_t poses=0x84E40, pose_size=6, units=363;

inline nlohmann::json of(const nlohmann::json& assets,unsigned number) {
    if(number<units && assets.contains("poses")) {
        try {
            const auto at=[&](unsigned k){return std::to_string(upgrades::game_read(poses+number*pose_size+k*2,2));};
            const auto key=at(0)+"-"+at(1)+"-"+at(2);
            if(const auto found=assets["poses"].find(key);found!=assets["poses"].end())return *found;
        } catch(const std::exception&) {}
    }
    if(assets.contains("units"))
        if(const auto found=assets["units"].find(std::to_string(number));found!=assets["units"].end())return *found;
    return nlohmann::json::object();
}
}
