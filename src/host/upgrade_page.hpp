#pragma once
#include "json/json.hpp"
#include "recomp.h"
#include <cstdint>
#include <filesystem>
#include <string>

// The native ユニット改造 screens (docs/native/native-upgrade-screens.md): the machine
// list (intermission screens 2 and 3) and the five-stat upgrade (screen 10). As with
// the main menu, the original set-ups draw nothing and the shared UI shows the same
// panels; a choice writes the cursor and feeds the original step one A/B edge, so
// prices, the cap rule (upgrade_rules.hpp), propagation and the EW swap stay original.
// ←→ plan several levels at once; the confirm feeds the paid step once per level.
namespace srw64::upgrade_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread, for page `serial`: list "move:R", "page:P", "choose", "back";
// stats "move:S", "more:S" / "less:S" (plan one level more or less), "choose:S",
// "confirm", "cancel", "dismiss", "back" (drops a plan first); weapon confirm
// "move:0/1", "more" / "less", "confirm", "cancel", "dismiss", "back"; the unified
// weapon page (settings::unified_weapon_upgrades) "more" / "less", "page:P", "choose",
// "confirm", "cancel", "dismiss", "back".
void answer(uint64_t serial,const std::string& action);
// After the インターミッション is entered (801D8F74): with unified weapon upgrades, weapons
// below their unit's unified level come up to it for free; with that or the cap-break
// rule on, a full-upgrade bonus whose weapon is already at its original cap unlocks.
void entered(uint8_t* ram,recomp_context* ctx);
// A machine swap's inheritance (800AA8F4, after the original and the rule's gaps): with
// unified weapon upgrades the successor's weapons come up to the predecessor's unified
// level at once, free; then, always, a full-upgrade bonus whose weapon came over at or
// past the successor's original cap unlocks (upgrades::grant_bonuses).
void inherited(uint8_t* ram,uint32_t unit,uint32_t saved,int count);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
// Shared with the ability pages: one weapon row of `unit` (index into its weapon
// list) and the 14 weapon column labels, as the weapon screens show them.
nlohmann::json weapon_row_json(const uint8_t* ram,uint32_t unit,unsigned index);
nlohmann::json weapon_labels_json(const uint8_t* ram);
// Splits a row's menu "name" (格／射 + pure name + P／B／MAP) into "display_name" and
// "markers", given the weapon's pure name; also used by the title's Library.
void weapon_markers(nlohmann::json& row,const std::string& pure);
}
