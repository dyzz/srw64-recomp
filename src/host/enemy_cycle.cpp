#include "enemy_cycle.hpp"
#include "game_hooks.hpp"
#include "graphics.hpp"
#include "guest_memory.hpp"
#include "input_mode.hpp"
#include "settings_window.hpp"
#include "funcs.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace srw64::enemy_cycle {
namespace {
using namespace guest;
using json = nlohmann::json;
// Tactical overlay (load_000AB160), static reading (docs/native/enemy-cycle.md). The idle
// map's step 801C8B04 is where the original's L / R (and Z) step through the player's units.
constexpr uint32_t kRoster = 0x8015E100, kSideStride = 0x258, kSlotStride = 0x14;  // +0 status (1: on the map), +0xC unit
constexpr unsigned kSlots = 30;
constexpr uint32_t kHandleBase[3] = {0x42, 0x60, 0x7E};                   // 801E514C: side and slot to handle
constexpr uint32_t kObjects = 0x800FFA74, kObjectSize = 0xC4;             // by handle; +0 / +4 map pixels, f32
constexpr uint32_t kCursorX = 0x80102308, kCursorY = 0x8010230C;          // cursor object 0x35, f32
constexpr uint32_t kTargetX = 0x80172EB4, kTargetY = 0x80172EB8;
constexpr unsigned kCursorSound = 0xB9;
constexpr uint64_t kRepeatDelay = 24, kRepeatEvery = 8;                   // VI, a trigger held down

std::ofstream log;
std::mutex mutex;                       // the snapshot below, for the debug thread
json snapshot = {{"steps", 0}, {"last", nullptr}};
uint32_t held_before = 0;
uint64_t held_since = 0, last_step = 0;

float f32(const uint8_t* ram, uint32_t p) {
    const uint32_t bits = read(ram, p, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
void put_f32(uint8_t* ram, uint32_t p, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    write32(ram, p, bits);
}
uint32_t call(uint8_t* ram, recomp_context* ctx, void (*function)(uint8_t*, recomp_context*), uint32_t a0 = 0, uint32_t a1 = 0) {
    auto c = *ctx;
    c.r29 = int32_t(uint32_t(ctx->r29) - 0x200);
    c.r4 = int32_t(a0); c.r5 = int32_t(a1);
    function(ram, &c);
    return uint32_t(c.r2);
}

struct Unit { unsigned side, slot, handle; float x, y; };
// The enemy's and the third party's units on the map, in roster order.
std::vector<Unit> opponents(const uint8_t* ram) {
    std::vector<Unit> units;
    for (unsigned side = 1; side <= 2; ++side)
        for (unsigned slot = 0; slot < kSlots; ++slot) {
            const uint32_t entry = kRoster + side * kSideStride + slot * kSlotStride;
            if (read(ram, entry, 1) != 1 || !valid(read(ram, entry + 0xC, 4), 0x54)) continue;
            const uint32_t handle = kHandleBase[side] + slot, object = kObjects + handle * kObjectSize;
            units.push_back({side, slot, handle, f32(ram, object), f32(ram, object + 4)});
        }
    return units;
}

bool idle(uint8_t* ram, recomp_context* ctx) {
    // The triggers are host bits (input_mode.hpp): the game never reads them.
    const uint32_t triggers = srw64_pad_state() & (input::pad_l2 | input::pad_r2);
    const uint64_t now = srw64_current_vi();
    const bool fresh = triggers & ~held_before;
    if (triggers != held_before) held_since = now;
    held_before = triggers;
    if (!triggers || triggers == (input::pad_l2 | input::pad_r2) || settings_window::owns_input()) return false;
    if (!fresh && (now - held_since < kRepeatDelay || now - last_step < kRepeatEvery)) return false;
    const auto units = opponents(ram);
    if (units.empty()) return false;
    // From the unit under the cursor, or from either end when the cursor is on none.
    const bool back = triggers & input::pad_l2;
    const float cx = f32(ram, kCursorX), cy = f32(ram, kCursorY);
    const int count = int(units.size());
    int at = -1;
    for (int i = 0; i < count; ++i)
        if (std::fabs(units[i].x - cx) < 8 && std::fabs(units[i].y - cy) < 8) { at = i; break; }
    const auto& unit = units[at < 0 ? (back ? count - 1 : 0) : (at + (back ? count - 1 : 1)) % count];
    // As 801C8B04 does for L / R: the cursor sound, cursor and target together, and the
    // camera re-centred (801FFCB0) only when the unit is off screen (801FFE34).
    call(ram, ctx, resident_func_8007E8A8, kCursorSound);
    put_f32(ram, kCursorX, unit.x); put_f32(ram, kTargetX, unit.x);
    put_f32(ram, kCursorY, unit.y); put_f32(ram, kTargetY, unit.y);
    const int32_t px = int32_t(unit.x), py = int32_t(unit.y);
    if (!(call(ram, ctx, load_000AB160_func_801FFE34, uint32_t(px), uint32_t(py)) & 0xFF))
        call(ram, ctx, load_000AB160_func_801FFCB0, uint32_t(px), uint32_t(py));
    last_step = now;
    json row = {{"kind", "step"}, {"direction", back ? "previous" : "next"}, {"side", unit.side}, {"slot", unit.slot},
                {"handle", unit.handle}, {"x", px}, {"y", py}, {"opponents", count}, {"vi", now}};
    {
        std::lock_guard lock(mutex);
        snapshot["steps"] = snapshot["steps"].get<unsigned>() + 1;
        snapshot["last"] = row;
    }
    if (log.is_open()) {
        row["schema"] = "srw64.enemy-cycle-event.v1";
        log << row.dump() << '\n';
        log.flush();
    }
    return true;   // this frame is the step; the original runs again from the next
}
}

void configure(const std::filesystem::path& output) {
    if (!output.empty()) log.open(output / "enemy-cycle-events.jsonl");
    srw64_game_hooks.map_idle = idle;
}
json state() {
    std::lock_guard lock(mutex);
    return snapshot;
}
}
