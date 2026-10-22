#pragma once
// Quick load of the map's suspend save (docs/design/quick-load-and-unified-weapons.md §1):
// the original reloads it when Z is held, START pressed and START still held as the fade
// ends (801DFBD0, 801C97E4), with no binding for Z here. A confirmed request presses that
// combination for the player: the fade, the check of the card (80093610), the switch to
// mode 0x11 and the restore are all the game's own, and the store's redirection applies.
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>

namespace srw64::quick_load {
// The suspend record on the card: whether there is one, and its progress block (+0x26C).
struct Saved {
    bool exists{};
    bool this_stage{};   // its stage title is the one being played
    unsigned turn{};     // as the map shows it, from 1
    uint32_t funds{};
};

void configure(const std::filesystem::path& run_directory);
// Any thread: the game stands where the combination works now (a map with its UI up, or
// a battle animation that can be aborted), and the card's suspend record.
bool usable();
Saved saved();
// Any thread: load it as soon as the game takes the combination (confirmed by the player).
void request();
// Back to the title screen: the same combination with START let go as soon as the game
// takes it, so the fade ends at the BANPRESTO logo and the title instead of a load. The
// original tests it on the tactical map (with its UI up), the world map, the intermission
// and in a battle animation; usable_title() says whether the game stands in one of them.
bool usable_title();
void request_title();
// Game thread, every frame boundary (80085F30).
void frame(uint8_t* ram);
// The input callback (host.cpp get_input), after every other filter: while a load runs,
// the combination and nothing of the player's.
uint32_t filter_input(uint32_t buttons);
nlohmann::json state();
}
