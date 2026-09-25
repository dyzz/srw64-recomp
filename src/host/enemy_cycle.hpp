#pragma once
#include "json/json.hpp"
#include <filesystem>
// Modern enemy cycling on the tactical map (docs/native/enemy-cycle.md): on the idle map a
// controller's L2 / R2 put the cursor on the previous / next enemy unit, the way L / R
// already step through the player's units in the original.
namespace srw64::enemy_cycle {
// Installs the game hook; events go to `output`/enemy-cycle-events.jsonl when it is set.
void configure(const std::filesystem::path& output);
// For the debug interface: the last step (VI, direction, side, slot, cursor) and the count.
nlohmann::json state();
}
