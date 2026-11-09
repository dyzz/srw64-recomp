// The wide move range (src/host/move_range.hpp) against a line-by-line transcription of
// the original's search (801C3B0C, 801C58E0, 801C5C48). Up to move 15 the game runs the
// original, so the two have to agree for the wide grid to continue it faithfully; above
// 15 there is no original to compare with and the grid is held to what the range means.
#include "move_range.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <queue>
#include <random>
#include <vector>

using namespace srw64::move_range;

namespace {
constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, -1, 0, 1}, kBack[4] = {2, 3, 0, 1};
constexpr int kCells = 961;

struct World {
    int width = 0, height = 0;   // tiles
    std::vector<uint8_t> costs, blocked, friends;
    int at(int x, int y) const { return y * width + x; }
    bool in_bounds(int px, int py) const { return px >= 0x20 && px <= width * 16 - 0x30 && py >= 0x20 && py <= height * 16 - 0x30; }
};

World make_world(std::mt19937& rng, int width, int height, bool free_squares) {
    World w;
    w.width = width; w.height = height;
    w.costs.resize(width * height); w.blocked.resize(width * height); w.friends.resize(width * height);
    static constexpr uint8_t plain[] = {1, 1, 1, 1, 2, 2, 3, 4, 0xFF};
    for (auto& c : w.costs) c = free_squares && rng() % 5 == 0 ? 0 : plain[rng() % std::size(plain)];
    for (auto& b : w.blocked) b = rng() % 12 == 0;
    for (auto& f : w.friends) f = rng() % 10 == 0;
    return w;
}

// The original's tables and its two searches, as read. An impassable square leaves room
// for more than the original's 15 layers: rem - 0xFF does not fit the signed byte it is
// stored in, so it is never found again and every arrival adds a layer. The original
// writes those past its table (see BUG10); here they just have room.
struct Original {
    uint8_t reach[kCells], cost[kCells], count[kCells], visited[kCells], mask[kCells];
    int8_t layers[64][kCells];
    uint8_t paths[15][kCells];
    unsigned found = 0;
    void clear() {
        std::memset(reach, 0, sizeof reach); std::memset(cost, 0xFF, sizeof cost);
        std::memset(count, 0, sizeof count); std::memset(layers, 0xFF, sizeof layers);
    }
    uint8_t& r(int x, int y) { return reach[y * 31 + x]; }
    static bool on(int x, int y) { return x >= 0 && y >= 0 && x < 31 && y < 31; }
    // 801C3B0C.
    void fill(int x, int y, int rem, int from) {
        for (int way = 0; way < 4; ++way) {
            if (kBack[way] == from) continue;
            const int nx = x + kDx[way], ny = y + kDy[way];
            if (!on(nx, ny)) continue;    // reached only through cost-free squares at the rim
            const int cell = ny * 31 + nx;
            const uint8_t v = reach[cell];
            if (!(v > 0 && rem > 0) || v - 1u >= 2) continue;
            const int left = rem - cost[cell];
            unsigned k = 0;
            for (; k < count[cell]; ++k) if (layers[k][cell] == left) break;
            if (k != count[cell]) continue;
            assert(k < 64 && (k < 15 || left < -128));
            layers[k][cell] = int8_t(left);
            ++count[cell];
            if (left < 0) continue;
            if (reach[cell] == 1) reach[cell] = 2;
            if (left <= 0) continue;
            fill(nx, ny, left, way);
        }
    }
    void start_path() {
        std::memset(visited, 0, sizeof visited); std::memset(mask, 0, sizeof mask);
        std::memset(count, 0, sizeof count); std::memset(layers, 0xFF, sizeof layers);
        std::memset(paths, 0, sizeof paths);
        found = 0;
    }
    // 801C58E0.
    void path(int x, int y, int tx, int ty, int rem, int from) {
        for (int way = 0; way < 4; ++way) {
            if (kBack[way] == from) continue;
            const int nx = x + kDx[way], ny = y + kDy[way];
            if (!on(nx, ny)) continue;
            const int cell = ny * 31 + nx;
            const uint8_t v = reach[cell];
            if (v < 2) continue;
            if (!(visited[cell] == 0 && rem > 0)) continue;
            if (v - 2u >= 2) continue;
            const int left = rem - cost[cell];
            unsigned k = 0;
            bool first = false;
            for (; k < count[cell]; ++k) if (layers[k][cell] == left) break;
            if (k == count[cell]) {
                assert(k < 15);
                layers[k][cell] = int8_t(left);
                ++count[cell];
                first = true;
            }
            if (!(first && left >= 0)) continue;
            ++visited[cell];
            if (ny == ty && nx == tx) {
                assert(found < 15);
                for (int i = 0; i < kCells; ++i) if (visited[i]) paths[found][i] = visited[i];
                ++found;
                visited[cell] = 0;
                continue;
            }
            if (left <= 0) { visited[cell] = 0; continue; }
            path(nx, ny, tx, ty, left, way);
        }
        visited[y * 31 + x] = 0;
    }
    // 801C5C48.
    void pick() {
        unsigned best = 0;
        uint8_t low = 0;
        for (unsigned t = 0; t < found; ++t) {
            uint8_t sum = 0;
            for (int i = 0; i < kCells; ++i) if (paths[t][i]) sum = uint8_t(sum + cost[i]);
            if (t == 0 || sum < low) { low = sum; best = t; }
        }
        for (int i = 0; i < kCells; ++i) if (paths[best][i]) mask[i] = 1;
    }
};

