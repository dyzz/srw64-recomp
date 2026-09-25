#pragma once
#include "json/json.hpp"
#include <filesystem>
// Modern move selection on the tactical map (docs/native/move-jump.md): while the player
// chooses where a unit moves, holding R lights up the farthest squares it can reach and
// the directions jump the cursor between them; A and B work as before.
namespace srw64::move_jump {
// Installs the game hooks; events go to `output`/move-jump-events.jsonl when it is set.
void configure(const std::filesystem::path& output);
// For the debug interface: when choosing last ran (VI), whether R is lighting squares,
// the cursor and farthest cells (gy * 31 + gx around the unit at 15, 15), jumps made.
nlohmann::json state();
}
