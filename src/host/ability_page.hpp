#pragma once
#include "json/json.hpp"
#include <utility>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <string>

// The native ユニット能力／パイロット能力 screens (docs/native/native-ability-screens.md):
// the machine list (intermission screen 4) with the unit page (13) and its weapon
// list (14), and the pilot list (5) with the pilot page (15). Read-only screens:
// the original set-ups build their lists without drawing, the shared UI shows the
// same panels, and screen changes feed the original step one button edge.
namespace srw64::ability_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// The same without copying it, and a revision that moves only when it changed (the UI's
// frames, page_snapshot.hpp).
std::pair<std::shared_ptr<const nlohmann::json>,uint64_t> shared_state();
// Window thread, for page `serial`: lists "move:R", "page:P", "choose", "back";
// unit / pilot pages "prev", "next", "choose" (unit: the weapons), "back";
// weapons "move:R", "page:P", "back".
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
