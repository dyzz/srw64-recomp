#include "move_jump.hpp"
#include "game_hooks.hpp"
#include "graphics.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include "json/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace srw64::move_jump {
namespace {
using namespace guest;
using json = nlohmann::json;
// Tactical overlay (load_000AB160), all static reading (docs/native/move-jump.md).
constexpr uint32_t kState = 0x80172EB0, kSub = 0x80172EB2;                // 0xC / 0: choosing a destination
constexpr uint32_t kAnchorX = 0x80172ECC, kAnchorY = 0x80172ED0;          // the unit's square, map pixels
constexpr uint32_t kTargetX = 0x80172EB4, kTargetY = 0x80172EB8;          // cursor target, f32
constexpr uint32_t kCursorX = 0x80102308, kCursorY = 0x8010230C;          // cursor object 0x35, f32
constexpr uint32_t kVelocityX = 0x8010232C, kVelocityY = 0x80102330;
constexpr uint32_t kScrollX = 0x8010F5D4, kScrollY = 0x8010F5D8;          // screen = map + scroll
constexpr uint32_t kMapWidth = 0x80172EC4, kMapHeight = 0x80172EC8;       // pixels
constexpr uint32_t kReach = 0x80227BD0, kCost = 0x80227328;               // 31x31 around the unit
constexpr uint32_t kHeld = 0x800F97D0, kPressed = 0x80178A08, kRepeat = 0x801612E0;
constexpr uint16_t kA = 0x8000, kB = 0x4000, kR = 0x0010, kUp = 0x0800, kDown = 0x0400, kLeft = 0x0200, kRight = 0x0100;
constexpr uint8_t kStop = 2, kPass = 3;   // 801C3E50: a square the unit can stop on / only pass (a friend on it)
constexpr int kSize = 31, kCentre = 15, kCells = kSize * kSize;
constexpr unsigned kCursorSound = 0xB9;

std::ofstream log;
std::mutex mutex;                       // the snapshot below, for the debug thread
json snapshot = {{"select_vi", nullptr}, {"highlight_vi", nullptr}, {"active", false}, {"jumps", 0}};
bool active = false;
int32_t anchor[2]{};
std::vector<int> farthest;              // cells, gy * 31 + gx
std::array<bool, kCells> is_farthest{};

void record(json row) {
    if (!log.is_open()) return;
    row["schema"] = "srw64.move-jump-event.v1";
    row["vi"] = srw64_current_vi();
    log << row.dump() << '\n';
    log.flush();
}
int32_t s32(const uint8_t* ram, uint32_t p) { return int32_t(read(ram, p, 4)); }
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
int cell_of(const uint8_t* ram) {
    const int gx = (int(f32(ram, kCursorX)) >> 4) - (anchor[0] >> 4) + kCentre;
    const int gy = (int(f32(ram, kCursorY)) >> 4) - (anchor[1] >> 4) + kCentre;
    return gx < 0 || gy < 0 || gx >= kSize || gy >= kSize ? -1 : gy * kSize + gx;
}

// The farthest squares: those the unit can stop on that no shortest path runs on through
// to another such square. That is the rim of the range, dead ends, and where the range
// meets the map edge only its two ends; not squares that a lake or a friend merely
// blocks on one side. Shortest paths use the grid's entry costs, as 801C3B0C does.
void find_farthest(const uint8_t* ram) {
    std::array<uint8_t, kCells> reach, cost;
    for (int i = 0; i < kCells; ++i) {
        reach[i] = uint8_t(read(ram, kReach + i, 1));
        cost[i] = uint8_t(read(ram, kCost + i, 1));
    }
    const auto open = [&](int i) { return reach[i] == kStop || reach[i] == kPass; };
    const auto neighbours = [](int i, auto&& visit) {
        const int x = i % kSize, y = i / kSize;
        if (x > 0) visit(i - 1);
        if (x + 1 < kSize) visit(i + 1);
        if (y > 0) visit(i - kSize);
        if (y + 1 < kSize) visit(i + kSize);
    };
    constexpr int kFar = 1 << 20;
    std::array<int, kCells> distance;
    distance.fill(kFar);
    const int centre = kCentre * kSize + kCentre;
    distance[centre] = 0;
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> queue;
    queue.push({0, centre});
    while (!queue.empty()) {
        // Not a structured binding: the lambda below uses d, which clang 14 (the Linux
        // build's) cannot capture from one.
        const int d = queue.top().first, i = queue.top().second;
        queue.pop();
        if (d != distance[i]) continue;
        neighbours(i, [&](int n) {
            if (!open(n) || d + cost[n] >= distance[n]) return;
            distance[n] = d + cost[n];
            queue.push({distance[n], n});
        });
    }
    std::vector<int> order;
    for (int i = 0; i < kCells; ++i) if (distance[i] < kFar) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return distance[a] > distance[b]; });
    std::array<bool, kCells> beyond{};   // a stoppable square lies further along a shortest path
    for (const int i : order)
        neighbours(i, [&](int n) {
            if (open(n) && cost[n] > 0 && distance[n] == distance[i] + cost[n] && (reach[n] == kStop || beyond[n])) beyond[i] = true;
        });
    farthest.clear();
    is_farthest.fill(false);
    for (int i = 0; i < kCells; ++i)
        if (reach[i] == kStop && distance[i] < kFar && !beyond[i]) { farthest.push_back(i); is_farthest[i] = true; }
}

