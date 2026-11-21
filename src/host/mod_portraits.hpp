#pragma once
// A character's portrait for the host's pages (docs/design/unit-mod-kit.md §5.5): the
// battle assets' entry by its number; a new character's own (its package's 96x96 picture,
// its HD one through portraits::mod_picture), else the entry of the character it copies.
#include "mod_data.hpp"
#include "table_window.hpp"
#include "json/json.hpp"
#include <string>

namespace srw64::mod_portraits {
using json=nlohmann::json;
inline json art(const json& portraits,unsigned pilot) {
    if(!portraits.is_object())return json::object();
    if(const auto found=portraits.find(std::to_string(pilot));found!=portraits.end())return *found;
    if(!table_window::new_pilot(pilot))return json::object();
    const auto own=mod_data::portraits();
    if(const auto p=own.find(pilot);p!=own.end()) {
        json entry={{"path",p->second.pixels.string()},{"width",96},{"height",96},{"resources",{p->second.image,p->second.palette}}};
        if(!p->second.hd.empty())entry["hd"]=p->second.hd.string();
        return entry;
    }
    const auto copies=mod_data::pilot_copies();
    if(const auto c=copies.find(pilot);c!=copies.end())
        if(const auto found=portraits.find(std::to_string(c->second));found!=portraits.end())return *found;
    return json::object();
}
}
