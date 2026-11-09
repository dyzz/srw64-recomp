#pragma once
// A move range wider than the original's grid (docs/gameplay/base-fixes.md, BUG10).
//
// The tactical overlay works out a unit's move range on 31x31 tables centred on the unit,
// with 15 layers per square: exactly enough for move 15, and each table is followed
// directly by live data. Up to 15 the original runs untouched. Beyond it the host works
// on a grid sized to the move, and hands the original's tables only the 31x31 window
// around the unit, so the code that reads them sees what it always saw.
//
// This header is the part that needs no guest: it is what tests/native_move_range.cpp
// compares, square by square, with a transcription of the original's search.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace srw64::move_range {
inline constexpr int kWindow = 15, kWindowSize = 2 * kWindow + 1;
// 801CC290 and 801C264C read a walking unit's square as a signed byte with the unit at 15.
inline constexpr int kMaxRadius = 127 - kWindow;
inline constexpr uint8_t kNone = 0, kInside = 1, kStop = 2, kPass = 3;
// The table at 802178A0: the order the original's searches try the four neighbours.
inline constexpr int kStepX[4] = {-1, 0, 1, 0}, kStepY[4] = {0, -1, 0, 1};

// What a square holds for this unit: 801E213C's terrain, its entry cost, and whether
// 801EB4C0 (mode 1) keeps the unit out. A blocked square's terrain is 0xFF.
struct Sample {
    uint8_t terrain, cost;
    bool blocked;
};

class Grid {
public:
    int radius() const { return radius_; }
    int size() const { return size_; }
    bool contains(int dx, int dy) const { return std::abs(dx) <= radius_ && std::abs(dy) <= radius_; }
    int index(int dx, int dy) const { return (dy + radius_) * size_ + dx + radius_; }
    uint8_t reach(int dx, int dy) const { return contains(dx, dy) ? reach_[index(dx, dy)] : kNone; }
    uint8_t cost(int dx, int dy) const { return contains(dx, dy) ? cost_[index(dx, dy)] : 0xFF; }
    uint8_t terrain(int dx, int dy) const { return contains(dx, dy) ? terrain_[index(dx, dy)] : 0xFF; }
    uint8_t reach_at(int cell) const { return reach_[cell]; }
    uint8_t cost_at(int cell) const { return cost_[cell]; }

    // 801C3E50, up to the search: every square within `move` of the unit, by the map's
    // playable bounds, is looked at. (x, y) is the unit's position in map pixels.
    template <class Sampler>
    void survey(int move, int x, int y, int map_width, int map_height, Sampler&& sample) {
        reset(std::min(move, kMaxRadius));
        for (int dy = -radius_; dy <= radius_; ++dy) {
            const int span = radius_ - std::abs(dy);
            const int py = y + dy * 16;
            if (py < 0x20 || py > map_height - 0x30) continue;
            for (int dx = -span; dx <= span; ++dx) {
                const int px = x + dx * 16;
                if (px < 0x20 || px > map_width - 0x30) continue;
                const Sample s = sample(dx, dy);
                const int i = index(dx, dy);
                terrain_[i] = s.terrain;
                cost_[i] = s.cost;
                reach_[i] = s.blocked ? kNone : kInside;
            }
        }
    }

    // 801C3B0C's result. It tries every way with what is left of the move and marks a
    // square stoppable once it is entered with nothing owing; a unit with none left does
    // not move on. That is a shortest-path search with those two limits.
    void flood(int move) {
        move = std::min(move, kMaxRadius);
        const int start = index(0, 0);
        reach_[start] = kNone;
        distance_.assign(cells(), kFar);
        heap_.clear();
        distance_[start] = 0;
        push(0, start);
        while (!heap_.empty()) {
            const auto [d, cell] = pop();
            if (d != distance_[cell] || d >= move) continue;
            for (int way = 0; way < 4; ++way) {
                const int next = neighbour(cell, way);
                if (next < 0 || reach_[next] != kInside) continue;
                const int total = d + cost_[next];
                if (total > move || total >= distance_[next]) continue;
                distance_[next] = uint16_t(total);
                push(total, next);
            }
        }
        for (int i = 0; i < cells(); ++i)
            if (i != start && distance_[i] != kFar) reach_[i] = kStop;
    }

    // 801C3E50's tail: squares with someone on them that the unit can only pass through.
    template <class Occupied>
    void mark_passes(Occupied&& pass_only) {
        for (int dy = -radius_; dy <= radius_; ++dy)
            for (int dx = -radius_; dx <= radius_; ++dx)
                if (reach_[index(dx, dy)] == kStop && pass_only(dx, dy)) reach_[index(dx, dy)] = kPass;
    }

    // What the original's tables would hold in their 31x31 around the unit.
    template <class Store>
    void store_window(Store&& store) const {
        for (int gy = 0; gy < kWindowSize; ++gy)
            for (int gx = 0; gx < kWindowSize; ++gx)
                store(gx, gy, reach(gx - kWindow, gy - kWindow), cost(gx - kWindow, gy - kWindow), terrain(gx - kWindow, gy - kWindow));
    }

