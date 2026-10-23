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
// The intermission panels' FILLRECTs in the resident display lists of layouts 0x69 and 0x8D:
// {address, the ROM's word, ours}. The menu panels, (33,41)-(103,183) and (33,41)-(103,71),
// 8 wider (right edge 111) and a row taller (bottom 199 and 87); the nine-item menu's chapter
// panel (34,193)-(287,207) 16 lower.
constexpr uint32_t kPanels[][3] = {
    {0x800C7BB8, 0xF619C2DC, 0xF61BC31C},
    {0x800C7C18, 0xF619C11C, 0xF61BC15C},
    {0x800C7BC8, 0xF647C33C, 0xF647C37C}, {0x800C7BCC, 0x00088304, 0x00088344},
};
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

// Always one row taller, for the 返回标题画面 row:
// - the blank-tile menu's frame (scene 1198, 80 x 128: a 16-pixel border row at the top and
//   at y 112, six interior rows; no other window draws it; map_menu.hpp);
// - the intermission's full-screen frames (intermission_row.hpp): in 1177 the nine-item
//   box's last interior row (y 160, the box's columns 16-96 the only cells there) and below
//   it its bottom edge, which is also the chapter box's top, and the chapter box; in 1329
//   the two-item box's interior row (y 48) and its bottom edge (y 64, columns 16-96: the
//   info box sits on that row further right).
constexpr uint16_t kMapMenuScene = 1198, kIntermissionFull = 1177, kIntermissionTwo = 1329;
constexpr int kRow = 16;
std::optional<Rows> rows_spec(uint16_t scene) {
    if (scene == kMapMenuScene) return Rows{0, 64, 96, 112, 128, kRow};
    if (scene == kIntermissionFull) return Rows{0, 320, 160, 176, 224, kRow};
    if (scene == kIntermissionTwo) return Rows{16, 96, 48, 64, 80, kRow};
    return std::nullopt;
}
std::optional<Rows> rows(uint16_t scene) { return rows_spec(scene); }
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
    for (const auto& [address, original, ours] : kPanels)
        if (word(rdram, address) == original) put(rdram, address, ours);
}

void frame_drawn(uint8_t* rdram, uint32_t dl_begin, uint32_t dl_end, uint16_t scene, int origin_x, int origin_y) {
    const auto c = columns(scene);
    const auto r = rows(scene);
    if ((!c && !r) || dl_end <= dl_begin) return;
    float y0 = 1e9f, y1 = -1e9f;
    for (uint32_t p = dl_begin; p + 24 <= dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if ((op != 0xE4 && op != 0xE5) || (word(rdram, p + 8) >> 24) != 0xE1 || (word(rdram, p + 16) >> 24) != 0xF1) continue;
        const uint32_t w1 = word(rdram, p + 4);
        uint32_t xl = (w1 >> 12) & 0xFFF, xh = (w0 >> 12) & 0xFFF, yl = w1 & 0xFFF, yh = w0 & 0xFFF;
        // The cell's place in the scene as the ROM has it; both stretches test that.
        const int x = int(std::lround(xl / 4.f)) - origin_x, y = int(std::lround(yl / 4.f)) - origin_y;
        y0 = std::min(y0, yl / 4.f); y1 = std::max(y1, yh / 4.f);
        uint32_t f1 = word(rdram, p + 20);
        if (c && y >= c->top && y <= c->bottom) {
            if (x == c->stretch) {
                // Drawn wider at a step that still covers the 16 texels once.
                xh += uint32_t(c->delta) * 4;
                const int dsdx = int16_t(f1 >> 16);
                f1 = uint32_t(uint16_t(int(std::lround(dsdx * 16.0 / (16 + c->delta))))) << 16 | (f1 & 0xFFFF);
            } else if (x >= c->shift && x < c->shift_end) {
                xl += uint32_t(c->delta) * 4; xh += uint32_t(c->delta) * 4;
            }
        }
        if (r && x >= r->left && x <= r->right) {
            if (y == r->stretch) {
                // The same down the scene: the stretched cell sampled at 16 / (16 + delta) in t.
                yh += uint32_t(r->delta) * 4;
                const int dtdy = int16_t(f1 & 0xFFFF);
                f1 = (f1 & 0xFFFF0000u) | (uint32_t(int(std::lround(dtdy * 16.0 / (16 + r->delta)))) & 0xFFFF);
            } else if (y >= r->shift && y < r->shift_end) {
                yl += uint32_t(r->delta) * 4; yh += uint32_t(r->delta) * 4;
            }
        }
        xl = std::min<uint32_t>(xl, 0xFFF); xh = std::min<uint32_t>(xh, 0xFFF);
        yl = std::min<uint32_t>(yl, 0xFFF); yh = std::min<uint32_t>(yh, 0xFFF);
        put(rdram, p, (w0 & 0xFF000000u) | xh << 12 | yh);
        put(rdram, p + 4, (w1 & 0xFF000000u) | xl << 12 | yl);
        put(rdram, p + 20, f1);
        p += 16;
    }
    if (y1 < y0) return;
    std::lock_guard lock(mutex);
    Window window{};
    if (scene == kMapMenuScene) {
        // The text's room: the panel's inner edge at x + 75, down to the eighth row.
        window = {float(origin_x + 4), float(origin_y + 4), float(origin_x + 75), float(origin_y + 4 + 119 + kRow)};
    } else if (scene >= kUnitFirst && scene <= kUnitLast) {
        // The panel runs to the frame's x + 43 (801E3EF0's width 0x2C), now 16 further.
        window = {float(origin_x), y0, float(origin_x + 43 + kUnitDelta - 1), y1};
    } else {
        // The box's inner line at x 103, moved over, down to its added row.
        window = {float(origin_x + 31), float(origin_y + 39), float(origin_x + 103 + kMenuDelta - 2),
                  float(origin_y + (scene == kIntermissionFull ? 184 : 72) + kRow)};
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
    case 0x8009CDF0:                        // the blank-tile menu's dark panel (its window entry's own), one row taller
        if (tactical)
            for (uint32_t p = dl_begin; p + 8 <= dl_end; p += 8) {
                const uint32_t w0 = word(rdram, p);
                if ((w0 >> 24) != 0xF6) continue;
                const uint32_t w1 = word(rdram, p + 4);
                // The menu's own panel: 119 tall (its y + 4 to y + 123), unlike the other windows'.
                if (std::abs(int((w0 & 0xFFF) / 4) - int((w1 & 0xFFF) / 4) - 119) > 1) continue;
                put(rdram, p, (w0 & ~0xFFFu) | std::min<uint32_t>((w0 & 0xFFF) + uint32_t(kRow) * 4, 0xFFF));
            }
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
