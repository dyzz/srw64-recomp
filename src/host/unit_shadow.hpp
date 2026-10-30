#pragma once
#include "guest_memory.hpp"
#include <cstdint>
#include <cstring>

// Map unit shadows (docs/native/unit-icon-facing.md, 阴影). The original gives every map
// unit a shadow, sprite sub 1 (resource 687, an ellipse in the bottom six rows of a 16x16
// tile), drawn by 801E21F8 on the ground square while the icon is lifted by the unit's height
// (slot +0xC); a unit on the ground has its shadow node hidden (node +1 bit 4, set where the
// icon is made, 801C61D8 and 801E04DC), and its head would cover it anyway.
//
// - A unit on the ground gets its own silhouette as a shadow, as in the later games: before
//   the icon's texture rectangle the same rectangle is drawn once more in black at 60% of the
//   icon's alpha, squashed to half its height against its bottom edge, so it hugs the
//   picture's outline. It samples the same texture, so original and HD images
//   (RT64 replaces the texture by hash) and the facing (unit_icon_facing.hpp, applied first)
//   all carry over; native_icon.cpp draws a MOD icon's from its own picture.
// - A flying unit keeps the original's ellipse at the original's 70%, moved up two pixels
//   towards its icon; with the HD images native_icon.cpp draws it as a soft ellipse.
namespace srw64::unit_shadow {
constexpr uint32_t kIconDrawer = 0x801E2C5C, kIconSub = 0, kShadowSub = 1;
constexpr uint32_t kSlotHeads = 0x800FFA70, kSlotSize = 0xC4;   // +0xC the height above the ground, f32
constexpr uint32_t kFirstUnitSlot = 0x42, kLastUnitSlot = 0x9B;
constexpr uint32_t kGroundAlpha = 0x99;   // of 255: 60%
constexpr int32_t kAirRaise = 2;          // pixels of a 16-pixel tile the flying unit's shadow moves up
// 801E2734's combiner: colour and alpha TEXEL0 x PRIM (1 cycle). The silhouette takes 0 as
// its colour inputs a (both cycles): black, the alpha unchanged.
constexpr uint32_t kIconCombine0 = 0xFC119623, kIconCombine1 = 0xFF2FFFFF;
constexpr uint32_t kShadowCombine0 = (kIconCombine0 & ~(0xFu << 20) & ~(0xFu << 5)) | 0xFu << 20 | 0xFu << 5;
constexpr uint32_t kInserted = 7 * 8;     // prim, combine, the rectangle (3), prim, combine

inline float height(const uint8_t* ram, uint32_t slot) {
    const uint32_t bits = guest::read(ram, kSlotHeads + slot * kSlotSize + 0xC, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

inline bool unit_slot(int32_t slot) { return slot >= int32_t(kFirstUnitSlot) && slot <= int32_t(kLastUnitSlot); }

// The texture rectangle (E4, E1, F1) in [begin, end), or 0.
inline uint32_t rectangle(const uint8_t* ram, uint32_t begin, uint32_t end) {
    for(uint32_t p = begin; p + 24 <= end; p += 8)
        if(guest::read(ram, p, 1) == 0xE4 && guest::read(ram, p + 8, 1) == 0xE1 && guest::read(ram, p + 16, 1) == 0xF1)return p;
    return 0;
}

// After the drawer wrote the icon or the shadow of sprite `slot` into the display list from
// `begin` to the address the word at `cursor` holds; inserting moves that word on.
inline void after_draw(uint8_t* ram, uint32_t function, int32_t slot, int32_t sub, uint32_t begin, uint32_t cursor) {
    if(function != kIconDrawer || !unit_slot(slot))return;
    const uint32_t end = guest::read(ram, cursor, 4);
    if(end <= begin || end - begin > 0x400)return;
    const uint32_t p = rectangle(ram, begin, end);
    if(!p)return;
    const uint32_t w0 = guest::read(ram, p, 4), w1 = guest::read(ram, p + 4, 4);
    int32_t lrx = (w0 >> 12) & 0xFFF, lry = w0 & 0xFFF, ulx = (w1 >> 12) & 0xFFF, uly = w1 & 0xFFF;
    const int32_t tall = lry - uly;
    if(lrx <= ulx || tall <= 0)return;
    if(sub == int32_t(kShadowSub)) {
        if(height(ram, uint32_t(slot)) == 0.f)return;   // hidden on the ground; drawn only for a flier
        const int32_t up = tall * kAirRaise / 16;
        if(uly < up)return;
        uly -= up; lry -= up;
        guest::write32(ram, p, (w0 & 0xFF000000u) | uint32_t(lrx) << 12 | uint32_t(lry));
        guest::write32(ram, p + 4, (w1 & 0xFF000000u) | uint32_t(ulx) << 12 | uint32_t(uly));
        return;
    }
    if(sub != int32_t(kIconSub) || height(ram, uint32_t(slot)) != 0.f)return;
    // The icon's colour and combiner, set before its rectangle.
    uint32_t prim0 = 0, prim1 = 0;
    bool combine = false;
    for(uint32_t q = begin; q < p; q += 8) {
        const uint32_t c0 = guest::read(ram, q, 4);
        if(c0 >> 24 == 0xFA) { prim0 = c0; prim1 = guest::read(ram, q + 4, 4); }
        if(c0 >> 24 == 0xFC)combine = c0 == kIconCombine0 && guest::read(ram, q + 4, 4) == kIconCombine1;
    }
    if(!prim0 || !combine)return;   // not the drawer's usual list: as drawn
    // The silhouette: the lower half of the tile (squashed to half its height, DtDy doubled),
    // ending with the icon. T stops a texel short of the tile's end: the last rows would
    // otherwise filter into the wrapped first row (the top of the picture) and draw a line
    // under the shadow.
    const int32_t top = uly + tall / 2, bottom = lry;
    const uint32_t st = guest::read(ram, p + 12, 4), steps = guest::read(ram, p + 20, 4);
    const int32_t texels = tall * int16_t(steps & 0xFFFF) / (4 * 1024);
    if(texels < 2)return;
    const int32_t dtdy = int16_t(steps & 0xFFFF) * 2 * (texels - 1) / texels;
    if(dtdy <= 0 || dtdy > 0x7FFF)return;
    const uint32_t alpha = (prim1 & 0xFF) * kGroundAlpha / 0xFF;
    const uint32_t words[kInserted / 4] = {
        prim0, (prim1 & 0xFFFFFF00u) | alpha,
        kShadowCombine0, kIconCombine1,
        (w0 & 0xFF000000u) | uint32_t(lrx) << 12 | uint32_t(bottom), (w1 & 0xFF000000u) | uint32_t(ulx) << 12 | uint32_t(top),
        0xE1000000u, st,
        0xF1000000u, (steps & 0xFFFF0000u) | uint32_t(dtdy),
        prim0, prim1,
        kIconCombine0, kIconCombine1,
    };
    // Make room in front of the icon's rectangle: the rest of the list moves on.
    for(uint32_t q = end; q > p; q -= 4)guest::write32(ram, q - 4 + kInserted, guest::read(ram, q - 4, 4));
    for(uint32_t i = 0; i < kInserted / 4; ++i)guest::write32(ram, p + i * 4, words[i]);
    guest::write32(ram, cursor, end + kInserted);
}
}
