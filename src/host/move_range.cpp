#include "move_range.hpp"
#include "game_frame.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include <array>
#include <cstring>

// The move-range code of the tactical overlay (load_000AB160), static reading; the
// flow is laid out in docs/gameplay/base-fixes.md (BUG10). Game thread only.
namespace srw64::move_range {
namespace {
using namespace guest;
constexpr uint32_t kObjects = 0x800FFA70, kObjectSize = 0xC4;    // +1 kind, +4/+8/+C position (f32)
constexpr uint32_t kRoster = 0x8015E100, kRosterSide = 600;      // one side's records, 20 bytes each
constexpr uint32_t kReachTable = 0x80227BD0, kCostTable = 0x80227328, kTerrainTable = 0x8021F470;
constexpr uint32_t kTerrainCosts = 0x802196D5;                   // record +5, 11 bytes each, by terrain
constexpr uint32_t kMapWidth = 0x80172EC4, kMapHeight = 0x80172EC8;
constexpr uint32_t kAnchorX = 0x80172ECC, kAnchorY = 0x80172ED0;
constexpr uint32_t kCursorX = 0x80102308, kCursorY = 0x8010230C;
constexpr uint32_t kScrollX = 0x8010F5D4, kScrollY = 0x8010F5D8;
constexpr uint32_t kState = 0x80172EB0;
constexpr uint32_t kWalk = 0x80172F04;                           // now x, y; before x, y (int8, the unit at 15)
constexpr uint32_t kBoarding = 0x8021F83B;                       // 801EA498: the ships this unit may board

Grid grid;
bool wide = false;
uint32_t generation = 0;          // of the fill; the AI's nearest square is cached by it
struct Nearest {
    bool valid = false, found = false;
    uint32_t generation = 0, unit = 0, side = 0, index = 0;
    int32_t x = 0, y = 0;
} nearest_cache;

uint8_t byte(const uint8_t* ram, uint32_t p) { return uint8_t(read(ram, p, 1)); }
uint32_t word(const uint8_t* ram, uint32_t p) { return read(ram, p, 4); }
int32_t s32(const uint8_t* ram, uint32_t p) { return int32_t(word(ram, p)); }
float f32(const uint8_t* ram, uint32_t p) {
    const uint32_t bits = word(ram, p);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
void put_f32(uint8_t* ram, uint32_t p, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    write32(ram, p, bits);
}
uint32_t object(uint32_t handle) { return kObjects + handle * kObjectSize; }

// 801E510C: side 0 is handles 0x42.., 1 is 0x60.., 2 is 0x7E..
struct Unit { uint32_t side, index; };
Unit unit_of(uint32_t handle) {
    if (handle < 0x60) return {0, handle - 0x42};
    if (handle < 0x7E) return {1, handle - 0x60};
    return {2, handle - 0x7E};
}
uint32_t roster(Unit unit) { return kRoster + unit.index * 20 + unit.side * kRosterSide; }

// A guest call from a hook: the context is a copy, below the caller's frame.
struct Guest {
    uint8_t* ram;
    recomp_context call;
    Guest(uint8_t* ram, const recomp_context* ctx) : ram(ram), call(*ctx) { call.r29 = int32_t(uint32_t(ctx->r29) - 0x200); }
    uint32_t sp(uint32_t offset) const { return uint32_t(call.r29) + offset; }
    bool occupied(int32_t handle, int32_t x, int32_t y, uint32_t mode, uint32_t& who) {
        call.r4 = handle; call.r5 = int32_t(sp(0x20)); call.r6 = x; call.r7 = y;
        write32(ram, sp(0x10), mode);
        load_000AB160_func_801EB4C0(ram, &call);
        who = word(ram, sp(0x20));
        return (uint32_t(call.r2) & 0xFF) != 0;
    }
    float distance(float x, float y, uint32_t to) {
        put_f32(ram, sp(0x28), x); put_f32(ram, sp(0x2C), y); write32(ram, sp(0x30), 0);
        write32(ram, sp(0x10), sp(0x3C));
        call.r4 = 0; call.r5 = int32_t(sp(0x28)); call.r6 = int32_t(to); call.r7 = int32_t(sp(0x38));
        resident_func_80081F08(ram, &call);
        return f32(ram, sp(0x3C));
    }
};

// 801C3E50 with a move the original's tables cannot hold. Above 15 before the EN limit
// and not after it, the original still does: it applies the same limit itself.
bool fill(uint8_t* ram, recomp_context* ctx) {
    int move = int32_t(ctx->r5);
    if (move <= kWindow) return false;
    const int32_t handle = int32_t(ctx->r4);
    Guest g(ram, ctx);
    if (int(read(ram, uint32_t(ctx->r6) + 8, 2)) < move) {
        load_000AB160_func_801E2054(ram, &g.call);
        if (byte(ram, object(handle) + 1) == 2 || (uint32_t(g.call.r2) & 0xFF) == 1)
            move = int(read(ram, uint32_t(ctx->r6) + 8, 2));
    }
    if (move <= kWindow) return false;

    load_000AB160_func_801EA3C8(ram, &g.call);
    srw64_original_move_clear(ram, &g.call);
    const int32_t x = int32_t(f32(ram, object(handle) + 4)), y = int32_t(f32(ram, object(handle) + 8));
    g.call.r4 = handle;
    load_000AB160_func_801EEA14(ram, &g.call);
    const uint32_t column = uint32_t(g.call.r2);
    grid.survey(move, x, y, s32(ram, kMapWidth), s32(ram, kMapHeight), [&](int dx, int dy) {
        const int32_t px = x + dx * 16, py = y + dy * 16;
        g.call.r4 = px >> 4; g.call.r5 = py >> 4; g.call.r6 = 0x3B; g.call.r7 = 0;
        load_000AB160_func_801E213C(ram, &g.call);
        uint8_t terrain = uint8_t(g.call.r2);
        uint32_t who;
        const bool blocked = g.occupied(handle, px, py, 1, who);
        if (blocked) terrain = 0xFF;
        return Sample{terrain, byte(ram, kTerrainCosts + terrain * 11u + column), blocked};
    });
    grid.flood(move);

    g.call.r4 = handle;
    load_000AB160_func_801EA498(ram, &g.call);
    const Unit self = unit_of(uint32_t(handle));
    grid.mark_passes([&](int dx, int dy) {
        uint32_t who;
        if (!g.occupied(handle, x + dx * 16, y + dy * 16, 0, who)) return false;
        const Unit other = unit_of(who);
        if (other.side != 0) return true;
        if (byte(ram, kBoarding) != 0)
            for (uint32_t ship = 0; ship < 3; ++ship)
                if (int32_t(who) == int16_t(read(ram, kBoarding + 0x11 + ship * 2, 2))) return false;
        if (byte(ram, roster(self) + 1) & 0x80) return true;
        if (word(ram, word(ram, roster(self) + 0xC) + 0x28) & 0x80000) return true;
        return (word(ram, word(ram, roster(other) + 0xC) + 0x28) & 0x80000) == 0;
    });
    grid.store_window([&](int gx, int gy, uint8_t reach, uint8_t cost, uint8_t terrain) {
        const uint32_t cell = uint32_t(gy * kWindowSize + gx);
        write8(ram, kReachTable + cell, reach);
        write8(ram, kCostTable + cell, cost);
        write8(ram, kTerrainTable + cell, terrain);
    });
    wide = true;
    ++generation;
    return true;
}

// 801C3D34 clears the tables for any new range, a weapon's too: the grid is out of date.
void cleared() { wide = false; }

// 801E4760 draws from the tables around the anchor. The window slides to the part of the
// grid on screen for the call: ±240 pixels covers the widest picture the game allows.
std::array<uint8_t, kWindowSize * kWindowSize> saved_reach;
int32_t saved_anchor[2];
bool draw_begin(uint8_t* ram) {
    if (!wide) return false;
    saved_anchor[0] = s32(ram, kAnchorX);
    saved_anchor[1] = s32(ram, kAnchorY);
    const int32_t view = srw64::frame::wide ? int32_t(srw64::frame::picture_width) : int32_t(srw64::frame::kWidth);
    const int32_t shift_x = (view / 2 - s32(ram, kScrollX) - saved_anchor[0] + 8) >> 4;
    const int32_t shift_y = (120 - s32(ram, kScrollY) - saved_anchor[1] + 8) >> 4;
    for (int gy = 0; gy < kWindowSize; ++gy)
        for (int gx = 0; gx < kWindowSize; ++gx) {
            const uint32_t cell = uint32_t(gy * kWindowSize + gx);
            saved_reach[cell] = byte(ram, kReachTable + cell);
            write8(ram, kReachTable + cell, grid.reach(gx - kWindow + shift_x, gy - kWindow + shift_y));
        }
    write32(ram, kAnchorX, uint32_t(saved_anchor[0] + shift_x * 16));
    write32(ram, kAnchorY, uint32_t(saved_anchor[1] + shift_y * 16));
    return true;
}
void draw_end(uint8_t* ram) {
    for (uint32_t cell = 0; cell < saved_reach.size(); ++cell) write8(ram, kReachTable + cell, saved_reach[cell]);
    write32(ram, kAnchorX, uint32_t(saved_anchor[0]));
    write32(ram, kAnchorY, uint32_t(saved_anchor[1]));
}

// 801E0B94 for the destination the player confirms (the attack screens ask it too, of a
// weapon's range, by which time 801C3D34 has ended the wide grid).
bool query(uint8_t* ram, recomp_context* ctx) {
    if (!wide || (uint32_t(ctx->r4) & 0xFF) != kStop || int8_t(byte(ram, kState)) != 0xC) return false;
    const int dx = (int32_t(ctx->r5) >> 4) - (s32(ram, kAnchorX) >> 4);
    const int dy = (int32_t(ctx->r6) >> 4) - (s32(ram, kAnchorY) >> 4);
    ctx->r2 = grid.reach(dx, dy) == kStop;
    return true;
}

// 801E0C68. Without a wide grid the original runs, but no higher than 15: its search has
// 15 layers. A move over 15 got here with the range already cut to the EN left, so the
// cheapest way to any square in range is within 15 and the result does not change.
bool path(uint8_t* ram, recomp_context* ctx) {
    if (!wide) {
        if (int32_t(ctx->r4) > kWindow) ctx->r4 = kWindow;
        return false;
    }
    const int tx = (int32_t(f32(ram, kCursorX)) >> 4) - (s32(ram, kAnchorX) >> 4);
    const int ty = (int32_t(f32(ram, kCursorY)) >> 4) - (s32(ram, kAnchorY) >> 4);
    grid.find_path(int32_t(ctx->r4), tx, ty);
    for (uint32_t i = 0; i < 4; ++i) write8(ram, kWalk + i, kWindow);
    return true;
}

// 801C5810.
bool step(uint8_t* ram, recomp_context* ctx) {
    if (!wide) return false;
    int dx = int8_t(byte(ram, kWalk)) - kWindow, dy = int8_t(byte(ram, kWalk + 1)) - kWindow;
    write8(ram, kWalk + 2, byte(ram, kWalk));
    write8(ram, kWalk + 3, byte(ram, kWalk + 1));
    const bool over = grid.advance(dx, dy);
    write8(ram, kWalk, uint8_t(dx + kWindow));
    write8(ram, kWalk + 1, uint8_t(dy + kWindow));
    ctx->r2 = over;
    return true;
}

// 80202848: the enemy AI's square nearest the unit it goes for.
bool near_unit(uint8_t* ram, recomp_context* ctx) {
    if (!wide) return false;
    const int32_t self = int32_t(ctx->r4);
    const uint32_t to = uint32_t(ctx->r5), out_x = uint32_t(ctx->r6), out_y = uint32_t(ctx->r7);
    const int32_t x = int32_t(f32(ram, object(self) + 4)), y = int32_t(f32(ram, object(self) + 8));
    Guest g(ram, ctx);
    for (uint32_t i = 0; i < 3; ++i) write32(ram, g.sp(0x18 + i * 4), word(ram, object(to) + 4 + i * 4));
    int bx = 0, by = 0;
    if (grid.pick_nearest(
            [&](int dx, int dy) {
                g.call.r4 = self; g.call.r5 = x + dx * 16; g.call.r6 = y + dy * 16;
                load_000AB160_func_802024FC(ram, &g.call);
                return (uint32_t(g.call.r2) & 0xFF) == 0;
            },
            [&](int dx, int dy) { return g.distance(float(x + dx * 16), float(y + dy * 16), g.sp(0x18)); }, bx, by)) {
        write32(ram, out_x, uint32_t(x + bx * 16));
        write32(ram, out_y, uint32_t(y + by * 16));
    }
    return true;
}

// 80202A84: the square nearest a unit given by side and index. 802024FC calls this once
// for each square 80202848 tries, always with the same answer: the scan is made once.
bool near_target(uint8_t* ram, recomp_context* ctx) {
    if (!wide) return false;
    const uint32_t self = uint32_t(ctx->r4), side = uint32_t(ctx->r5), index = uint32_t(ctx->r6);
    const uint32_t out_x = uint32_t(ctx->r7), out_y = word(ram, uint32_t(ctx->r29) + 0x10);
    const uint32_t record = roster({side, index});
    const float tx = float(byte(ram, record + 6) << 4) + 32.0f, ty = float(byte(ram, record + 7) << 4) + 32.0f;
    const int32_t x = int32_t(f32(ram, object(self) + 4)), y = int32_t(f32(ram, object(self) + 8));
    if (tx == float(x) && ty == float(y)) {
        write32(ram, out_x, uint32_t(x));
        write32(ram, out_y, uint32_t(y));
        return true;
    }
    Nearest& cache = nearest_cache;
    if (!cache.valid || cache.generation != generation || cache.unit != self || cache.side != side || cache.index != index) {
        Guest g(ram, ctx);
        put_f32(ram, g.sp(0x18), tx); put_f32(ram, g.sp(0x1C), ty); write32(ram, g.sp(0x20), 0);
        int bx = 0, by = 0;
        cache.found = grid.pick_nearest([](int, int) { return true; },
            [&](int dx, int dy) { return g.distance(float(x + dx * 16), float(y + dy * 16), g.sp(0x18)); }, bx, by);
        cache.x = x + bx * 16; cache.y = y + by * 16;
        cache.valid = true; cache.generation = generation;
        cache.unit = self; cache.side = side; cache.index = index;
    }
    if (cache.found) {
        write32(ram, out_x, uint32_t(cache.x));
        write32(ram, out_y, uint32_t(cache.y));
    }
    return true;
}

// 801F3268 with no radius: whether unit a1 stands on a square of value a2 of unit a0's
// range (801F9590 lights the ships a unit can board, right after the fill).
bool unit_on_range(uint8_t* ram, recomp_context* ctx) {
    if (!wide || (uint32_t(ctx->r7) & 0xFF) != 0) return false;
    const uint32_t self = uint32_t(ctx->r4), other = uint32_t(ctx->r5);
    const int32_t x = int32_t(f32(ram, object(self) + 4)), y = int32_t(f32(ram, object(self) + 8));
    const int32_t ox = int32_t(f32(ram, object(other) + 4)), oy = int32_t(f32(ram, object(other) + 8));
    ctx->r2 = 0;
    if ((ox - x) % 16 || (oy - y) % 16) return true;
    if (grid.reach((ox - x) / 16, (oy - y) / 16) != (uint32_t(ctx->r6) & 0xFF)) return true;
    ctx->r2 = byte(ram, object(other) + 1) != 3;
    return true;
}
}  // namespace

const Grid* active() { return wide ? &grid : nullptr; }

void configure() {
    srw64_game_hooks.move_fill = fill;
    srw64_game_hooks.move_cleared = cleared;
    srw64_game_hooks.move_draw_begin = draw_begin;
    srw64_game_hooks.move_draw_end = draw_end;
    srw64_game_hooks.move_query = query;
    srw64_game_hooks.move_path = path;
    srw64_game_hooks.move_step = step;
    srw64_game_hooks.ai_near_unit = near_unit;
    srw64_game_hooks.ai_near_target = near_target;
    srw64_game_hooks.unit_on_range = unit_on_range;
}
}  // namespace srw64::move_range
