#pragma once
#include "guest_memory.hpp"
#include "unit_icon_facing_table.hpp"
#include <cstdint>

// Map unit icons face the side they fight for: the player's units and friendly third
// parties look left, enemies and hostile third parties look right (docs/native/unit-icon-facing.md).
// The original draws each icon as one 16x16 G_TEXRECT (801E2C5C -> 801E2734), which no
// sprite mirror bit reaches, so the rectangle is mirrored after it is written: its S starts
// at the far edge and DsDx runs backwards. The HD icons replace the texture by hash and
// take the same coordinates, so original and HD images both turn; the shadow (sub 1) and
// the acted grey (a TLUT swap in the same draw) are untouched.
namespace srw64::unit_icon_facing {
constexpr uint32_t kIconDrawer = 0x801E2C5C;
constexpr uint32_t kSubRecords = 0x800FFAA4, kSlotSize = 0xC4, kSubSize = 0x30;   // +0x14 atlas, +0x16 palette handle
constexpr uint32_t kHandles = 0x80160340, kHandleSize = 20;                     // +2 the ROM resource
constexpr uint32_t kRoster = 0x8015E100, kSideStride = 0x258, kSlotStride = 0x14;
constexpr uint32_t kFirstSlot[3] = {0x42, 0x60, 0x7E}, kSlots = 30;             // 801E514C: side and slot to handle
constexpr uint8_t kFriendly = 0x20;   // third party, roster +0xB: attacks only the enemy (80202188; 4 in the deployment)
constexpr uint32_t kPlayerPalette = 1010, kThirdPalette = 1012;

inline uint32_t resource(const uint8_t* ram, uint32_t handle_field) {
    const int32_t handle = int16_t(guest::read(ram, handle_field, 2));
    return handle < 0 ? 0 : guest::read(ram, kHandles + uint32_t(handle) * kHandleSize + 2, 2);
}

// Whether the unit drawn in this sprite slot should look left: by its side, read from the
// slot (unit slots 0x42-0x9B), else (the copies in slots 0x3D and 0x9C) from its palette.
inline bool looks_left(const uint8_t* ram, uint32_t slot, uint32_t palette) {
    for(uint32_t side = 0; side < 3; ++side) {
        if(slot < kFirstSlot[side] || slot >= kFirstSlot[side] + kSlots)continue;
        if(side == 0)return true;
        if(side == 1)return false;
        const uint32_t flags = kRoster + 2 * kSideStride + (slot - kFirstSlot[2]) * kSlotStride + 0xB;
        return guest::read(ram, flags, 1) & kFriendly;
    }
    return palette == kPlayerPalette;
}

// After the icon drawer wrote [begin, end): mirror its texture rectangle when the icon
// looks the other way from its side.
inline void after_draw(uint8_t* ram, uint32_t function, int32_t slot, int32_t sub, uint32_t begin, uint32_t end) {
    if(function != kIconDrawer || sub != 0 || slot < 0 || end <= begin || end - begin > 0x400)return;
    const uint32_t record = kSubRecords + uint32_t(slot) * kSlotSize;
    const uint32_t icon = resource(ram, record + 0x14);
    if(icon < kFirstIcon || icon > kLastIcon)return;
    const char facing = kFacing[icon - kFirstIcon];
    if(facing == 'F')return;
    if((facing == 'L') == looks_left(ram, uint32_t(slot), resource(ram, record + 0x16)))return;
    for(uint32_t p = begin; p + 24 <= end; p += 8) {
        const uint32_t w0 = guest::read(ram, p, 4);
        if(w0 >> 24 != 0xE4 || guest::read(ram, p + 8, 1) != 0xE1 || guest::read(ram, p + 16, 1) != 0xF1)continue;
        const uint32_t w1 = guest::read(ram, p + 4, 4), st = guest::read(ram, p + 12, 4), steps = guest::read(ram, p + 20, 4);
        const int32_t width = int32_t((w0 >> 12) & 0xFFF) - int32_t((w1 >> 12) & 0xFFF);   // 10.2 screen px
        const int32_t dsdx = int16_t(steps >> 16);                                           // s5.10
        if(width <= 0 || dsdx <= 0)return;   // already mirrored, or not the shape expected
        // S (s10.5) to the far edge: the span in texels less a 32nd, so the first pixel
        // samples the last texel and the wrap never reaches the first one.
        const int32_t span = width * dsdx / (4 * 1024);
        const uint32_t s = ((st >> 16) + uint32_t(span * 32 - 1)) & 0xFFFF;
        guest::write32(ram, p + 12, (s << 16) | (st & 0xFFFF));
        guest::write32(ram, p + 20, (uint32_t(uint16_t(int16_t(-dsdx))) << 16) | (steps & 0xFFFF));
        return;
    }
}
}
