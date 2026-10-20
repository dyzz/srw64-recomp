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
// A learning level on the pilot page (enemy_pilot_page.hpp): the text engine's label in
// pool slot `slot`, printed at (x, y), is drawn as `shown` (small and yellow) beside its
// spirit's name while the slot still prints `text` there (the engine's ASCII has digits but
// no letters, so "Lv" is the host's). clear_small() drops the marks of a page.
void mark_small(unsigned slot, int x, int y, const std::string& text, const std::string& shown);
// A figure the host adds to an original page with a label of its own (pilot_kills.hpp):
// the number `text` that pool slot `slot` prints at (x, y) is drawn as the reading
// language's UI string `key`, a space and the number, in the label face from (x, y),
// squeezed into `width`. clear_small() drops it too.
void mark_label(unsigned slot, int x, int y, const std::string& text, const std::string& key, int width);
void clear_small();
// A label of the host's own among the game's (the blank-tile menu's eighth row, map_menu.hpp):
// drawn in the labels' face at x, y (original pixels) within width, as long as it is set.
void set_extra(const std::string& id, int x, int y, const std::string& key, int width);
void clear_extra(const std::string& id);
// Damage past 65535 (overkill.hpp): the game draws its figure as 65535, and the next map
// figure (banner false) or battle banner number (true) that reads 65535 is drawn as `real`
// instead, in as many cells as it takes (the map's to the left, the banner's to the right).
// 0 drops it.
void real_figure(bool banner, uint64_t real);
}
