#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

// The native ユニット改造 screens (docs/native/native-upgrade-screens.md): the machine
// list (intermission screens 2 and 3) and the five-stat upgrade (screen 10). As with
// the main menu, the original set-ups draw nothing and the shared UI shows the same
// panels; a choice writes the cursor and feeds the original step one A/B edge, so
// prices, the cap rule (upgrade_rules.hpp), propagation and the EW swap stay original.
namespace srw64::upgrade_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread, for page `serial`: list "move:R", "page:P", "choose", "back";
// stats "move:S", "choose:S", "confirm", "cancel", "dismiss", "back".
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
