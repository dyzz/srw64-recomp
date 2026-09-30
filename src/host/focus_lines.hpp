#pragma once
// The blue focus lines the original draws behind a unit's battle picture in the
// intermission (801C6410's first half: sprite slot 0x12, mode 9, layout 0x3F6 on image
// 0x50F with palette 0x3F7, the palette animation 0x3F8 that makes them flicker). The
// native unit ability, upgrade and swap confirm pages build without 801C6410, drawing
// the picture themselves; this puts the lines back under the page at the picture's
// place, and the game animates and clears them as it does the original's.
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
}  // namespace srw64::focus_lines
