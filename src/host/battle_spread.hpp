#pragma once
// The battle animation across a picture wider than 4:3 (docs/design/battle-animation-rendering.md
// §17). The behaviour routines place what flies in and out at the original's screen edges:
// a shot enters at the camera target -/+ 160, leaves by a frame count, and 214 routines read
// that target. In a wider picture those edges are in the middle of it, so shots appeared out
// of thin air and vanished short of the edge, and a special move's backdrop left the sides bare.
//
// Nothing of the game's state changes. 8008261C computes every sprite's matrix from its
// slot once per frame, at the end of the battle's step; its wrapper (game_hooks.cpp) moves
// slots for the length of that call and puts them back. Timing, frame counts and the random
// numbers are the original's. Game thread.
//
// Only what a weapon's, a hit's or a reaction's script shows is moved: the actors of the two
// sides' records. The ground, the units, their shadows and whatever else stands in the
// world are never touched, so nothing has to be told apart from them by its shape. Of an
// actor:
// - where it stands across the screen, u (0 the centre, +/-1 the original's edges, in the
//   camera's view at its depth), is kept up to kInner, where the units stand and the hits
//   land, and stretched beyond it so that +/-1 falls on the picture's edges. One that
//   follows its unit and is by it stays by it, the unit being where the original has it.
// - a beam (kBeamScenes: drawn to one side of where it stands) has its far end placed the
//   same way and is stretched between the two, about where it stands;
// - a backdrop (kBackdropScenes: it covered the original's screen) is enlarged whole about
//   the screen's centre by the picture's width over the original's.
// What a scene is comes from all its frames together (battle_spread_scenes.py), never from
// the frame shown, so a sprite is treated one way for the whole of its animation.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game_frame.hpp"
#include "guest_memory.hpp"

