#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>

// The title screen's pages behind the ring menu (docs/native/native-title-menus.md):
// オプション (サウンド stereo/mono, サウンドセレクト, カラオケモード) and the two song lists.
// The page draws; the original state functions keep the logic (unlocks, scrolling,
// playing a song, the カラオケ hand-off) and receive the page's choices as buttons.
// configure() also routes the title's ロード screens to save_page.
namespace srw64::title_page {
void configure(const std::filesystem::path& directory);
nlohmann::json state();
// Window thread, for page `serial`: options "move:N", "choose", "back"; song lists
// "move:+1" / "move:-1", "jump:N", "choose", "back", "prev" / "next" (play the
// neighbour; サウンドセレクト only), "exit".
void answer(uint64_t serial, const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
void overlay_loaded(uint32_t rom, uint32_t ram, uint32_t size);
}
