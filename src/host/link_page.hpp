#pragma once
#include "json/json.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The native series page in front of the リンク screen (docs/gameplay/link-battler.md),
// laid out like the protagonist selection page. Choosing リンク in the intermission menu
// opens it instead of reading a Game Boy cartridge: the player ticks the series to
// link, and continuing hands the choice to the virtual cartridge (link_battler.hpp)
// and runs the original screen; 戻る goes back to the menu as B does there.
namespace srw64::link_page {
struct Request {
    uint64_t serial{};
    bool visible{};                   // up and waiting for an answer
    std::array<bool,3> joined{};      // series order of link::series
    std::array<bool,3> scheduled{};
    // Portrait paths per series, the lead pilot first (name-entry assets, original art).
    std::array<std::vector<std::string>,3> portraits;
};
void configure(const std::filesystem::path& directory);
Request request();
// Window thread: the player's answer for page `serial`.
void answer(uint64_t serial,unsigned selection,bool confirm);
// While the page is up, and until the keys that closed it are released, the game
// sees no buttons.
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
nlohmann::json state();
// Platform backend: main/window thread only, never reads guest memory.
void window_init(void* cocoa_window,const std::filesystem::path&);
void window_update();
void window_shutdown();
}
