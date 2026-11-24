#pragma once
#include "json/json.hpp"
#include <utility>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <string>

// The native のりかえ screens (docs/native/native-swap-screens.md): the pilot list
// (intermission screen 6), the machine list for that pilot (16) and the confirm page
// (17); the fairy list (20) and its pilot list with the confirm window (21). The
// original set-ups build their candidate lists without drawing; the swap itself is
// one A edge fed to the original step, so roster, parts and sub-pilot handling
// stay original.
namespace srw64::swap_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// The same without copying it, and a revision that moves only when it changed (the UI's
// frames, page_snapshot.hpp).
std::pair<std::shared_ptr<const nlohmann::json>,uint64_t> shared_state();
// Window thread, for page `serial`: lists "move:R", "page:P", "choose", "back";
// confirm "move:R" (0 はい / 1 いいえ), "choose", "back"; fairy targets also
// "cancel" (close the window).
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