Sample sample_of(const World& w, int ux, int uy, int dx, int dy) {
    const int i = w.at(ux + dx, uy + dy);
    return w.blocked[i] && (dx || dy) ? Sample{0xFF, 0x7F, true} : Sample{uint8_t(i & 0x7F), w.costs[i], false};
}
bool can_stand(const World& w, int ux, int uy) {
    return w.in_bounds(ux * 16, uy * 16);
}

// 801C3E50 around the search, on the same world.
void original_fill(Original& o, const World& w, int ux, int uy, int move) {
    o.clear();
    for (int dx = -move; dx <= move; ++dx)
        for (int dy = -(move - std::abs(dx)); dy <= move - std::abs(dx); ++dy)
            if (w.in_bounds((ux + dx) * 16, (uy + dy) * 16)) o.r(15 + dx, 15 + dy) = 1;
    for (int gy = 0; gy < 31; ++gy)
        for (int gx = 0; gx < 31; ++gx) {
            if (o.r(gx, gy) != 1) continue;
            const Sample s = sample_of(w, ux, uy, gx - 15, gy - 15);
            if (s.blocked) o.r(gx, gy) = 0;
            o.cost[gy * 31 + gx] = s.cost;
        }
    o.r(15, 15) = 0;
    o.fill(15, 15, move, -1);
    for (int gy = 0; gy < 31; ++gy)
        for (int gx = 0; gx < 31; ++gx)
            if (o.r(gx, gy) == 2 && w.friends[w.at(ux + gx - 15, uy + gy - 15)]) o.r(gx, gy) = 3;
}
void host_fill(Grid& g, const World& w, int ux, int uy, int move) {
    g.survey(move, ux * 16, uy * 16, w.width * 16, w.height * 16, [&](int dx, int dy) { return sample_of(w, ux, uy, dx, dy); });
    g.flood(move);
    g.mark_passes([&](int dx, int dy) { return w.friends[w.at(ux + dx, uy + dy)] != 0; });
}

// Every target square in the window: the path the original takes against the host's.
void compare_paths(Original& o, Grid& g, int move, bool exact, std::mt19937& rng) {
    for (int gy = 0; gy < 31; ++gy)
        for (int gx = 0; gx < 31; ++gx) {
            if (o.r(gx, gy) < 2) continue;
            if (rng() % 4) continue;     // a quarter of them: the search is the slow part
            o.start_path();
            o.path(15, 15, gx, gy, move, -1);
            o.pick();
            const bool have = g.find_path(move, gx - 15, gy - 15);
            assert(have == (o.found > 0));
            if (exact) {
                for (int y = 0; y < 31; ++y)
                    for (int x = 0; x < 31; ++x)
                        assert((o.mask[y * 31 + x] == 1) == g.on_path(x - 15, y - 15));
            }
            // Walking it, as 801C5810 does: to the target, one square at a time, at the
            // lowest cost there is.
            int x = 0, y = 0, total = 0, steps = 0;
            while (!g.advance(x, y)) {
                total += o.cost[(y + 15) * 31 + x + 15];
                assert(++steps <= 961);
            }
            assert(x == gx - 15 && y == gy - 15);
            int low = 0;
            for (int i = 0; i < kCells; ++i) if (o.mask[i]) low += o.cost[i];
            assert(total == low);
        }
}

