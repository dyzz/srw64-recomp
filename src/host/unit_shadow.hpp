#pragma once
#include "guest_memory.hpp"
#include <cstdint>
#include <cstring>

// Ground units show their shadow too (docs/native/unit-icon-facing.md, 阴影). The original
// gives every map unit a shadow, sprite sub 1 (resource 687, an ellipse in the bottom six rows
// of a 16x16 tile), drawn by 801E21F8 on the ground square while the icon is lifted by the
// unit's height (slot +0xC); a unit on the ground has its shadow node hidden (node +1 bit 4,
// set where the icon is made, 801C61D8 and 801E04DC, and cleared when it flies). frame()
// shows it again for every unit on the ground whose icon shows, and after_draw() moves that
// shadow's texture rectangle down two pixels and widens it by 30% about its centre, so it
// shows under the head as in the later games. Flying units keep the original's shadow. With
// the HD images native_icon.cpp draws every unit shadow as a soft ellipse made in code.
namespace srw64::unit_shadow {
constexpr uint32_t kIconDrawer = 0x801E2C5C, kShadowSub = 1;
constexpr uint32_t kSlotHeads = 0x800FFA70, kSlotSize = 0xC4;   // +0xC the height above the ground, f32
constexpr uint32_t kSubRecords = 0x800FFAA4, kSubSize = 0x30;    // +0 in use, +4 the node, +0xE the resource
constexpr uint32_t kShadowResource = 687;
constexpr uint8_t kNodeHidden = 0x04;                           // node +1
constexpr uint32_t kFirstUnitSlot = 0x42, kLastUnitSlot = 0x9B;
constexpr int32_t kWiden = 30;         // percent of the width added, half on each side
constexpr int32_t kDown = 2;           // pixels of a 16-pixel tile

inline float height(const uint8_t* ram, uint32_t slot) {
    const uint32_t bits = guest::read(ram, kSlotHeads + slot * kSlotSize + 0xC, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// Game thread, at the frame's start: show the shadow of every unit on the ground whose icon
// shows. Only sprites the map icon drawer paints with the shadow resource are touched, so the
// battle's use of these slots is left alone.
inline void frame(uint8_t* ram) {
    for(uint32_t slot = kFirstUnitSlot; slot <= kLastUnitSlot; ++slot) {
        const uint32_t icon = kSubRecords + slot * kSlotSize, shadow = icon + kShadowSub * kSubSize;
        if(guest::read(ram, icon, 1) != 1 || guest::read(ram, shadow, 1) != 1)continue;
        if(guest::read(ram, shadow + 0xE, 2) != kShadowResource)continue;
        const uint32_t icon_node = guest::read(ram, icon + 4, 4), shadow_node = guest::read(ram, shadow + 4, 4);
        if(!guest::valid(icon_node, 0x10) || !guest::valid(shadow_node, 0x10))continue;
        if(guest::read(ram, shadow_node + 0xC, 4) != kIconDrawer || guest::read(ram, icon_node + 0xC, 4) != kIconDrawer)continue;
        if(guest::read(ram, icon_node + 1, 1) & kNodeHidden || height(ram, slot) != 0.f)continue;
        const uint8_t flags = uint8_t(guest::read(ram, shadow_node + 1, 1));
        if(flags & kNodeHidden)guest::write8(ram, shadow_node + 1, uint8_t(flags & ~kNodeHidden));
    }
}

// After the drawer wrote a shadow into [begin, end).
inline void after_draw(uint8_t* ram, uint32_t function, int32_t slot, int32_t sub, uint32_t begin, uint32_t end) {
    if(function != kIconDrawer || sub != int32_t(kShadowSub) || slot < int32_t(kFirstUnitSlot) || slot > int32_t(kLastUnitSlot))return;
    if(end <= begin || end - begin > 0x400 || height(ram, uint32_t(slot)) != 0.f)return;
    for(uint32_t p = begin; p + 24 <= end; p += 8) {
        const uint32_t w0 = guest::read(ram, p, 4);
        if(w0 >> 24 != 0xE4 || guest::read(ram, p + 8, 1) != 0xE1 || guest::read(ram, p + 16, 1) != 0xF1)continue;
        const uint32_t w1 = guest::read(ram, p + 4, 4), steps = guest::read(ram, p + 20, 4);
        // Screen corners in 10.2: lower right in w0, upper left in w1.
        int32_t lrx = (w0 >> 12) & 0xFFF, lry = w0 & 0xFFF, ulx = (w1 >> 12) & 0xFFF, uly = w1 & 0xFFF;
        const int32_t width = lrx - ulx, tall = lry - uly, dsdx = int16_t(steps >> 16);
        if(width <= 0 || tall <= 0 || dsdx <= 0)return;
        const int32_t side = width * kWiden / 200;               // added on each side, 10.2
        const int32_t down = tall * kDown / 16;                  // the same two pixels at any map zoom
        if(ulx < side || lrx + side > 0xFFF || lry + down > 0xFFF)return;   // at the screen's edge: as drawn
        ulx -= side; lrx += side; uly += down; lry += down;
        // The same 16 texels across the wider rectangle: DsDx shrinks by the same factor.
        const int32_t widened = dsdx * width / (width + 2 * side);
        guest::write32(ram, p, (w0 & 0xFF000000u) | (uint32_t(lrx) << 12) | uint32_t(lry));
        guest::write32(ram, p + 4, (w1 & 0xFF000000u) | (uint32_t(ulx) << 12) | uint32_t(uly));
        guest::write32(ram, p + 20, (uint32_t(uint16_t(int16_t(widened))) << 16) | (steps & 0xFFFF));
        return;
    }
}
}
