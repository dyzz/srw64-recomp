#include "battle_hud.hpp"
#include "game_frame.hpp"
#include "wide_map.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace srw64::battle_hud {
namespace {
// How far the windows move up or down into the rows the bands covered, in original pixels:
// the HP windows to the top edge, the quote box and the portrait to the bottom (the
// portrait's bust ends at the picture's edge, as it ended at the band before).
constexpr int kTop = -12, kBox = 14, kPortrait = 16;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}
void put(uint8_t* rdram, uint32_t address, uint32_t value) { std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4); }

// The bands: the battle overlay's static list sets black and fills (0, 0)-(320, 16) and
// (0, 224)-(320, 240). Left out, the two fills become no-ops; put back for 4:3.
constexpr uint32_t kBands = 0x80222D78;
constexpr uint32_t kBandsOn[8] = {0xFA000000, 0x000000FF, 0xF6500040, 0x00000000, 0xF65003C0, 0x00000380, 0xDF000000, 0};
constexpr uint32_t kBandsOff[8] = {0xFA000000, 0x000000FF, 0, 0, 0, 0, 0xDF000000, 0};

constexpr uint32_t kExtended = 0x64000000, kSetScissor = 0x05, kSetRectAlign = 0x06, kPushScissor = 0x17, kPopScissor = 0x18;
constexpr uint32_t kOriginLeft = 0x000, kOriginRight = 0x400, kOriginNone = 0x800;
uint32_t pair(int32_t high, int32_t low) { return (uint32_t(high) & 0xFFFF) << 16 | (uint32_t(low) & 0xFFFF); }

// The cut-in frame's static list and its first command (G_TEXTURE off); the widened copy
// lives in the last 256 bytes of the debug script scratch (script_inject.hpp, 807F0000-807FFFFF),
// which the game never uses.
constexpr uint32_t kCutinMask = 0x800C6D00, kCutinHead[2] = {0xD7000000, 0x00000000}, kCutinCopy = 0x807FFF00;

// Half the picture beyond the original's 320, in 10.2 pixels.
int32_t side4() { return int32_t(std::lround((float(frame::picture_width) - frame::kWidth) * 2)); }

enum class Kind { none, top, box_frame, box_fill, box_text, portrait };
Kind kind(uint32_t function, int32_t slot) {
    switch (function) {
    case 0x800945D4:   // 16 x 16 tiles: the HP windows' frame (31), the counter mark (36), the quote box (32, 33)
        if (slot == 31 || slot == 36) return Kind::top;
        if (slot == 32 || slot == 33) return Kind::box_frame;
        return Kind::none;
    case 0x8009B74C: return slot == 31 ? Kind::top : Kind::none;              // the HP windows' ground
    case 0x801C2C98: return slot == 29 || slot == 30 ? Kind::top : Kind::none; // HP and EN gauges
    case 0x8008EB5C: return Kind::top;                                         // HP and EN figures, both sides in one strip
    case 0x8009B970: return slot == 32 || slot == 33 ? Kind::box_fill : Kind::none;  // the quote box's ground
    case 0x8008DC40: return Kind::box_text;                                    // the original's quote text
    case 0x800964E4: return slot == 27 || slot == 28 ? Kind::portrait : Kind::none;
    default: return Kind::none;
    }
}

struct Rect {
    size_t at, words;          // in the copy of the draw, 2 (FILLRECT) or 6 (TEXRECT + E1 + F1)
    int32_t ulx, uly, lrx, lry; // 10.2
};

class Writer {
public:
    std::vector<uint32_t> out;
    void align(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
        const int32_t now[4] = {ulx, uly, lrx, lry};
        if (std::equal(now, now + 4, last)) return;
        std::copy(now, now + 4, last);
        out.insert(out.end(), {kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, pair(ulx, uly), pair(lrx, lry)});
    }
private:
    int32_t last[4] = {INT32_MIN, 0, 0, 0};   // the first always goes out
};
}  // namespace

bool active() { return frame::wide && wide_map::battle_shown(); }

