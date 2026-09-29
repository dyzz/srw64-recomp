#include "wide_map.hpp"
#include "game_frame.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace srw64::wide_map {
namespace {
// Game thread.
int32_t view_extra = 0;       // Wv - 320
float view_left = 0;          // the view's x = 0 in the picture
bool drawing_wide = false;    // between begin and end
// When begin last placed a draw. The frame boundary runs far more often than the game
// builds a display list, so "shown" means within the last fifth of a second.
std::atomic<int64_t> last_shown{INT64_MIN / 2};   // steady_clock nanoseconds; the debug status reads it too
int64_t now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::atomic<uint64_t> placed{};           // draws placed in the view, for the debug status
std::atomic<int32_t> debug_map_width{}, debug_scroll{};
// Game thread writes, the graphics thread takes.
constexpr int kOriginal = 1, kBattle = 2, kView = 4;
std::atomic<int> frame_marks{};
std::atomic<float> view_band_left{0}, view_band_right{0};
std::mutex shape_mutex;
std::map<uint64_t, Shape> shapes;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}
// Appends RT64 extended commands (rt64_extended_gbi.h, opcode 0x64 enabled by graphics.cpp
// for every display list) at the display list cursor and moves the cursor past them.
void append(uint8_t* rdram, int32_t cursor, std::initializer_list<uint32_t> words) {
    const uint32_t list = word(rdram, uint32_t(cursor));
    uint32_t at = list & 0x1FFFFFFF;
    for (const uint32_t value : words) { std::memcpy(rdram + at, &value, 4); at += 4; }
    const uint32_t next = list + uint32_t(words.size() * 4);
    std::memcpy(rdram + (uint32_t(cursor) & 0x1FFFFFFF), &next, 4);
}
constexpr uint32_t kExtended = 0x64000000;
constexpr uint32_t kSetScissor = 0x05, kSetRectAlign = 0x06, kPushScissor = 0x17, kPopScissor = 0x18, kSetRectAspect = 0x33;
constexpr uint32_t kAspectAuto = 0, kAspectStretch = 1;
constexpr uint32_t kOriginLeft = 0x000, kOriginRight = 0x400, kOriginNone = 0x800;
uint32_t pair(int32_t high, int32_t low) { return (uint32_t(high) & 0xFFFF) << 16 | (uint32_t(low) & 0xFFFF); }
}  // namespace

void frame(uint8_t* rdram) {
    const float picture = frame::wide ? float(frame::picture_width) : frame::kWidth;
    // The map's width in pixels (800945D4's layout); a smaller view than the picture is
    // centred in it, a map no wider than the original keeps the original's view.
    const int32_t map = int32_t(word(rdram, 0x80172EC4));
    debug_map_width = map; debug_scroll = int32_t(word(rdram, 0x8010F5D4));
    const int32_t view = std::clamp(int32_t(std::floor(picture)), int32_t(frame::kWidth), std::max(map, int32_t(frame::kWidth)));
    view_extra = view - int32_t(frame::kWidth);
    view_left = (picture - float(view)) / 2;
    // A camera placed for another width (the stage's opening camera comes before the map's
    // width is known; the window can change shape) can leave map-less space at the right
    // edge until the cursor moves: back into the view's range (801FFCB0's clamp). Within
    // 32 pixels is left alone, for the script's shakes (3D36) that the original lets through.
    const int32_t lowest = view - map;
    if (map_shown() && map >= view && int32_t(word(rdram, 0x8010F5D4)) < lowest - 32) {
        const uint32_t clamped = uint32_t(lowest);
        std::memcpy(rdram + (0x8010F5D4 & 0x1FFFFFFF), &clamped, 4);
    }
}
int32_t extra() { return view_extra; }
bool map_shown() { return now() - last_shown < 200'000'000; }
uint64_t placed_draws() { return placed; }
int32_t map_width_seen() { return debug_map_width; }
int32_t scroll_seen() { return debug_scroll; }

bool begin(uint8_t* rdram, int32_t cursor) {
    if (view_extra <= 0 || drawing_wide) return false;
    // Quarter pixels (10.2): the view's edges in the picture. A scissor's right edge counts
    // from the picture's right edge (origin RIGHT), so RT64 still sees a 320-wide scissor.
    const int32_t left = int32_t(std::lround(view_left * 4));
    append(rdram, cursor, {
        kExtended | kPushScissor, 0,
        kExtended | kSetScissor, kOriginLeft << 2 | kOriginRight << 14, pair(left, 0), pair(-left, 240 * 4),
        kExtended | kSetRectAlign, kOriginLeft | kOriginLeft << 12, pair(left, 0), pair(left, 0)});
    drawing_wide = true;
    last_shown = now();
    ++placed;
    view_band_left = view_left;
    view_band_right = view_left + frame::kWidth + float(view_extra);
    frame_marks |= kView;
    return true;
}
void end(uint8_t* rdram, int32_t cursor) {
    if (!drawing_wide) return;
    append(rdram, cursor, {
        kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, 0, 0,
        kExtended | kPopScissor, 0});
    drawing_wide = false;
}
float view_offset() { return drawing_wide ? view_left : -1.f; }

