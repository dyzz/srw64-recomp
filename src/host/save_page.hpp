#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

// The native データセーブ screens (docs/native/native-save-screens.md): the medium
// choice (intermission screen 1, ROMカートリッジ／コントローラパック with the
// "データを調べています。" pause) and the two-slot page (9) with its overwrite window
// and the Controller Pak messages. The page owns the state machine and only calls the
// original slot reader and the SRAM / Controller Pak write routines.
namespace srw64::save_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread, for page `serial`: choice "move:N" (0 ROM / 1 pak), "choose", "back";
// slots "move:N", "choose", "back"; overwrite window "move:N" (0 はい / 1 いいえ),
// "choose", "cancel"; error "choose" (recheck / repair), "back".
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
