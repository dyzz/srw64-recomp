#pragma once
// The blue focus lines the original draws behind a unit's battle picture in the
// intermission (801C6410's first half: sprite slot 0x12, mode 9, layout 0x3F6 on image
// 0x50F with palette 0x3F7, the palette animation 0x3F8 that makes them flicker). The
// native unit ability, upgrade and swap confirm pages build without 801C6410, drawing
// the picture themselves; this puts the lines back under the page at the picture's
// place, and the game animates and clears them as it does the original's. The page
// leaves that panel clear and the game fills it first (panel), so the lines lie over
// the panel's colour as in the original instead of dimmed under it.
#include "guest_memory.hpp"
#include "funcs.h"
#include <cstring>

namespace srw64::focus_lines {
inline void show(uint8_t* ram, const recomp_context* ctx, float x = 176, float y = 8) {
    const auto call = [&](void (*function)(uint8_t*, recomp_context*), uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                          uint32_t s10, uint32_t s14, uint32_t s18, uint32_t s1C) {
        recomp_context c = *ctx;
        const uint32_t sp = uint32_t(ctx->r29) - 0x200;
        c.r29 = int32_t(sp);
        c.r4 = int32_t(a0); c.r5 = int32_t(a1); c.r6 = int32_t(a2); c.r7 = int32_t(a3);
        guest::write32(ram, sp + 0x10, s10); guest::write32(ram, sp + 0x14, s14);
        guest::write32(ram, sp + 0x18, s18); guest::write32(ram, sp + 0x1C, s1C);
        function(ram, &c);
    };
    call(resident_func_80098158, 0x12, 0, 9, 0x8D, 0x3F6, 0x50F, 0x3F7, 0);
    call(resident_func_80099C88, 0x12, 0, 0x3F8, 1, 0, 0, 0, 0);
    // The slot's origin (800FFA70 + 0x12 * 0xC4, +4 / +8), as 801C6410 sets it.
    uint32_t bits;
    std::memcpy(&bits, &x, 4); guest::write32(ram, 0x8010083C, bits);
    std::memcpy(&bits, &y, 4); guest::write32(ram, 0x80100840, bits);
}
// The lines' sprite (slot 0x12, sub 0, layout 0x3F6 in 800FFA70's sprite records).
inline bool is_lines(const uint8_t* ram, uint32_t slot, uint32_t sub) {
    return slot == 0x12 && sub == 0 && guest::read(ram, 0x800FFA70 + 0x12 * 0xC4 + 0x3C + 4, 2) == 0x3F6;
}
// Before 800945D4 draws the lines: the native panel's fill (frontend.cpp .im-panel,
// #0a0e3c at 0xC8) over the picture's panel (176,8)-(302,132), at the display list
// cursor `cursor` (a Gfx**). One cycle, primitive colour, translucent blend; the lines'
// drawer sets its own modes after.
inline void panel(uint8_t* ram, int32_t cursor) {
    const uint32_t words[] = {
        0xE7000000, 0,                        // pipe sync
        0xEF000000, 0x00504240,               // one cycle; G_RM_XLU_SURF, G_RM_XLU_SURF2
        0xFCFFFFFF, 0xFFFDF6FB,               // G_CC_PRIMITIVE, G_CC_PRIMITIVE
        0xFA000000, 0x0A0E3CC8,               // primitive colour
        0xF6000000 | (303 << 2) << 12 | (133 << 2), (176 << 2) << 12 | (8 << 2),
        0xE7000000, 0};
    const uint32_t list = guest::read(ram, uint32_t(cursor), 4);
    uint32_t at = list;
    for (const uint32_t word : words) { guest::write32(ram, at, word); at += 4; }
    guest::write32(ram, uint32_t(cursor), list + uint32_t(sizeof(words)));
}
}  // namespace srw64::focus_lines
