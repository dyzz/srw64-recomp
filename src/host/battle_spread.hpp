#pragma once
// The battle animation across a picture wider than 4:3 (docs/design/battle-animation-rendering.md
// §17). The behaviour routines place what flies in and out at the original's screen edges:
// a shot enters at the camera target -/+ 160, leaves by a frame count, and 214 routines read
// that target. In a wider picture those edges are in the middle of it, so shots appeared out
// of thin air and vanished short of the edge.
//
// Nothing of the game's state changes. 8008261C computes every sprite's matrix from its
// slot once per frame, at the end of the battle's step; its wrapper (game_hooks.cpp) moves
// the slots for the length of that call and puts them back. A sprite's place across the
// screen, u (0 the centre, +/-1 the original's edges, measured in the camera's view at the
// sprite's depth), is kept up to kInner, where the units stand and the hits land, and
// stretched beyond it so that +/-1 falls on the picture's edges. Small sprites move, wide
// ones (a beam from the edge to its target) have both ends placed and are stretched between
// them, and one that covers the original's whole screen (a special move's backdrop) is
// enlarged whole until it covers the picture. Timing, frame counts and the random numbers
// are the original's. Game thread.
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
inline constexpr float kInner = 0.3f;         // |u| kept as it is
inline constexpr float kHalfHeight = 0.46630766f;   // tan(25 degrees): the projection's fovy is 50

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
// The current frame of a sub-record's scene in the sprite's own coordinates, as 8009761C
// draws it: the mirrored vertex set when the sub-record's mirror byte is set (vertex mode 0).
inline bool frame_box(const uint8_t* ram, uint32_t sub, float box[4]) {
    const int16_t handle = int16_t(guest::read(ram, sub + 0x12, 2));
    if (handle < 0 || uint32_t(handle) >= kHandleCount) return false;
    const uint32_t record = kHandles + uint32_t(handle) * kHandleSize;
    if (guest::read(ram, record, 2) != 1) return false;
    const uint32_t address = guest::read(ram, record + 0x10, 4) & 0x1FFFFFFF;   // as 8008A11C gives it, any segment
    if (!address) return false;
    const uint32_t data = 0x80000000u | address;
    if (!guest::valid(data, 0x40)) return false;
    const uint32_t steps = guest::read(ram, data, 1), mode = guest::read(ram, data + 1, 1), step = guest::read(ram, sub + 0x19, 1);
    if (mode > 1 || step >= steps) return false;
    const uint32_t frame = guest::read(ram, data + 2 + 2 * step, 1);
    if (frame == 0xFF) return false;
    const bool mirrored = mode == 0 && guest::read(ram, sub + 0x1C, 1) != 0;
    uint32_t part = data + guest::read(ram, data + 2 + 2 * steps + 2 + 2 * frame, 2);
    bool any = false;
    for (int n = 0; n < 64 && guest::valid(part, 16); ++n, part += 16) {
        if (guest::read(ram, part, 2) & 0x8000) break;
        const uint32_t vertices = data + guest::read(ram, part + 12, 4) + (mirrored ? 0x40 : 0);
        if (!guest::valid(vertices, 64)) return false;
        for (uint32_t v = 0; v < 4; ++v) {
            const float x = int16_t(guest::read(ram, vertices + v * 16, 2)), y = int16_t(guest::read(ram, vertices + v * 16 + 2, 2));
            if (!any) { box[0] = box[2] = x; box[1] = box[3] = y; any = true; }
            box[0] = std::min(box[0], x); box[2] = std::max(box[2], x);
            box[1] = std::min(box[1], y); box[3] = std::max(box[3], y);
        }
    }
    return any;
}

// The ground's tiles (8008422C) are slots of mode 0xF too, drawing a model rather than a
// scene: no sub-record has a scene handle. They meet edge to edge, so they stay where the
// original puts them.
inline bool is_ground(const uint8_t* ram, uint32_t record, uint32_t mode) {
    if (mode != 0xF) return false;
    for (uint32_t s = 0; s < 3; ++s) {
        const uint32_t sub = record + kSubs + s * kSubSize;
        if (guest::read(ram, sub, 1) && int16_t(guest::read(ram, sub + 0x12, 2)) >= 0) return false;
    }
    return true;
}

