#include "menu_widen.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace srw64::menu_widen {
namespace {
// The unit commands: grid scenes 1161-1171 (D_800C8988 entries 7-17 and 19, menus of
// 1-11 rows), three 16-pixel columns of which the middle one is plain line cells.
constexpr uint16_t kUnitFirst = 1161, kUnitLast = 1171;
constexpr int kUnitDelta = 16;
// The intermission menu: full-screen scenes 1177 (layout 0x69, nine items) and 1329
// (0x8D, two). The menu box runs x 31-104; the title box starts at x 119 on the same
// rows, so it grows 8.
constexpr int kMenuDelta = 8;

constexpr uint32_t kClamp = 0x80217B1C, kClampLoad = 0x801C9D70, kClampLoadWord = 0x8C847B1C;   // lw $a0, 0x7B1C($a0)
constexpr uint32_t kClampUnit = 0xEE;                                    // D_80217B1C[0]: the menu's x limit
// The intermission panel's FILLRECT in layouts 0x69 and 0x8D: (33,41)-(103,183) and (33,41)-(103,71).
constexpr uint32_t kPanels[][2] = {{0x800C7BB8, 0xF619C2DC}, {0x800C7C18, 0xF619C11C}};
constexpr uint32_t kPanelEdge = 103;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}
void put(uint8_t* rdram, uint32_t address, uint32_t value) { std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4); }

std::mutex mutex;
uint64_t frame_number = 0;
struct Seen { Window window; uint64_t frame; };
std::vector<Seen> seen;

bool recent(uint64_t frame) { return frame && frame + 1 >= frame_number; }

// The FILLRECT commands in [begin, end) starting at x0 (10.2), or any when x0 < 0: right edge moved by delta.
int widen_fills(uint8_t* rdram, uint32_t begin, uint32_t end, int x0, int delta, int only_edge = -1, int only_width = -1) {
    int changed = 0;
    for (uint32_t p = begin; p + 8 <= end; p += 8) {
        const uint32_t w0 = word(rdram, p);
        if ((w0 >> 24) != 0xF6) continue;
        const uint32_t w1 = word(rdram, p + 4), xl = (w1 >> 12) & 0xFFF, xh = (w0 >> 12) & 0xFFF;
        if (x0 >= 0 && std::abs(int(xl) - x0) > 2) continue;
        if (only_edge >= 0 && int(xh) / 4 != only_edge && int(xh) / 4 != only_edge - 1) continue;
        if (only_width >= 0 && std::abs(int(xh - xl) / 4 - only_width) > 1) continue;
        const uint32_t moved = std::min<uint32_t>(xh + uint32_t(delta) * 4, 0xFFF);
        put(rdram, p, (w0 & ~(0xFFFu << 12)) | moved << 12);
        ++changed;
    }
    return changed;
}
}

std::optional<Columns> columns(uint16_t scene) {
    if (scene >= kUnitFirst && scene <= kUnitLast) return Columns{0, 176, 16, 32, 48, kUnitDelta};
    if (scene == 1177) return Columns{32, 176, 80, 96, 112, kMenuDelta};
    if (scene == 1329) return Columns{32, 64, 80, 96, 112, kMenuDelta};
    return std::nullopt;
}

void frame_start(uint8_t* rdram) {
    {
        std::lock_guard lock(mutex);
        ++frame_number;
    }
    // Where a unit menu may open: its right edge stays on screen, 16 wider.
    if (word(rdram, kClampLoad) == kClampLoadWord && word(rdram, kClamp) == kClampUnit && word(rdram, kClamp + 4) == 0xDE)
        put(rdram, kClamp, kClampUnit - kUnitDelta);
    for (const auto& [address, original] : kPanels)
        if (word(rdram, address) == original)
            put(rdram, address, (original & ~(0xFFFu << 12)) | (kPanelEdge + kMenuDelta) * 4 << 12);
}