void compare_with_original(std::mt19937& rng, bool free_squares, int trials) {
    for (int t = 0; t < trials; ++t) {
        const World w = make_world(rng, 12 + rng() % 40, 12 + rng() % 40, free_squares);
        // Corners and edges of the playable area come up often: the range is cut there.
        int ux, uy;
        do {
            ux = rng() % 3 == 0 ? 2 + rng() % 2 : rng() % 3 == 0 ? w.width - 3 - int(rng() % 2) : rng() % w.width;
            uy = rng() % 3 == 0 ? 2 + rng() % 2 : rng() % 3 == 0 ? w.height - 3 - int(rng() % 2) : rng() % w.height;
        } while (!can_stand(w, ux, uy));
        const int move = 1 + rng() % 15;
        Original o;
        original_fill(o, w, ux, uy, move);
        Grid g;
        host_fill(g, w, ux, uy, move);
        for (int gy = 0; gy < 31; ++gy)
            for (int gx = 0; gx < 31; ++gx)
                assert(o.r(gx, gy) == g.reach(gx - 15, gy - 15));
        compare_paths(o, g, move, !free_squares, rng);
    }
}

// Above 15: no original. Whether a square is in range is what the costs say.
void check_wide(std::mt19937& rng, int trials) {
    for (int t = 0; t < trials; ++t) {
        const World w = make_world(rng, 40 + rng() % 230, 40 + rng() % 170, false);
        int ux, uy;
        do {
            ux = rng() % 4 == 0 ? 2 + rng() % 2 : rng() % w.width;
            uy = rng() % 4 == 0 ? w.height - 3 - int(rng() % 2) : rng() % w.height;
        } while (!can_stand(w, ux, uy));
        const int move = t == 0 ? 400 : 16 + rng() % 30;
        const int limited = std::min(move, kMaxRadius);
        Grid g;
        host_fill(g, w, ux, uy, move);

        // Bellman-Ford over the whole map, no window, no budget.
        std::vector<int> distance(w.costs.size(), 1 << 20);
        std::vector<uint8_t> open(w.costs.size(), 0);
        for (int y = 0; y < w.height; ++y)
            for (int x = 0; x < w.width; ++x) {
                const int dx = x - ux, dy = y - uy;
                open[w.at(x, y)] = w.in_bounds(x * 16, y * 16) && std::abs(dx) + std::abs(dy) <= limited &&
                                   !sample_of(w, ux, uy, dx, dy).blocked && (dx || dy);
            }
        distance[w.at(ux, uy)] = 0;
        for (bool changed = true; changed;) {
            changed = false;
            for (int y = 0; y < w.height; ++y)
                for (int x = 0; x < w.width; ++x) {
                    const int d = distance[w.at(x, y)];
                    if (d >= limited) continue;
                    for (int way = 0; way < 4; ++way) {
                        const int nx = x + kDx[way], ny = y + kDy[way];
                        if (nx < 0 || ny < 0 || nx >= w.width || ny >= w.height || !open[w.at(nx, ny)]) continue;
                        const int total = d + w.costs[w.at(nx, ny)];
                        if (total <= limited && total < distance[w.at(nx, ny)]) { distance[w.at(nx, ny)] = total; changed = true; }
                    }
                }
        }
        int checked = 0;
        for (int y = 0; y < w.height; ++y)
            for (int x = 0; x < w.width; ++x) {
                const int dx = x - ux, dy = y - uy;
                const bool reached = (dx || dy) && distance[w.at(x, y)] < (1 << 20);
                const uint8_t v = g.reach(dx, dy);
                if (!g.contains(dx, dy)) { assert(!reached); continue; }
                assert(reached == (v == kStop || v == kPass));
                if (reached) assert((v == kPass) == (w.friends[w.at(x, y)] != 0));
                if (reached && v == kStop && checked < 12 && rng() % 8 == 0) {
                    ++checked;
                    assert(g.find_path(move, dx, dy));
                    int px = 0, py = 0, total = 0, steps = 0;
                    while (!g.advance(px, py)) {
                        // On screen 801CC290 reads this as a signed byte with the unit at 15.
                        assert(px + kWindow >= -128 && px + kWindow <= 127 && py + kWindow >= -128 && py + kWindow <= 127);
                        total += w.costs[w.at(ux + px, uy + py)];
                        assert(++steps <= 100000);
                    }
                    assert(px == dx && py == dy && total == distance[w.at(x, y)]);
                }
            }

        // What the original's tables get: nothing outside their 31x31.
        static constexpr int kGuard = 64;
        std::vector<uint8_t> table(kCells + 2 * kGuard, 0xA5);
        g.store_window([&](int gx, int gy, uint8_t reach, uint8_t, uint8_t) {
            assert(gx >= 0 && gy >= 0 && gx < 31 && gy < 31);
            table[kGuard + gy * 31 + gx] = reach;
        });
        for (int i = 0; i < kGuard; ++i) assert(table[i] == 0xA5 && table[kGuard + kCells + i] == 0xA5);
        for (int gy = 0; gy < 31; ++gy)
            for (int gx = 0; gx < 31; ++gx) assert(table[kGuard + gy * 31 + gx] == g.reach(gx - 15, gy - 15));
    }
}

