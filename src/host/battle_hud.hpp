#pragma once
// The battle animation's windows placed for the whole picture (docs/design/deck-16x10.md
// §6, item 13). With the aspect "auto" the original's black bands above and below (a
// static display list in the battle overlay) are left out, and the windows move to the
// picture's edges: the two HP windows to the top corners, a speaker's portrait to the
// bottom corner on its side, the quote box between it and the other edge, widened to
// reach it. Everything else in the battle keeps its place. Game thread, except active()
// and the offsets, which read atomics.
#include <cstdint>

namespace srw64::battle_hud {
// Frame boundary: the black bands on or off.
void frame(uint8_t* rdram);
// The battle animation is on screen and the picture follows the screen.
bool active();
// After a draw callback of the battle in [begin, the cursor): moves what it drew.
void after_draw(uint8_t* rdram, int32_t cursor, uint32_t function, int32_t slot, int32_t sub, uint32_t begin);
// The HD portrait drawn in place of the original's tiles (native_portrait.cpp): the offset
// in original pixels for a portrait whose left edge is at x.
void portrait_offset(float x, float& dx, float& dy);
// An HD sprite drawn in place of a window's original tiles (native_sprite.cpp): the offset
// for one in sprite slot `slot` centred at x (the counter mark, slot 36).
void sprite_offset(int32_t slot, float x, float& dx, float& dy);
// Native text drawn over the original's glyphs (ui_text.cpp through native_sprite.cpp):
// the offset for text at x, y, the same as the window it is in.
void text_offset(float x, float y, float& dx, float& dy);
// The native battle quote (native_dialogue.cpp): where its box moved and how much wider it is.
struct Quote { float dx = 0, dy = 0, extra = 0; };
Quote quote();
}  // namespace srw64::battle_hud