    // 801C58E0 then 801C5C48: of the ways to (tx, ty) with the lowest total cost, the one
    // found first trying left, up, right, down. False, with no path, if there is none.
    bool find_path(int budget, int tx, int ty) {
        mask_.assign(cells(), 0);
        if (!contains(tx, ty) || (tx == 0 && ty == 0)) return false;
        const int start = index(0, 0), target = index(tx, ty);
        if (!walkable(target)) return false;
        distance_.assign(cells(), kFar);
        heap_.clear();
        distance_[start] = 0;
        push(0, start);
        while (!heap_.empty()) {
            const auto [d, cell] = pop();
            if (d != distance_[cell] || d >= budget) continue;
            for (int way = 0; way < 4; ++way) {
                const int next = neighbour(cell, way);
                if (next < 0 || !walkable(next)) continue;
                const int total = d + cost_[next];
                if (total > budget || total >= distance_[next]) continue;
                distance_[next] = uint16_t(total);
                push(total, next);
            }
        }
        if (distance_[target] == kFar) return false;
        // The squares that lie on some cheapest way to the target.
        on_way_.assign(cells(), 0);
        on_way_[target] = 1;
        stack_.assign(1, target);
        while (!stack_.empty()) {
            const int cell = stack_.back();
            stack_.pop_back();
            if (cell == start) continue;
            for (int way = 0; way < 4; ++way) {
                const int before = neighbour(cell, way);
                if (before < 0 || on_way_[before] || !leads_to(before, cell, budget)) continue;
                on_way_[before] = 1;
                stack_.push_back(before);
            }
        }
        // From the unit, the first way that stays on one. Cost-free squares can make a
        // circle; a square is tried once, so that cannot go round for ever.
        on_way_[start] = 2;
        stack_.assign(1, start);
        ways_.assign(1, 0);
        while (!stack_.empty() && stack_.back() != target) {
            const int cell = stack_.back();
            bool advanced = false;
            while (ways_.back() < 4 && !advanced) {
                const int next = neighbour(cell, ways_.back()++);
                if (next < 0 || on_way_[next] != 1 || !leads_to(cell, next, budget)) continue;
                on_way_[next] = 2;
                stack_.push_back(next);
                ways_.push_back(0);
                advanced = true;
            }
            if (!advanced) { stack_.pop_back(); ways_.pop_back(); }
        }
        for (const int cell : stack_)
            if (cell != start) mask_[cell] = 1;
        return !stack_.empty();
    }

    // 801C5810: the unit has reached (dx, dy); on to the next square of the path, trying
    // left, up, right, down. True when there is none: the walk is over.
    bool advance(int& dx, int& dy) {
        if (contains(dx, dy)) mask_[index(dx, dy)] = 0;
        for (int way = 0; way < 4; ++way) {
            const int nx = dx + kStepX[way], ny = dy + kStepY[way];
            if (!contains(nx, ny) || mask_[index(nx, ny)] != 1) continue;
            dx = nx; dy = ny;
            return false;
        }
        return true;
    }

    // 80202848 and 80202A84: the stoppable square `distance` likes best, in the original's
    // scan order, of those `accept` lets through. The original compares the distance cut
    // to a whole number with the best one kept whole, and starts from 10000.
    template <class Accept, class Distance>
    bool pick_nearest(Accept&& accept, Distance&& distance, int& best_x, int& best_y) const {
        float best = 10000.0f;
        bool found = false;
        for (int dy = -radius_; dy <= radius_; ++dy)
            for (int dx = -radius_; dx <= radius_; ++dx) {
                if (reach_[index(dx, dy)] != kStop || !accept(dx, dy)) continue;
                const float d = distance(dx, dy);
                if (std::trunc(d) < best) { best = d; best_x = dx; best_y = dy; found = true; }
            }
        return found;
    }

    bool on_path(int dx, int dy) const { return contains(dx, dy) && mask_[index(dx, dy)] == 1; }

private:
    static constexpr uint16_t kFar = 0xFFFF;
    int cells() const { return size_ * size_; }
    void reset(int radius) {
        radius_ = radius;
        size_ = 2 * radius + 1;
        reach_.assign(cells(), kNone);
        cost_.assign(cells(), 0xFF);
        terrain_.assign(cells(), 0xFF);
        mask_.assign(cells(), 0);
    }
    int neighbour(int cell, int way) const {
        const int x = cell % size_ + kStepX[way], y = cell / size_ + kStepY[way];
        return x < 0 || y < 0 || x >= size_ || y >= size_ ? -1 : y * size_ + x;
    }
    // 801C58E0 walks squares the fill stopped or passed on, never back onto the unit.
    bool walkable(int cell) const { return reach_[cell] == kStop || reach_[cell] == kPass; }
    bool leads_to(int from, int to, int budget) const {
        return distance_[from] != kFar && distance_[from] < budget && distance_[from] + cost_[to] == distance_[to];
    }
    // Keeps the search free of allocation once the vectors have grown.
    void push(int d, int cell) {
        heap_.push_back(uint32_t(d) << 16 | uint32_t(cell));
        std::push_heap(heap_.begin(), heap_.end(), std::greater<>());
    }
    struct Entry { int d, cell; };
    Entry pop() {
        std::pop_heap(heap_.begin(), heap_.end(), std::greater<>());
        const uint32_t e = heap_.back();
        heap_.pop_back();
        return {int(e >> 16), int(e & 0xFFFF)};
    }

    int radius_ = 0, size_ = 1;
    std::vector<uint8_t> reach_, cost_, terrain_, mask_, on_way_, ways_;
    std::vector<uint16_t> distance_;
    std::vector<int> stack_;
    std::vector<uint32_t> heap_;
};

// The grid of the last fill wider than the original's, or null if the original's tables
// hold the range (the debug interface and the farthest squares read it).
const Grid* active();
void configure();
}  // namespace srw64::move_range