void frame(uint8_t* rdram) {
    uint32_t now[8];
    for (int i = 0; i < 8; ++i) now[i] = word(rdram, kBands + 4 * i);
    const bool on = std::equal(now, now + 8, kBandsOn), off = std::equal(now, now + 8, kBandsOff);
    const bool wanted_off = frame::wide;
    if (on && wanted_off) for (int i = 0; i < 8; ++i) put(rdram, kBands + 4 * i, kBandsOff[i]);
    if (off && !wanted_off) for (int i = 0; i < 8; ++i) put(rdram, kBands + 4 * i, kBandsOn[i]);
    // The cut-in's black frame (resident static list 800C6D00, docs/design/battle-animation-
    // rendering.md §6.3): four FILLRECTs leave a 140 x 120 window; the top and bottom ones
    // span 0-320 and RT64 widens them, the side ones (0-90, 230-320) do not, so on a wider
    // picture the battle showed past them. On a wider picture the list branches to a copy
    // whose side rectangles reach the picture's edges (gEX rect alignment, like the HUD).
    const bool widened = frame::wide && side4() > 0;
    const uint32_t head0 = word(rdram, kCutinMask), head1 = word(rdram, kCutinMask + 4);
    const bool original = head0 == kCutinHead[0] && head1 == kCutinHead[1], branched = head0 == 0xDE010000 && head1 == kCutinCopy;
    if (widened && (original || branched)) {
        const int32_t side = side4();
        const uint32_t list[] = {
            0xD7000000, 0x00000000, 0xE200001C, 0x00504240, 0xFCFFFFFF, 0xFFFDF6FB, 0xFA000000, 0x000000FF,
            0xF65000B4, 0x00000000, 0xF65003C0, 0x00000294,                                // top, bottom
            kExtended | kPushScissor, 0,
            kExtended | kSetScissor, kOriginLeft << 2 | kOriginRight << 14, pair(0, 0), pair(0, 240 * 4),
            kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, pair(-side, 0), pair(0, 0),
            0xF6168294, 0x000000B4,                                                        // left
            kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, pair(0, 0), pair(side, 0),
            0xF6500294, 0x003980B4,                                                        // right
            kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, 0, 0,
            kExtended | kPopScissor, 0,
            0xDF000000, 0x00000000};
        for (size_t i = 0; i < std::size(list); ++i) put(rdram, kCutinCopy + 4 * uint32_t(i), list[i]);
        if (original) { put(rdram, kCutinMask, 0xDE010000); put(rdram, kCutinMask + 4, kCutinCopy); }
    } else if (!widened && branched) {
        put(rdram, kCutinMask, kCutinHead[0]); put(rdram, kCutinMask + 4, kCutinHead[1]);
    }
}

void portrait_offset(float x, float& dx, float& dy) {
    dx = dy = 0;
    if (!active()) return;
    const float side = side4() / 4.f;
    dx = x < frame::kWidth / 2 ? -side : side;
    dy = kPortrait;
}

void sprite_offset(int32_t slot, float x, float& dx, float& dy) {
    dx = dy = 0;
    if (!active()) return;
    const float side = side4() / 4.f;
    if (slot == 29 || slot == 30 || slot == 31 || slot == 36) { dx = x < frame::kWidth / 2 ? -side : side; dy = kTop; }
    else if (slot == 27 || slot == 28) { dx = x < frame::kWidth / 2 ? -side : side; dy = kPortrait; }
}

void text_offset(float x, float y, float& dx, float& dy) {
    dx = dy = 0;
    if (!active()) return;
    const float side = side4() / 4.f;
    if (y < 50) { dx = x < frame::kWidth / 2 ? -side : side; dy = kTop; }
    else if (y >= 160) { dx = -side; dy = kBox; }   // the quote box, 162-226
}

Quote quote() {
    if (!active()) return {};
    const float side = side4() / 4.f;
    return {-side, float(kBox), 2 * side};
}

