#pragma once
// The tactical map with the battle animation off: a "MISS" over the unit an attack missed,
// and the result presentation played faster.
//
// With the animation off (8015DDA8 & 4) 801D4E6C has 801FCA78 queue the exchange's rounds
// through 801FB8B4 (count 802277E8, target handle 802277E9[], flags 80227825[], damage
// 80227862[]) and the map presents them in state 0x4B (801D92B0, sub-states in table
// 80217D4C): 3 moves the cursor to the target, plays the hit effect and sound; 4 opens the
// HP window; 5 drains the HP; 6 floats the figure (801FCF00) and closes it (8009DB8C);
// 7-10 the next round or the end. The original drops a round that missed (801FB8B4: no
// damage and a reaction other than a barrier or ダミー: 0x16 evaded, 6-0xC 分身,
// 0x12/0x13 切り払い), so a miss shows nothing, and with no round at all 0x4B is skipped
// (user, 2026-10-10: show it, in the reading language, as the damage is shown).
//
// Here such a round is queued anyway with no damage and remembered as a miss. When its
// sub-state 3 is due the host does what that sub-state does before it looks at the damage
// (the cursor and camera on the target), places the figure as sub-state 5 does
// (801E3AE4 for the place, 801FCC6C), turns its cells into blank-and-letter cells and goes
// to sub-state 6, which floats and closes it as it does the damage; ui_text draws the
// letters. An exchange of misses only now ends as the animated battle does (state 24
// sub-state 9: experience and funds, nothing for a miss). Only with the animation off
// (not the presentation that replays an aborted animation) and with the native text on:
// the original's own digits would read 0000.
#include "guest_memory.hpp"
#include "funcs.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace srw64::map_miss {
inline constexpr uint32_t animation_flags = 0x8015DDA8, animation_off = 4;
inline constexpr uint32_t map_state = 0x80172EB0, map_sub = 0x80172EB2;
inline constexpr uint32_t frame = 0x8021F464, round_index = 0x8022731C;
inline constexpr uint32_t round_count = 0x802277E8, round_cells = 0x802277E9, round_flags = 0x80227825, round_damage = 0x80227862;
inline constexpr uint32_t current_cell = 0x80227BC6, current_damage = 0x80227BC8;
inline constexpr uint32_t cursor = 0x80102308, cell_table = 0x800FFA74, cell_stride = 0xC4;
inline constexpr uint32_t figure_x = 0x80101DAC, figure_y = 0x80101DB0;
inline constexpr uint32_t figure_cells = 0x80228530, figure_shown = 0x80228536;
inline constexpr uint8_t resolving = 0x4B;
inline constexpr unsigned capacity = 16;

struct State {
    bool collecting = false;   // inside an animation-off 801FCA78
    bool replaying = false;    // the aborted animation's replay (battle_animation_probe.hpp)
    std::array<bool, capacity> miss{};
    std::atomic<bool> showing{};   // a MISS figure is up (ui_text draws it)
};
inline State& state() { static State s; return s; }

inline bool missed(int8_t reaction) {
    return reaction == 0x16 || (reaction >= 6 && reaction <= 0xC) || reaction == 0x12 || reaction == 0x13;
}

// 801FCA78, around the original: collect misses only for the animation-off presentation.
inline void queue_begin(const uint8_t* ram, bool native_text) {
    auto& s = state();
    s.miss.fill(false);
    s.collecting = native_text && !s.replaying && (guest::read(ram, animation_flags, 1) & animation_off);
}
inline void queue_end() { state().collecting = false; }

// 801FB8B4(handle, damage, flags, reaction), after the original: a dropped miss is queued
// with no damage.
inline void round_pushed(uint8_t* ram, uint32_t handle, uint32_t damage, uint32_t reaction, unsigned count_before) {
    auto& s = state();
    const unsigned count = guest::read(ram, round_count, 1);
    if (!s.collecting || count != count_before || (damage & 0xFFFF) || !missed(int8_t(reaction)) || count >= capacity) return;
    guest::write8(ram, round_cells + count, uint8_t(handle));
    guest::write8(ram, round_flags + count, 0);
    guest::write16(ram, round_damage + 2 * count, 0);
    guest::write8(ram, round_count, uint8_t(count + 1));
    s.miss[count] = true;
}