// From a farthest square: the nearest one within 45 degrees of that way, so holding a
// direction walks the rim square by square, corners included; failing that, the one
// least off the line ahead. From anywhere else: the one furthest that way from the
// unit, then nearest the cursor's line.
int next(int from, int dx, int dy) {
    const int fx = from % kSize, fy = from / kSize;
    int best = -1;
    double best_score = 1e18;
    for (const int f : farthest) {
        if (f == from) continue;
        const int vx = f % kSize - fx, vy = f / kSize - fy;
        const int along = vx * dx + vy * dy, across = std::abs(vx * dy - vy * dx);
        if (along <= 0) continue;
        const int reach = (f % kSize - kCentre) * dx + (f / kSize - kCentre) * dy;
        const double score = !is_farthest[from] ? -1000.0 * reach + across
                           : across <= along ? (vx * vx + vy * vy) * 10.0 + across
                           : 1e6 + along + 2.0 * across;
        if (score < best_score) { best_score = score; best = f; }
    }
    return best;
}
// Holding R after moving the cursor off the unit: the farthest square in that direction.
int along_ray(int from) {
    const double rx = from % kSize - kCentre, ry = from / kSize - kCentre;
    int best = -1;
    double best_angle = 1e9, best_length = 0;
    for (const int f : farthest) {
        const double vx = f % kSize - kCentre, vy = f / kSize - kCentre;
        const double angle = std::abs(std::remainder(std::atan2(vy, vx) - std::atan2(ry, rx), 2 * M_PI));
        const double length = std::hypot(vx, vy);
        if (angle < best_angle - 1e-6 || (angle < best_angle + 1e-6 && length > best_length)) {
            best = f; best_angle = std::min(angle, best_angle); best_length = length;
        }
    }
    return best;
}

// What the original does when it puts the cursor somewhere (801C8E20 cycling units):
// cursor and target together, the camera re-centred (801FFCB0) only if it left the screen.
void warp(uint8_t* ram, recomp_context* ctx, int cell) {
    const int32_t px = anchor[0] + (cell % kSize - kCentre) * 16, py = anchor[1] + (cell / kSize - kCentre) * 16;
    put_f32(ram, kCursorX, float(px)); put_f32(ram, kTargetX, float(px));
    put_f32(ram, kCursorY, float(py)); put_f32(ram, kTargetY, float(py));
    write32(ram, kVelocityX, 0); write32(ram, kVelocityY, 0);
    const int32_t sx = s32(ram, kScrollX), sy = s32(ram, kScrollY);
    if (px + sx < 32 || px + sx > 288 || py + sy < 32 || py + sy > 208) {
        write32(ram, kScrollX, uint32_t(std::clamp(152 - px, std::min(0, 320 - s32(ram, kMapWidth)), 0)));
        write32(ram, kScrollY, uint32_t(std::clamp(112 - py, std::min(0, 240 - s32(ram, kMapHeight)), 0)));
    }
    auto call = *ctx;
    call.r29 = int32_t(uint32_t(ctx->r29) - 0x200);
    call.r4 = int32_t(kCursorSound);
    resident_func_8007E8A8(ram, &call);
}

