#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// MOD map icons (art/icons/<unit>.png, docs/design/mod-names-art-data.md §2.3): what one
// picture becomes. The author draws the unit once, in its own colours; like the HD-2D pack
// (tools/hd_ai/unit_icon_imagegen.py faction_mark) an acted unit turns grey, so each icon is
// four pictures, one per palette the original draws it with (1010-1013). The sides carry no
// mark: each faces its own way (unit_icon_facing.hpp). The picture looks the way the original
// icon does unless the package's art/icons.json says otherwise, and is turned for its side.
namespace srw64::unit_icon_art {
constexpr int kSize = 128;                                    // the master, as the HD-2D icons
constexpr uint32_t kIconTable = 1052024, kUnits = 363;        // u16 icon resource per unit (layout images.units)
constexpr uint32_t kFirstPalette = 1010, kActed = 1013;       // player, enemy, third party, acted
constexpr float kActedBrightness = 0.85f;

// Square pixel art at 16, 32, 64 or 128: whole multiples of the original's 16 that scale to
// the master without blending.
inline bool size_ok(int width, int height) {
    return width == height && (width == 16 || width == 32 || width == 64 || width == 128);
}

// RGBA (straight alpha) at `size` to the 128 master, each pixel repeated.
inline std::vector<uint8_t> master(const uint8_t* rgba, int size) {
    std::vector<uint8_t> out(size_t(kSize) * kSize * 4);
    const int step = kSize / size;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (int c = 0; c < 4; ++c) out[(size_t(y) * kSize + x) * 4 + c] = rgba[(size_t(y / step) * size + x / step) * 4 + c];
    return out;
}

// The master as palette `index` (0-3 from 1010) draws it: as it is for every side, grey at
// 85% brightness once acted.
inline std::vector<uint8_t> marked(std::vector<uint8_t> rgba, unsigned index) {
    const size_t n = size_t(kSize) * kSize;
    if (index == kActed - kFirstPalette) {
        for (size_t i = 0; i < n; ++i) {
            const float grey = (rgba[i * 4] * 0.299f + rgba[i * 4 + 1] * 0.587f + rgba[i * 4 + 2] * 0.114f) * kActedBrightness;
            const uint8_t g = uint8_t(grey > 255.f ? 255.f : grey);
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = g;
        }
    }
    return rgba;
}

// Premultiplied, with a box-filtered mip chain down to 1x1, so linear sampling at any
// window scale never pulls colour from transparent texels.
inline std::vector<std::vector<uint8_t>> levels(std::vector<uint8_t> rgba) {
    for (size_t i = 0; i < rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c) rgba[i + c] = uint8_t((rgba[i + c] * rgba[i + 3] + 127) / 255);
    std::vector<std::vector<uint8_t>> out;
    out.push_back(std::move(rgba));
    for (int s = kSize; s > 1; s /= 2) {
        const auto& src = out.back();
        const int d = s / 2;
        std::vector<uint8_t> next(size_t(d) * d * 4);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](int xx, int yy) { return src[(size_t(yy) * s + xx) * 4 + c]; };
                    next[(size_t(y) * d + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        out.push_back(std::move(next));
    }
    return out;
}

// The texture rectangle the icon drawer wrote (E4, E1, F1; 10.2 screen coordinates, S/T
// s10.5, steps s5.10) as a quad: the screen rectangle and the part of the 16x16 icon it
// shows (u0, v0, u1, v1 in 0-1), mirrored when S runs backwards (unit_icon_facing.hpp).
struct Quad { float rect[4], uv[4]; };
inline bool quad(uint32_t w0, uint32_t w1, uint32_t st, uint32_t steps, Quad& out) {
    const int32_t x1 = (w0 >> 12) & 0xFFF, y1 = w0 & 0xFFF, x0 = (w1 >> 12) & 0xFFF, y0 = w1 & 0xFFF;
    if (x1 <= x0 || y1 <= y0) return false;
    const float s = float(int16_t(st >> 16)) / 32.f, t = float(int16_t(st & 0xFFFF)) / 32.f;
    const float dsdx = float(int16_t(steps >> 16)) / 1024.f, dtdy = float(int16_t(steps & 0xFFFF)) / 1024.f;
    const float w = (x1 - x0) / 4.f, h = (y1 - y0) / 4.f;
    out.rect[0] = x0 / 4.f; out.rect[1] = y0 / 4.f; out.rect[2] = x1 / 4.f; out.rect[3] = y1 / 4.f;
    // A mirrored S starts a 32nd short of the far edge so the wrap never shows: round it back.
    const float s0 = dsdx < 0 ? float(int(s + 0.5f)) : s;
    out.uv[0] = s0 / 16.f; out.uv[1] = t / 16.f;
    out.uv[2] = (s0 + w * dsdx) / 16.f; out.uv[3] = (t + h * dtdy) / 16.f;
    return true;
}

// The quad turned for its side: `drawn` is the way the picture looks ('L', 'R' or 'F'; the
// package's art/icons.json, else the original icon's), `look_left` the way its unit should
// (unit_icon_facing::looks_left). A picture drawn facing front is never turned.
inline void face(Quad& q, char drawn, bool look_left) {
    const bool turn = drawn != 'F' && (drawn == 'L') != look_left;
    const float lo = std::min(q.uv[0], q.uv[2]), hi = std::max(q.uv[0], q.uv[2]);
    q.uv[0] = turn ? hi : lo;
    q.uv[2] = turn ? lo : hi;
}
}
