#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

// The native 強化パーツ screens (docs/native/native-parts-screens.md): the machine
// list (intermission screen 7), the unit's slots with the part inventory (screen
// 18) and the owned copies of one part (screen 19). As with ユニット改造, the
// original set-ups build their lists without drawing and the shared UI shows the
// same panels; equipping, removing and taking a part from another unit feed the
// original step one A/B edge, so the unit and inventory records stay original.
namespace srw64::parts_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread, for page `serial`: list "move:R", "page:P", "choose", "back";
// slots "move:S", "choose", "back" (slot cursor) and, with the inventory open,
// "move:R", "page:P", "choose", "cancel"; holders "move:R", "choose", "back".
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