void check_details() {
    // The diamond, an impassable square inside it, and the map's playable rim.
    World w;
    w.width = 40; w.height = 40;
    w.costs.assign(1600, 1); w.blocked.assign(1600, 0); w.friends.assign(1600, 0);
    w.costs[w.at(21, 20)] = 0xFF;
    w.costs[w.at(20, 20)] = 1;
    Grid g;
    host_fill(g, w, 20, 20, 16);
    assert(g.radius() == 16 && g.size() == 33);
    assert(g.reach(0, 0) == kNone);
    assert(g.reach(1, 0) == kInside);                  // impassable, inside the diamond
    assert(g.reach(0, 16) == kStop);                   // 36 tiles down: still on the map
    assert(g.reach(-16, 0) == kStop && g.reach(16, 16) == kNone);
    assert(!g.contains(17, 0) && g.reach(17, 0) == kNone && g.cost(17, 0) == 0xFF && g.terrain(17, 0) == 0xFF);

    host_fill(g, w, 20, 35, 16);
    assert(g.reach(0, 2) == kStop && g.reach(0, 3) == kNone);   // y <= height - 0x30
    host_fill(g, w, 2, 20, 16);
    assert(g.reach(0, 0) == kNone && g.reach(-1, 0) == kNone && g.reach(1, 0) == kStop);   // x >= 0x20

    // A second, smaller fill leaves nothing of the first.
    host_fill(g, w, 20, 20, 17);
    host_fill(g, w, 20, 20, 16);
    assert(g.size() == 33);
    assert(g.reach(1, 0) == kInside && g.reach(-16, 0) == kStop);

    // Nearest square. Of equals, a whole-number distance keeps the scan's first...
    int bx = 99, by = 99;
    assert(g.pick_nearest([](int, int) { return true; },
                          [](int dx, int dy) { return float(std::abs(dx) + std::abs(dy)); }, bx, by));
    assert(bx == 0 && by == -1);
    // ...and any other the scan's last: the original cuts only the new one to a whole number.
    assert(g.pick_nearest([](int, int) { return true; },
                          [](int dx, int dy) { return float(std::abs(dx) + std::abs(dy)) + 0.5f; }, bx, by));
    assert(bx == 0 && by == 1);
    bx = 99;
    assert(!g.pick_nearest([](int, int) { return false; }, [](int, int) { return 1.0f; }, bx, by) && bx == 99);
    assert(!g.pick_nearest([](int, int) { return true; }, [](int, int) { return 10000.5f; }, bx, by));

    // The walk: no path means over at once, and no square of the old path is kept.
    assert(!g.find_path(16, 0, 0));
    int x = 0, y = 0;
    assert(g.advance(x, y) && x == 0 && y == 0);
    assert(g.find_path(16, -3, 0));
    assert(!g.find_path(16, 17, 0));
    assert(g.advance(x, y) && x == 0 && y == 0);
}
}  // namespace

int main() {
    std::mt19937 rng(0x4D57u);
    check_details();
    compare_with_original(rng, false, 1500);
    compare_with_original(rng, true, 300);
    check_wide(rng, 120);
    std::puts("move range: ok");
    return 0;
}