void frame_drawn(uint8_t* rdram, uint32_t dl_begin, uint32_t dl_end, uint16_t scene, int origin_x, int origin_y) {
    const auto c = columns(scene);
    if (!c || dl_end <= dl_begin) return;
    float y0 = 1e9f, y1 = -1e9f;
    for (uint32_t p = dl_begin; p + 24 <= dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if ((op != 0xE4 && op != 0xE5) || (word(rdram, p + 8) >> 24) != 0xE1 || (word(rdram, p + 16) >> 24) != 0xF1) continue;
        const uint32_t w1 = word(rdram, p + 4);
        uint32_t xl = (w1 >> 12) & 0xFFF, xh = (w0 >> 12) & 0xFFF;
        const int x = int(std::lround(xl / 4.f)) - origin_x, y = int(std::lround((w1 & 0xFFF) / 4.f)) - origin_y;
        y0 = std::min(y0, (w1 & 0xFFF) / 4.f); y1 = std::max(y1, (w0 & 0xFFF) / 4.f);
        if (y >= c->top && y <= c->bottom) {
            if (x == c->stretch) {
                // Drawn wider at a step that still covers the 16 texels once.
                xh += uint32_t(c->delta) * 4;
                const uint32_t f1 = word(rdram, p + 20);
                const int dsdx = int16_t(f1 >> 16);
                const int scaled = int(std::lround(dsdx * 16.0 / (16 + c->delta)));
                put(rdram, p + 20, uint32_t(uint16_t(scaled)) << 16 | (f1 & 0xFFFF));
            } else if (x >= c->shift && x < c->shift_end) {
                xl += uint32_t(c->delta) * 4; xh += uint32_t(c->delta) * 4;
            }
            xl = std::min<uint32_t>(xl, 0xFFF); xh = std::min<uint32_t>(xh, 0xFFF);
            put(rdram, p, (w0 & ~(0xFFFu << 12)) | xh << 12);
            put(rdram, p + 4, (w1 & ~(0xFFFu << 12)) | xl << 12);
        }
        p += 16;
    }
    if (y1 < y0) return;
    std::lock_guard lock(mutex);
    Window window{};
    if (scene >= kUnitFirst && scene <= kUnitLast) {
        // The panel runs to the frame's x + 43 (801E3EF0's width 0x2C), now 16 further.
        window = {float(origin_x), y0, float(origin_x + 43 + kUnitDelta - 1), y1};
    } else {
        // The box's inner line at x 103, moved over.
        window = {float(origin_x + 31), float(origin_y + 39), float(origin_x + 103 + kMenuDelta - 2), float(origin_y + (scene == 1177 ? 184 : 72))};
    }
    std::erase_if(seen, [&](const Seen& s) { return !recent(s.frame) || (s.window.x0 == window.x0 && s.window.y0 == window.y0); });
    seen.push_back({window, frame_number});
}

void callback_drawn(uint8_t* rdram, uint32_t function, uint32_t dl_begin, uint32_t dl_end) {
    if (dl_end <= dl_begin) return;
    // The tactical overlay is the one loaded (its clamp load in place).
    const bool tactical = word(rdram, kClampLoad) == kClampLoadWord;
    switch (function) {
    case 0x801E3EF0: case 0x801E3F98:       // the unit menu's dark panel (D_800C8988 entries 7-17, 19)
        if (tactical) widen_fills(rdram, dl_begin, dl_end, -1, kUnitDelta);
        break;
    case 0x801E4160:                        // the green cursor bar, 40 wide on the unit menu only
        if (tactical) widen_fills(rdram, dl_begin, dl_end, -1, kUnitDelta, -1, 40);
        break;
    case 0x801C45F4:                        // the intermission menu's bar, (33, y)-(103, y + 15)
        widen_fills(rdram, dl_begin, dl_end, 33 * 4, kMenuDelta, int(kPanelEdge));
        break;
    default: break;
    }
}

std::vector<Window> windows() {
    std::lock_guard lock(mutex);
    std::vector<Window> out;
    for (const auto& s : seen) if (recent(s.frame)) out.push_back(s.window);
    return out;
}
}