namespace srw64::battle_spread {
inline constexpr uint32_t kSlots = 0x800FFA70, kSlotSize = 0xC4, kSlotCount = 300, kSubs = 0x34, kSubSize = 0x30;
inline constexpr uint32_t kHandles = 0x80160340, kHandleSize = 20, kHandleCount = 200;
inline constexpr uint32_t kEye = 0x8015DDFC, kAt = 0x8015DDF0, kUp = 0x8015DE14;
// The two sides' records, two script blocks in each (801C77D8: record + 0x2C and + 0x848),
// 24 actors of 0x50 bytes from a block's + 0x9C (801C5BAC): + 0 the slot, + 0x36 in use,
// + 0x38 how it is placed (1 and 3 by itself, otherwise from its unit's slot). A block's
// + 0x23 names the unit: slot 168, or 169 when set.
inline constexpr uint32_t kSides = 0x800F97E0, kSideSize = 0x1074, kBlocks[] = {0x2C, 0x848}, kActors = 0x9C, kActorSize = 0x50, kActorCount = 24;
inline constexpr uint32_t kUnitSlot = 168;
inline constexpr float kInner = 0.3f;         // |u| kept as it is
inline constexpr float kHalfHeight = 0.46630766f;   // tan(25 degrees): the projection's fovy is 50
#include "battle_spread_scenes.inc"

// Set by the battle step's wrapper for the length of the step.
inline bool in_step = false;
// SRW64_BATTLE_SPREAD=0 leaves the original's places (development, for comparisons).
inline bool enabled() {
    static const bool on = [] { const char* v = std::getenv("SRW64_BATTLE_SPREAD"); return !v || *v != '0'; }();
    return on;
}

struct Saved { uint32_t address; uint32_t words[6]; };

inline float real(const uint8_t* ram, uint32_t p) {
    const uint32_t bits = guest::read(ram, p, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
inline void put(uint8_t* ram, uint32_t p, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    guest::write32(ram, p, bits);
}
// u in the original's screen to u in the picture, `edge` the picture's half width in
// halves of the original's.
inline float place(float u, float edge) {
    const float a = std::abs(u);
    if (a <= kInner) return u;
    return std::copysign(kInner + (a - kInner) * (edge - kInner) / (1 - kInner), u);
}
// How far a sub-record's scene reaches to each side of where it stands, over all its frames,
// in the sprite's own coordinates as 8009761C draws them: the mirrored vertex set when the
// sub-record's mirror byte is set (vertex mode 0).
inline bool scene_reach(const uint8_t* ram, uint32_t sub, float& low, float& high) {
    const int16_t handle = int16_t(guest::read(ram, sub + 0x12, 2));
    if (handle < 0 || uint32_t(handle) >= kHandleCount) return false;
    const uint32_t record = kHandles + uint32_t(handle) * kHandleSize;
    if (guest::read(ram, record, 2) != 1) return false;
    const uint32_t address = guest::read(ram, record + 0x10, 4) & 0x1FFFFFFF;   // as 8008A11C gives it, any segment
    if (!address) return false;
    const uint32_t data = 0x80000000u | address;
    if (!guest::valid(data, 0x40)) return false;
    const uint32_t steps = guest::read(ram, data, 1), mode = guest::read(ram, data + 1, 1);
    if (mode > 1 || !steps || !guest::valid(data, 4 + 2 * steps)) return false;
    const bool mirrored = mode == 0 && guest::read(ram, sub + 0x1C, 1) != 0;
    bool any = false;
    for (uint32_t step = 0; step < steps; ++step) {
        const uint32_t frame = guest::read(ram, data + 2 + 2 * step, 1);
        if (frame == 0xFF) continue;
        uint32_t part = data + guest::read(ram, data + 2 + 2 * steps + 2 + 2 * frame, 2);
        for (int n = 0; n < 64 && guest::valid(part, 16); ++n, part += 16) {
            if (guest::read(ram, part, 2) & 0x8000) break;
            const uint32_t vertices = data + guest::read(ram, part + 12, 4) + (mirrored ? 0x40 : 0);
            if (!guest::valid(vertices, 64)) return false;
            for (uint32_t v = 0; v < 4; ++v) {
                const float x = int16_t(guest::read(ram, vertices + v * 16, 2));
                if (!any) { low = high = x; any = true; }
                low = std::min(low, x); high = std::max(high, x);
            }
        }
    }
    return any;
}

// A sub-record's node (0x6C bytes, pointer at +4): flags at +1 (4 hidden, 0x10 a shadow on
// the ground), the drawing routine at +0xC. Modes 0xE-0x10 register 8009761C for a scene.
inline constexpr uint32_t kSceneDrawer = 0x8009761C;
inline bool draws_scene(const uint8_t* ram, uint32_t sub) {
    if (!guest::read(ram, sub, 1)) return false;
    const uint32_t node = guest::read(ram, sub + 4, 4);
    return guest::valid(node, 0x6C) && guest::read(ram, node + 0xC, 4) == kSceneDrawer && !(guest::read(ram, node + 1, 1) & 0x10);
}
template <size_t N> inline bool listed(const uint16_t (&scenes)[N], uint32_t scene) {
    return std::binary_search(std::begin(scenes), std::end(scenes), uint16_t(scene));
}

// Moves the battle's actors for the matrices about to be computed; `saved` takes what
// restore() puts back.
inline void apply(uint8_t* ram, std::vector<Saved>& saved) {
    const float edge = frame::picture_width.load(std::memory_order_relaxed) / frame::kWidth;
    if (!in_step || !enabled() || !frame::wide.load(std::memory_order_relaxed) || edge < 1.01f) return;
    float eye[3], at[3], up[3];
    for (uint32_t i = 0; i < 3; ++i) { eye[i] = real(ram, kEye + 4 * i); at[i] = real(ram, kAt + 4 * i); up[i] = real(ram, kUp + 4 * i); }
    // The view's axes: back (from the target to the eye) and right = up x back.
    float back[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
    const float distance = std::sqrt(back[0] * back[0] + back[1] * back[1] + back[2] * back[2]);
    if (!(distance > 1) || !std::isfinite(distance)) return;
    for (float& c : back) c /= distance;
    float right[3] = {up[1] * back[2] - up[2] * back[1], up[2] * back[0] - up[0] * back[2], up[0] * back[1] - up[1] * back[0]};
    const float length = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (!(length > 1e-3f) || !std::isfinite(length)) return;
    for (float& c : right) c /= length;
    const auto across = [&](const float p[3]) { return (p[0] - at[0]) * right[0] + (p[1] - at[1]) * right[1] + (p[2] - at[2]) * right[2]; };
    bool done[kSlotCount]{};   // a slot named by two actors is moved once
    for (uint32_t side = 0; side < 2; ++side) for (const uint32_t offset : kBlocks) {
        const uint32_t block = kSides + side * kSideSize + offset;
        const uint32_t unit = kSlots + (kUnitSlot + (guest::read(ram, block + 0x23, 1) ? 1 : 0)) * kSlotSize;
        const int count = std::clamp<int>(int8_t(guest::read(ram, block + 0x26, 1)), 0, kActorCount);
        for (int index = 0; index < count; ++index) {
            const uint32_t actor = block + kActors + uint32_t(index) * kActorSize;
            const int16_t slot = int16_t(guest::read(ram, actor, 2));
            if (!guest::read(ram, actor + 0x36, 1) || slot < 0 || uint32_t(slot) >= kSlotCount || done[slot]) continue;
            if (uint32_t(slot) == kUnitSlot || uint32_t(slot) == kUnitSlot + 1) continue;
            done[slot] = true;
            const uint32_t record = kSlots + uint32_t(slot) * kSlotSize;
            const uint32_t mode = guest::read(ram, record, 1);
            if (mode < 0xE || mode > 0x10 || !guest::read(ram, record + 1, 1)) continue;
            // The scene it draws: the first sub-record that draws one.
            uint32_t sub = 0;
            for (uint32_t s = 0; s < 3 && !sub; ++s)
                if (draws_scene(ram, record + kSubs + s * kSubSize)) sub = record + kSubs + s * kSubSize;
            if (!sub) continue;
            float p[3], scale[2];
            for (uint32_t i = 0; i < 3; ++i) p[i] = real(ram, record + 4 + 4 * i);
            for (uint32_t i = 0; i < 2; ++i) scale[i] = real(ram, record + 0x10 + 4 * i);
            if (!std::isfinite(p[0] + p[1] + p[2] + scale[0] + scale[1])) continue;
            const float depth = (eye[0] - p[0]) * back[0] + (eye[1] - p[1]) * back[1] + (eye[2] - p[2]) * back[2];
            if (depth < 40) continue;
            const float half = depth * kHalfHeight * 4 / 3;   // the original's half width at this depth
            const auto placed = [&](float x) { return place(x / half, edge) * half; };
            const float d = across(p);
            const uint32_t scene = guest::read(ram, sub + 0xC, 2);
            float moved = d, wide = 1, tall = 1, rise = 0;
            if (mode != 0xE && listed(kBackdropScenes, scene)) {
                moved = d * edge; wide = tall = edge; rise = (p[1] - at[1]) * (edge - 1);
            } else {
                // Where it stands; one that follows its unit, as far from the unit as it was.
                const uint32_t placing = guest::read(ram, actor + 0x38, 2);
                moved = placed(d);
                if (placing != 1 && placing != 3 && guest::read(ram, unit + 1, 1)) {
                    float host[3];
                    for (uint32_t i = 0; i < 3; ++i) host[i] = real(ram, unit + 4 + 4 * i);
                    // Only while it is by the unit: a shot keeps the unit that fired it after
                    // the camera has gone to the other side, with that unit far off the screen.
                    const float h = across(host), apart = std::abs(d - h) / half;
                    const float near = std::clamp(2 - 2 * apart, 0.f, 1.f);   // all within half a screen's half width, none at one
                    if (std::isfinite(h)) moved += (h - placed(h)) * near * near * (3 - 2 * near);
                }
                float low, high;
                if (mode != 0xE && listed(kBeamScenes, scene) && scene_reach(ram, sub, low, high)) {
                    // Its far end, the side it reaches further to, goes where that point goes.
                    const float reach = (high > -low ? high : low) * scale[0];
                    if (std::abs(reach) > 1) wide = std::clamp((placed(d + reach) + (moved - placed(d)) - moved) / reach, 1.f, 2.f);
                }
            }
            if (std::abs(moved - d) < 0.01f && std::abs(wide - 1) < 1e-4f) continue;
            Saved keep{record, {}};
            for (uint32_t i = 0; i < 6; ++i) keep.words[i] = guest::read(ram, record + 4 + 4 * i, 4);
            saved.push_back(keep);
            for (uint32_t i = 0; i < 3; ++i) put(ram, record + 4 + 4 * i, p[i] + (moved - d) * right[i] + (i == 1 ? rise : 0));
            put(ram, record + 0x10, scale[0] * wide);
            put(ram, record + 0x14, scale[1] * tall);
        }
    }
}
inline void restore(uint8_t* ram, const std::vector<Saved>& saved) {
    for (const Saved& keep : saved)
        for (uint32_t i = 0; i < 6; ++i) guest::write32(ram, keep.address + 4 + 4 * i, keep.words[i]);
}
}  // namespace srw64::battle_spread
