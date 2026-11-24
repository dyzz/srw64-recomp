#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include "recomp.h"

// Autosaves (docs/design/save-slots-autosave.md §2.4), only with the save library:
// in the original's slot format when the intermission is entered after a map and
// when 次のマップへ leaves it; in its suspend format once per player turn, when the map
// first stands idle with no event running. The random numbers of a turn autosave are
// kept beside it and put back after the map restarts from it.
namespace srw64::autosave {
void configure(const std::filesystem::path& directory);
// The title's ロード chose a turn autosave: its random numbers wait for the restore.
void prepare_turn_load(const std::filesystem::path& record);

// Android sends the app to the background (any thread). At the next save point (the map
// standing idle, the intermission's menu) the game is saved apart from the autosaves and
// auto/resume.json notes it; until then the note points at the newest autosave of this
// session. Back in the foreground the note goes. A note found at start-up means the system
// closed the game in the background: the title's ring offers to go back to it.
void set_background(bool background);
// True while the game is to be held (the VI thread waits): in the background and saved, or no
// save point came in time.
bool hold();
// The renderer's part (graphics.cpp, Android): once saved, it frees what it can, then says so.
bool trim_wanted();
void trimmed();
// Game thread, each frame (80085F30): the background note and the title's offer.
void frame(uint8_t* ram,recomp_context* ctx);
struct Offer { bool map; unsigned turn; unsigned funds; std::string time; };
std::optional<Offer> offer();
// The player's answer: go back to it (on the ring) or leave it.
void answer(bool go_back);
}