void after_draw(uint8_t* rdram, int32_t cursor, uint32_t function, int32_t slot, int32_t sub, uint32_t begin) {
    (void)sub;
    if (!active()) return;
    const Kind what = kind(function, slot);
    if (what == Kind::none) return;
    const uint32_t start = begin & 0x1FFFFFFF, end = word(rdram, uint32_t(cursor)) & 0x1FFFFFFF;
    if (end <= start || end - start > 0x8000) return;
    // Development: the battle without its windows, for the viewer's scene pictures
    // (tools/hd_ai/viewer_scenes.py).
    static const bool hidden = std::getenv("SRW64_DEV_HIDE_BATTLE_HUD") != nullptr;
    if (hidden) { put(rdram, uint32_t(cursor), begin); return; }
    std::vector<uint32_t> in((end - start) / 4);
    for (size_t i = 0; i < in.size(); ++i) in[i] = word(rdram, start + uint32_t(4 * i));
    // The rectangles it drew.
    std::vector<Rect> rects;
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        const uint32_t w0 = in[i], w1 = in[i + 1], op = w0 >> 24;
        const Rect r{i, 2, int32_t(w1 >> 12 & 0xFFF), int32_t(w1 & 0xFFF), int32_t(w0 >> 12 & 0xFFF), int32_t(w0 & 0xFFF)};
        if (op == 0xF6) rects.push_back(r);
        else if (op == 0xE4 && i + 5 < in.size() && in[i + 2] >> 24 == 0xE1 && in[i + 4] >> 24 == 0xF1) {
            rects.push_back({i, 6, r.ulx, r.uly, r.lrx, r.lry});
            i += 4;
        }
    }
    if (rects.empty()) return;
    const int32_t e = side4(), mid = 160 * 4;
    int32_t left = INT32_MAX, right = INT32_MIN;
    for (const auto& r : rects) { left = std::min(left, r.ulx); right = std::max(right, r.lrx); }
    const int32_t box_mid = (left + right) / 2;

    Writer w;
    // Outside the original's 320 the frame's scissor would cut them off.
    w.out.insert(w.out.end(), {kExtended | kPushScissor, 0,
        kExtended | kSetScissor, kOriginLeft << 2 | kOriginRight << 14, pair(0, 0), pair(0, 240 * 4)});
    // A copy of r with its left edge at ulx (10.2) and texture moved to match.
    const auto from = [&](const Rect& r, int32_t ulx) {
        std::vector<uint32_t> words(in.begin() + r.at, in.begin() + r.at + r.words);
        words[1] = (words[1] & ~(0xFFFu << 12)) | uint32_t(ulx) << 12;
        if (r.words == 6) {
            const int32_t s = int16_t(words[3] >> 16), dsdx = int16_t(words[5] >> 16);
            const int32_t moved = s + (ulx - r.ulx) * dsdx / 128;
            words[3] = (words[3] & 0xFFFF) | (uint32_t(moved) & 0xFFFF) << 16;
        }
        return words;
    };
    const auto upto = [&](const Rect& r, int32_t lrx) {
        std::vector<uint32_t> words(in.begin() + r.at, in.begin() + r.at + r.words);
        words[0] = (words[0] & ~(0xFFFu << 12)) | uint32_t(lrx) << 12;
        return words;
    };
    // Each rectangle becomes one or more pieces, each with its offsets. The first piece's
    // offsets go in before the commands that set the rectangle up, so a native draw's tag
    // (a no-op right before its rectangle, native_sprite.cpp / native_portrait.cpp) stays
    // next to it; those marker rectangles are left where they are, their draws move by
    // themselves (text_offset, portrait_offset).
    struct Piece { int32_t ulx, uly, lrx, lry; std::vector<uint32_t> words; };
    const auto whole = [&](const Rect& r) { return std::vector<uint32_t>(in.begin() + r.at, in.begin() + r.at + r.words); };
    const auto moved = [&](const Rect& r, int32_t dx, int32_t dy) { return Piece{dx, dy, dx, dy, whole(r)}; };
    size_t next = 0;
    for (const auto& r : rects) {
        std::vector<Piece> pieces;
        const bool marker = r.at >= 2 && in[r.at - 2] >> 16 == 0x0053;
        if (marker) pieces.push_back(moved(r, 0, 0));
        else switch (what) {
        case Kind::top:
            if (r.lry > 50 * 4) pieces.push_back(moved(r, 0, 0));
            else if (r.ulx < mid && r.lrx > mid) {   // across the middle: each half to its side
                pieces.push_back({-e, kTop * 4, -e, kTop * 4, upto(r, mid)});
                pieces.push_back({e, kTop * 4, e, kTop * 4, from(r, mid)});
            } else pieces.push_back(moved(r, r.ulx + r.lrx < 2 * mid ? -e : e, kTop * 4));
            break;
        case Kind::portrait:   // the whole portrait to its side
            pieces.push_back(moved(r, left + right < 2 * mid ? -e : e, kPortrait * 4));
            break;
        case Kind::box_fill:
            pieces.push_back({-e, kBox * 4, e, kBox * 4, whole(r)});
            break;
        case Kind::box_text:
            pieces.push_back(r.uly < 160 * 4 ? moved(r, 0, 0) : moved(r, -e, kBox * 4));
            break;
        case Kind::box_frame: {
            // The tiles left of the box's middle go with its left edge, the rest with its
            // right; the tile just left of the middle repeats across the gap between them.
            pieces.push_back(moved(r, r.ulx + r.lrx < 2 * box_mid ? -e : e, kBox * 4));
            const int32_t width = r.lrx - r.ulx;
            if (r.lrx == box_mid && width > 0)
                for (int32_t shift = -e + width; r.ulx + shift < box_mid + e; shift += width)
                    pieces.push_back({shift, kBox * 4, std::min(shift, box_mid + e - r.lrx), kBox * 4, whole(r)});
            break;
        }
        case Kind::none: pieces.push_back(moved(r, 0, 0)); break;
        }
        for (size_t k = 0; k < pieces.size(); ++k) {
            const auto& piece = pieces[k];
            w.align(piece.ulx, piece.uly, piece.lrx, piece.lry);
            if (k == 0) w.out.insert(w.out.end(), in.begin() + next, in.begin() + r.at);
            w.out.insert(w.out.end(), piece.words.begin(), piece.words.end());
        }
        next = r.at + r.words;
    }
    w.out.insert(w.out.end(), in.begin() + next, in.end());
    w.align(0, 0, 0, 0);
    w.out.insert(w.out.end(), {kExtended | kPopScissor, 0});
    for (size_t i = 0; i < w.out.size(); ++i) put(rdram, start + uint32_t(4 * i), w.out[i]);
    put(rdram, uint32_t(cursor), (begin & 0xE0000000) | (start + uint32_t(4 * w.out.size())));
}
}  // namespace srw64::battle_hud