bool choose(uint8_t* ram, recomp_context* ctx) {
    const uint16_t held = uint16_t(read(ram, kHeld, 2)), pressed = uint16_t(read(ram, kPressed, 2));
    if (!(held & kR) || (pressed & (kA | kB))) {
        if (active) record({{"kind", "off"}});
        active = false;
        return false;   // A and B confirm and cancel as always, R held or not
    }
    if (!active || s32(ram, kAnchorX) != anchor[0] || s32(ram, kAnchorY) != anchor[1]) {
        anchor[0] = s32(ram, kAnchorX); anchor[1] = s32(ram, kAnchorY);
        find_farthest(ram);
        active = true;
        const int at = cell_of(ram);
        int to = -1;
        if (at >= 0 && at != kCentre * kSize + kCentre && !is_farthest[at]) to = along_ray(at);
        if (to >= 0) warp(ram, ctx, to);
        record({{"kind", "on"}, {"anchor", {anchor[0], anchor[1]}}, {"farthest", farthest.size()}, {"cursor", at}, {"jump", to}});
    }
    if (farthest.empty()) return false;
    const uint16_t repeat = uint16_t(read(ram, kRepeat, 2));
    const int dx = bool(repeat & kRight) - bool(repeat & kLeft), dy = bool(repeat & kDown) - bool(repeat & kUp);
    if (dx || dy) {
        const int at = cell_of(ram);
        const int to = at < 0 ? -1 : next(at, dx, dy);
        if (to >= 0) {
            warp(ram, ctx, to);
            std::lock_guard lock(mutex);
            snapshot["jumps"] = snapshot["jumps"].get<int>() + 1;
        }
        record({{"kind", "jump"}, {"from", at}, {"to", to}, {"direction", {dx, dy}}});
    }
    return true;   // the directions belong to the farthest squares while R is held
}

bool select(uint8_t* ram, recomp_context* ctx) {
    const bool handled = choose(ram, ctx);
    std::lock_guard lock(mutex);
    snapshot["select_vi"] = srw64_current_vi();
    snapshot["active"] = active;
    snapshot["anchor"] = {anchor[0], anchor[1]};
    snapshot["cursor"] = cell_of(ram);
    snapshot["farthest"] = farthest;
    return handled;
}

// After 801E4760 drew the range (primitive colour, one fill rectangle per square): the
// farthest squares again in a warm colour that breathes, inside the same squares.
void range_drawn(uint8_t* ram, uint32_t list) {
    if (!active || farthest.empty()) return;
    if (int8_t(read(ram, kState, 1)) != 0xC || read(ram, kSub, 1) != 0 || !(read(ram, kHeld, 2) & kR)) return;
    if (s32(ram, kAnchorX) != anchor[0] || s32(ram, kAnchorY) != anchor[1]) return;
    uint32_t dl = read(ram, list, 4);
    if (!valid(dl, uint32_t(8 * (1 + farthest.size())))) return;
    const int32_t sx = s32(ram, kScrollX), sy = s32(ram, kScrollY);
    const double phase = std::fmod(double(srw64_current_vi()) / 72.0, 1.0);
    const uint32_t alpha = uint32_t(std::lround(72 + 56 * (0.5 - 0.5 * std::cos(phase * 2 * M_PI))));
    const auto put = [&](uint32_t w0, uint32_t w1) { write32(ram, dl, w0); write32(ram, dl + 4, w1); dl += 8; };
    put(0xFA000000, 0xFFE05000 | alpha);
    for (const int cell : farthest) {
        int x0 = anchor[0] + sx + (cell % kSize - kCentre) * 16, y0 = anchor[1] + sy + (cell / kSize - kCentre) * 16;
        int x1 = std::min(x0 + 16, 320), y1 = std::min(y0 + 16, 240);
        x0 = std::max(x0, 0); y0 = std::max(y0, 0);
        if (x1 <= x0 || y1 <= y0) continue;
        put(0xF6000000 | uint32_t(x1 & 0x3FF) << 14 | uint32_t(y1 & 0x3FF) << 2, uint32_t(x0 & 0x3FF) << 14 | uint32_t(y0 & 0x3FF) << 2);
    }
    write32(ram, list, dl);
    std::lock_guard lock(mutex);
    snapshot["highlight_vi"] = srw64_current_vi();
}
}

void configure(const std::filesystem::path& output) {
    if (!output.empty()) log.open(output / "move-jump-events.jsonl");
    srw64_game_hooks.move_select = select;
    srw64_game_hooks.move_range_drawn = range_drawn;
}

json state() {
    std::lock_guard lock(mutex);
    return snapshot;
}
}