// Moves the battle's sprites for the matrices about to be computed; `saved` takes what
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
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
        const uint32_t record = kSlots + slot * kSlotSize;
        const uint32_t mode = guest::read(ram, record, 1);
        if (mode < 0xE || mode > 0x10 || !guest::read(ram, record + 1, 1)) continue;
        if (is_ground(ram, record, mode)) continue;
        float p[3], scale[2];
        for (uint32_t i = 0; i < 3; ++i) p[i] = real(ram, record + 4 + 4 * i);
        for (uint32_t i = 0; i < 2; ++i) scale[i] = real(ram, record + 0x10 + 4 * i);
        if (!std::isfinite(p[0] + p[1] + p[2] + scale[0] + scale[1])) continue;
        const float depth = (eye[0] - p[0]) * back[0] + (eye[1] - p[1]) * back[1] + (eye[2] - p[2]) * back[2];
        if (depth < 40) continue;
        const float half = depth * kHalfHeight * 4 / 3;   // the original's half width at this depth
        const float d = (p[0] - at[0]) * right[0] + (p[1] - at[1]) * right[1] + (p[2] - at[2]) * right[2];
        // What the slot draws, across and up, from its place.
        float box[4]{};
        bool drawn = false;
        for (uint32_t s = 0; s < 3; ++s) {
            const uint32_t sub = record + kSubs + s * kSubSize;
            float one[4];
            if (!guest::read(ram, sub, 1) || !frame_box(ram, sub, one)) continue;
            if (!drawn) std::copy(one, one + 4, box);
            box[0] = std::min(box[0], one[0]); box[1] = std::min(box[1], one[1]);
            box[2] = std::max(box[2], one[2]); box[3] = std::max(box[3], one[3]);
            drawn = true;
        }
        float left = std::min(box[0] * scale[0], box[2] * scale[0]), across = std::abs((box[2] - box[0]) * scale[0]);
        const float tall = std::abs((box[3] - box[1]) * scale[1]) / (2 * depth * kHalfHeight);
        const auto ramp = [](float v) { v = std::clamp(v, 0.f, 1.f); return v * v * (3 - 2 * v); };
        if (mode == 0xE || !drawn) { left = 0; across = 0; }   // a rotated quad only moves
        const float ul = (d + left) / half, ur = (d + left + across) / half;
        const float l = place(ul, edge) * half, r = place(ur, edge) * half;
        // How much of the stretch between its ends a sprite takes: none while it is small
        // or stays about the middle (a unit, an explosion on it), all once it is wide and
        // an end comes near the original's edge.
        const float t = ramp((across / half - 0.3f) / 0.4f) * ramp((std::max(std::abs(ul), std::abs(ur)) - 0.6f) / 0.3f);
        const float stretch = across > 1 ? 1 + ((r - l) / across - 1) * t : 1;
        // Its middle goes where the middle goes, or with the stretch to between its ends.
        const float middle = d + left + across / 2;
        const float centre = place(middle / half, edge) * half * (1 - t) + (l + r) / 2 * t;
        const float moved = centre - stretch * (left + across / 2);
        // A backdrop covers the original's screen both ways (some grow into it): it is
        // enlarged whole, about the screen's centre, rather than only widened.
        const float whole = drawn && mode != 0xE ? ramp((std::min(-ul, ur) - 0.6f) / 0.3f) * ramp((tall - 0.6f) / 0.2f) : 0;
        const float grow = 1 + (stretch - 1) * whole;
        const float rise = (p[1] - at[1]) * (grow - 1);
        if (std::abs(moved - d) < 0.01f && std::abs(stretch - 1) < 1e-4f) continue;
        Saved keep{record, {}};
        for (uint32_t i = 0; i < 6; ++i) keep.words[i] = guest::read(ram, record + 4 + 4 * i, 4);
        saved.push_back(keep);
        for (uint32_t i = 0; i < 3; ++i) put(ram, record + 4 + 4 * i, p[i] + (moved - d) * right[i] + (i == 1 ? rise : 0));
        put(ram, record + 0x10, scale[0] * stretch);
        put(ram, record + 0x14, scale[1] * grow);
    }
}
inline void restore(uint8_t* ram, const std::vector<Saved>& saved) {
    for (const Saved& keep : saved)
        for (uint32_t i = 0; i < 6; ++i) guest::write32(ram, keep.address + 4 + 4 * i, keep.words[i]);
}
}  // namespace srw64::battle_spread
