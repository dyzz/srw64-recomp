#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
namespace srw64::intro {
void configure(const std::filesystem::path&);
uint16_t input(uint16_t);
void overlay_loaded(uint32_t rom, uint32_t ram, uint32_t size);
// Title/intro overlay state for the debug interface: loaded, title_major, skips
// and the last step's major/group/page/phase/active/scene.
nlohmann::json state();
// Title flow state of the intro overlay: 3 is the main menu. -1 when the overlay is not loaded.
int title_major();
// Ask the adapter to skip the current text sequence as if R+START were pressed.
void request_skip();
}
