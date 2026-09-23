#pragma once
#include "intermission_menu.hpp"
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

// The native インターミッション main menu (docs/native/native-intermission-menu.md).
// The original builds its four panels from screen layout 0x69 / 0x8D and moves a
// highlight sprite; here the build draws nothing and the shared UI shows the same
// panels. What a choice does stays with the original step (801CE19C): the adapter
// writes the cursor and feeds it one A edge.
namespace srw64::intermission_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread: "move:N", "choose:N", "swap-move:N", "swap:N", "swap-close" for page `serial`.
void answer(uint64_t serial,const std::string& action);
// While the page is up, and until the keys that closed it are released, the game
// sees no buttons, except the held pair the intermission's soft reset reads.
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
}
