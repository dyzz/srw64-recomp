#pragma once
#include <cstdint>
#include <optional>
#include <vector>
// Original menus made wider, so translated commands fit at the size of the rest:
// the tactical map's unit commands (変形 → "Transform") and the intermission menu
// (武器改造 → "Upgrade Weapons"). Their widths are constants of the original
// (docs/native/native-ui-text.md §6), so each part is widened where it is drawn:
// - the frame (a grid scene): one column of cells drawn wider, the cells right of it
//   moved over, in the display list and in the HD frame rom_art.cpp makes;
// - the dark panel and the cursor bar: the FILLRECT their callbacks write;
// - where the menu may open (unit menus) and the intermission panel: RAM data.
// ui_text.cpp lets translated labels run to the widened window's inner edge.
// Two are also a row taller for the 返回标题画面 row the host adds: the map's blank-tile
// menu (map_menu.hpp) and the intermission menu (intermission_row.hpp), whose chapter
// box under it moves down the same 16 pixels.
namespace srw64::menu_widen {
// Cells of a grid scene to widen: in rows top..bottom (scene pixels, cell corners), the
// cell at x = stretch is drawn 16 + delta wide and those at shift <= x < shift_end move
// right by delta.
struct Columns { int top = 0, bottom = 0, stretch = 0, shift = 0, shift_end = 0, delta = 0; };
std::optional<Columns> columns(uint16_t scene);
// Cells of a grid scene to make taller: in columns left..right (as the ROM places them), the
// cell at y = stretch is drawn 16 + delta tall and those at shift <= y < shift_end move down
// by delta.
struct Rows { int left = 0, right = 0, stretch = 0, shift = 0, shift_end = 0, delta = 0; };
std::optional<Rows> rows(uint16_t scene);       // the scenes drawn taller
std::optional<Rows> rows_spec(uint16_t scene);  // the scene's stretch whenever it is drawn tall

// Game thread, each frame: the RAM patches (they check the original values first).
void frame_start(uint8_t* rdram);
// Game thread, a mode 9 grid scene drawn at screen = scene + origin: a widened scene's
// rectangles are stretched (before the HD frame rewrite reads them).
void frame_drawn(uint8_t* rdram, uint32_t dl_begin, uint32_t dl_end, uint16_t scene, int origin_x, int origin_y);
// Game thread, after a render callback wrote [dl_begin, dl_end): the panel and bar fills.
void callback_drawn(uint8_t* rdram, uint32_t function, uint32_t dl_begin, uint32_t dl_end);

// The widened windows drawn lately (screen pixels): x0, y0, inner right edge, y1.
struct Window { float x0, y0, x1, y1; };
std::vector<Window> windows();
}
