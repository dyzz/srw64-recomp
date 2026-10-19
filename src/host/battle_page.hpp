#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
namespace srw64::battle_page {
void configure(const std::filesystem::path&);
nlohmann::json state();
// Every frame (host.cpp frame_start): ends a page the game left without it (a load, the
// title), or one whose step stopped running.
void frame(const uint8_t* ram);
// Ends the page now: the quick load, as the original takes its Z+START (quick_load.cpp).
void abandon(const char* why);
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t);
// battle_ui "original": while the original confirmation is up (forced battles too), its
// text is drawn as in the original image mode (ui_text.cpp) and its window frames,
// scenes 1196/1197, keep the ROM image (native_map.cpp).
bool original_screen();
}