inline uint32_t call(uint8_t* ram, recomp_context* ctx, uint32_t function, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    auto c = *ctx;
    c.r29 = int32_t(uint32_t(ctx->r29) - 0x200);
    c.r4 = int32_t(a0); c.r5 = int32_t(a1); c.r6 = int32_t(a2); c.r7 = int32_t(a3);
    LOOKUP_FUNC(function)(ram, &c);
    return uint32_t(c.r2);
}
inline float read_float(const uint8_t* ram, uint32_t address) {
    const uint32_t bits = guest::read(ram, address, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
// The game's float-to-unsigned conversion (trunc, with the 2^31 fold) for screen places.
inline uint32_t to_word(float value) {
    return value >= 2147483648.f ? uint32_t(int32_t(value - 2147483648.f)) | 0x80000000u : uint32_t(int32_t(value));
}

// Map overlay, each frame before the dispatcher (801DFBD0).
inline void map_step(uint8_t* ram, recomp_context* ctx) {
    auto& s = state();
    const uint8_t st = uint8_t(guest::read(ram, map_state, 1)), sub = uint8_t(guest::read(ram, map_sub, 1));
    if (st != resolving) { s.showing = false; return; }
    if (sub != 6) s.showing = false;
    const uint32_t counter = guest::read(ram, frame, 4);
    const uint32_t index = guest::read(ram, round_index, 4);

    // A miss round's sub-state 3, on the frame it acts (it compares the old counter to 0x14).
    if (sub == 3 && counter == 0x14 && index < capacity && s.miss[index]) {
        s.miss[index] = false;
        const uint8_t handle = uint8_t(guest::read(ram, round_cells + index, 1));
        const uint32_t cell = cell_table + handle * cell_stride;
        guest::write32(ram, frame, 0);
        guest::write16(ram, current_damage, 0);
        guest::write16(ram, current_cell, handle);
        for (uint32_t k = 0; k < 3; ++k) guest::write32(ram, cursor + 4 * k, guest::read(ram, cell + 4 * k, 4));
        const int32_t x = int32_t(read_float(ram, cell)), y = int32_t(read_float(ram, cell + 4));
        if (!(call(ram, ctx, 0x801FFE34, uint32_t(x), uint32_t(y)) & 0xFF)) call(ram, ctx, 0x801FFCB0, uint32_t(x), uint32_t(y));
        call(ram, ctx, 0x801E3AE4, 3, handle);   // the figure's place only (0 would open the HP window)
        call(ram, ctx, 0x801FCC6C, uint32_t(-1000), to_word(read_float(ram, figure_x)), to_word(read_float(ram, figure_y)), 0);
        // Six cells: the shown ones carry the letters ui_text draws, digit 0 (3) where a
        // letter goes so the original still draws its cell, blank (0) between.
        for (uint32_t k = 0; k < 6; ++k) {
            guest::write8(ram, figure_cells + k, k >= 1 && k <= 4 ? 3 : 0);
            guest::write8(ram, figure_shown + k, k >= 1 && k <= 4 ? 1 : 0);
        }
        guest::write8(ram, map_sub, 6);
        s.showing = true;
        return;
    }

    // Faster: the fixed waits count two frames a frame, always stopping one short of the
    // frame that acts, so that frame (and its events, exits and sounds) stays the
    // original's. Sub-states 3 and 4 act on an exact count (bne), the others on a bound.
    uint32_t bound = 0;
    switch (sub) {
    case 3: case 4: bound = 0x14; break;
    case 6: bound = 0x1F; break;
    case 9: bound = 0x15; break;
    case 10: bound = 0x3D; break;
    default: break;
    }
    if (bound && counter + 1 < bound) guest::write32(ram, frame, counter + 1);
    // Sub-state 5 drains the HP a step a frame: a second step.
    if (sub == 5) call(ram, ctx, 0x801D8BB0);
}
}
