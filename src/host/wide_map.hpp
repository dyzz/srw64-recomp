#pragma once
// The tactical map shown wider than the original's 320 pixels (docs/design/deck-16x10.md
// §5). The game plays in a view Wv pixels wide: the picture's width (game_frame.hpp) as far
// as the map reaches, never below 320. Its screen-width constants become the original plus
// Wv - 320 (srw64_map_x, patched in by tools/recomp/toolchain/generate_cpu.py), and what it
// draws in map space is appended with RT64's extended commands so RT64 places it left
// aligned in the wide picture, the view centred when the map is narrower than the picture.
// HUD windows keep the original's 320 and stay centred. At 4:3, Wv is 320 and nothing
// changes. Game thread, except the shape functions (see there).
#include <cstdint>

namespace srw64::wide_map {
// Frame boundary: this frame's view from the picture's width and the map's; a camera out
// of the view's range is brought back into it.
void frame(uint8_t* rdram);
// Wv - 320 for this frame.
int32_t extra();
// The widened map is on screen: begin placed a draw within the last fifth of a second.
bool map_shown();
// Draws placed in the widened view since the start (for the debug status).
uint64_t placed_draws();
// The map width (80172EC4) and X scroll (8010F5D4) the last frame boundary read.
int32_t map_width_seen();
int32_t scroll_seen();
// Before a draw in map space whose display list cursor (a Gfx**) is `cursor`: appends the
// commands that widen the scissor and align rectangles to the view, and returns true if it
// did; after the draw, end() puts both back.
bool begin(uint8_t* rdram, int32_t cursor);
void end(uint8_t* rdram, int32_t cursor);
// Where the view's x = 0 falls in the picture, in original pixels, while it is shown wide
// (for the HD map, native_map.cpp); negative when the view is the centred 320.
float view_offset();

// What a frame fills of the picture; graphics.cpp paints the rest black and keeps the
// battle's 3D in the original's 4:3. By default a frame fills the whole picture (the world
// map, the title and the prologue widen by themselves); art still 4:3 marks the frame
// original; a widened map leaves the band its view covers. The game thread's draws decide;
// send_dl takes it once per display list and keys it by workload for the present hook.
struct Shape {
    enum class Fill : uint8_t { whole, original, view } fill = Fill::original;
    float left = 0, right = 0;   // Fill::view: the view in picture pixels
    bool battle = false;         // the battle animation
};
// This frame draws 4:3 art (a background not drawn wider), or is the battle animation.
void mark_original();
void mark_battle();
// The battle animation ran within the last fifth of a second (its step runs every frame).
bool battle_shown();
// The battle's sky (80095974, a 320-pixel picture that wraps) is drawn again one period to
// each side, in the whole picture's scissor: open_sides before the copies, offset_rects
// before each (original pixels; 0 for the original itself), close_sides after.
// open_rows narrows that scissor to rows [top, bottom) (original pixels), and
// offset_rects can move the rectangles down (`down`, negative up) as well.
void open_sides(uint8_t* rdram, int32_t cursor);
void open_rows(uint8_t* rdram, int32_t cursor, float top, float bottom);
void offset_rects(uint8_t* rdram, int32_t cursor, float pixels, float down = 0);
void close_sides(uint8_t* rdram, int32_t cursor);
// Rectangles from here on stretch across the whole picture (RT64's rectangle aspect
// STRETCH), or back to RT64's own rule: art that has no wider version but reads the same
// stretched (the focus lines).
void stretch_rects(uint8_t* rdram, int32_t cursor, bool on);
// The shared screen wipe (80099814's render callback 80099508: 240 one-pixel black lines
// from x 0/1 to 319/320) across the whole picture: wipe_begin before it runs; wipe_end
// scales the lines it wrote in [begin, the cursor) to the picture's width, edges to its edges.
bool wipe_begin(uint8_t* rdram, int32_t cursor);
void wipe_end(uint8_t* rdram, int32_t cursor, uint32_t begin);
Shape take_shape();
void queue_shape(uint64_t workload, Shape shape);
Shape frame_shape(uint64_t workload);
}  // namespace srw64::wide_map

extern "C" {
// A screen-width constant of the tactical map: the original plus halves/2 of Wv - 320
// (2 for a right edge, 1 for a centre).
int32_t srw64_map_x(int32_t original, int32_t halves);
// The same for a float loaded by its upper 16 bits (lui): the bits of the new value.
int32_t srw64_map_x_float(uint32_t bits, int32_t halves);
}
