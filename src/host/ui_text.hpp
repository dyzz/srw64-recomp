#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
// Original UI text drawn natively (docs/native/native-ui-text.md): the resident text
// engine's single-line labels (pool 8015CB00) and numbers (pool 801613E0) on every
// screen that has no native page yet, tactical map and battle included. After each of
// the engine's two passes (8008DC40, 8008EB5C) the host checks the display list against
// the pools, blanks the ROM-font glyphs and draws the same text in one native draw:
// a label that still shows its text record in the reading language, anything else
// (entered names, sprintf numbers) glyph by glyph in its original cells, HD.
// The dialogue reader keeps its own boxes and speaker names. Game host only.
namespace srw64::ui_text {
void configure(const std::filesystem::path& output);
// Debug status: counters and the labels of the last passes.
nlohmann::json state();
// Whether the original's numbers and labels are drawn natively now (what the map's MISS
// figure needs, map_miss.hpp).
bool native_numbers();
}