void mark_original() { frame_marks |= kOriginal; }
namespace {
std::atomic<int64_t> last_battle{INT64_MIN / 2};
}
void mark_battle() { frame_marks |= kBattle; last_battle = now(); }
bool battle_shown() { return now() - last_battle < 200'000'000; }
void open_sides(uint8_t* rdram, int32_t cursor) {
    append(rdram, cursor, {kExtended | kPushScissor, 0,
        kExtended | kSetScissor, kOriginLeft << 2 | kOriginRight << 14, pair(0, 0), pair(0, 240 * 4)});
}
void offset_rects(uint8_t* rdram, int32_t cursor, float pixels) {
    const int32_t offset = int32_t(std::lround(pixels * 4));
    append(rdram, cursor, {kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, pair(offset, 0), pair(offset, 0)});
}
bool wipe_begin(uint8_t* rdram, int32_t cursor) {
    if (!frame::wide || float(frame::picture_width) <= frame::kWidth + 0.5f) return false;
    open_sides(rdram, cursor);
    append(rdram, cursor, {kExtended | kSetRectAlign, kOriginLeft | kOriginLeft << 12, 0, 0});
    return true;
}
void wipe_end(uint8_t* rdram, int32_t cursor, uint32_t begin) {
    const float picture = frame::picture_width, factor = picture / frame::kWidth;
    const uint32_t end = word(rdram, uint32_t(cursor)) & 0x1FFFFFFF;
    // FILLRECT: F6, lower right x/y then upper left x/y, 12-bit 10.2 each.
    const auto scaled = [&](uint32_t x, bool right) {
        const float pixels = x / 4.f;
        if (!right && pixels <= 1.f) return 0u;
        if (right && pixels >= frame::kWidth - 1.f) return uint32_t(std::lround(picture * 4));
        return uint32_t(std::lround(pixels * factor * 4));
    };
    for (uint32_t at = begin & 0x1FFFFFFF; at + 8 <= end; at += 8) {
        uint32_t w0, w1;
        std::memcpy(&w0, rdram + at, 4); std::memcpy(&w1, rdram + at + 4, 4);
        if (w0 >> 24 != 0xF6) continue;
        w0 = (w0 & 0xFF000FFF) | std::min(scaled((w0 >> 12) & 0xFFF, true), 0xFFFu) << 12;
        w1 = (w1 & 0xFF000FFF) | std::min(scaled((w1 >> 12) & 0xFFF, false), 0xFFFu) << 12;
        std::memcpy(rdram + at, &w0, 4); std::memcpy(rdram + at + 4, &w1, 4);
    }
    close_sides(rdram, cursor);
}
void close_sides(uint8_t* rdram, int32_t cursor) {
    append(rdram, cursor, {kExtended | kSetRectAlign, kOriginNone | kOriginNone << 12, 0, 0, kExtended | kPopScissor, 0});
}
void stretch_rects(uint8_t* rdram, int32_t cursor, bool on) {
    append(rdram, cursor, {kExtended | kSetRectAspect, on ? kAspectStretch : kAspectAuto});
}
Shape take_shape() {
    const int marks = frame_marks.exchange(0);
    Shape shape;
    shape.battle = marks & kBattle;
    if (marks & kOriginal) shape.fill = Shape::Fill::original;
    else if (marks & kView) shape = {Shape::Fill::view, view_band_left, view_band_right, false};
    else shape.fill = Shape::Fill::whole;
    return shape;
}
void queue_shape(uint64_t workload, Shape shape) {
    std::lock_guard lock(shape_mutex);
    shapes[workload] = shape;
    while (shapes.size() > 128) shapes.erase(shapes.begin());
}
Shape frame_shape(uint64_t workload) {
    std::lock_guard lock(shape_mutex);
    const auto found = shapes.find(workload);
    return found != shapes.end() ? found->second : Shape{};
}
}  // namespace srw64::wide_map

extern "C" int32_t srw64_map_x(int32_t original, int32_t halves) {
    return original + srw64::wide_map::extra() * halves / 2;
}
extern "C" int32_t srw64_map_x_float(uint32_t bits, int32_t halves) {
    float value;
    std::memcpy(&value, &bits, 4);
    value += float(srw64::wide_map::extra() * halves) / 2;
    std::memcpy(&bits, &value, 4);
    return int32_t(bits);
}
