// src/host/switch/heap_layout.hpp: the Switch heap's usable stretch and the thread-stack
// records' ring, against the answers svcQueryMemory can give, the odd ones included
// (tests/test_switch_heap.py builds this with AddressSanitizer and UBSan).
#include "switch/heap_layout.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace srw64::switch_heap;

namespace {
constexpr uint64_t MiB = 0x100000;
constexpr uint32_t kBorrowed = 0x1;      // MemAttr_IsBorrowed (BIT(0))
constexpr uint32_t kTypeStack = 0x0B;    // MemType_MappedMemory-ish: anything not heap
constexpr uint32_t kTypeUnmapped = 0x00;

// The kernel's view: regions in address order, the rest unmapped. Answers like svcQueryMemory,
// or the way a test wants it to misbehave.
struct Kernel {
    std::vector<Region> regions;
    int fail_at_call = -1;                   // the n-th query (0-based) fails
    int calls = 0;
    bool (*rewrite)(uint64_t at, Region& region, int call) = nullptr;
    bool operator()(uint64_t at, Region& out) {
        const int call = calls++;
        if (call == fail_at_call) return false;
        out = {};
        bool found = false;
        for (const auto& region : regions)
            if (at >= region.addr && at - region.addr < region.size) { out = region; found = true; break; }
        if (!found) {
            uint64_t next = UINT64_MAX;
            for (const auto& region : regions) if (region.addr > at && region.addr < next) next = region.addr;
            out = {at & ~(kPage - 1), next - (at & ~(kPage - 1)), kTypeUnmapped, 0, 0};
        }
        if (rewrite && !rewrite(at, out, call)) return false;
        return true;
    }
};

Region heap(uint64_t addr, uint64_t size) { return {addr, size, kTypeHeap, 0, kPermRw}; }
Region borrowed(uint64_t addr, uint64_t size) { return {addr, size, kTypeHeap, kBorrowed, 0}; }

// Whatever the answers, the stretch kept lies inside what was given and every byte of it was
// described as plain heap by an answer the walk accepted.
void check_inside(const Layout& layout, const Kernel& kernel) {
    if (!layout.usable() || kernel.regions.empty()) return;   // (empty: the answers are made up as asked)
    assert(layout.start >= layout.given_start);
    assert(layout.start + layout.size <= layout.given_start + layout.given_size);
    assert(layout.start % kPage == 0 && layout.size % kPage == 0);
    for (uint64_t at = layout.start; at < layout.start + layout.size; at += kPage) {
        bool ok = false;
        for (const auto& region : kernel.regions)
            if (at >= region.addr && at - region.addr < region.size) { ok = plain(region); break; }
        assert(ok);
    }
}

Layout run(Kernel& kernel, uint64_t start, uint64_t size) {
    Layout layout = choose(start, size, kernel);
    check_inside(layout, kernel);
    return layout;
}
}  // namespace

int main() {
    const uint64_t base = 0x8000000000ull, size = 512 * MiB;

    // A heap with nothing in it: all of it, one query.
    {
        Kernel kernel{{heap(base, size)}};
        const Layout layout = run(kernel, base, size);
        assert(layout.start == base && layout.size == size && layout.holes == 0 && !layout.walk_stopped);
        assert(kernel.calls == 1);
    }

    // The console on 2026-11-26: one borrowed 0x101000 stack hbmenu left behind. The larger
    // side is kept, the stack never.
    {
        const uint64_t hole = base + 100 * MiB;
        Kernel kernel{{heap(base, 100 * MiB), borrowed(hole, 0x101000), heap(hole + 0x101000, size - 100 * MiB - 0x101000)}};
        const Layout layout = run(kernel, base, size);
        assert(layout.start == hole + 0x101000 && layout.size == size - 100 * MiB - 0x101000);
        assert(layout.holes == 1 && layout.hole[0].addr == hole && layout.hole[0].size == 0x101000);
        assert(layout.hole[0].kind == HoleKind::Mapped && layout.hole[0].attr == kBorrowed);
    }

    // The hole near the end: the front is kept.
    {
        const uint64_t hole = base + size - 3 * MiB;
        Kernel kernel{{heap(base, size - 3 * MiB), borrowed(hole, MiB), heap(hole + MiB, 2 * MiB)}};
        const Layout layout = run(kernel, base, size);
        assert(layout.start == base && layout.size == size - 3 * MiB && layout.holes == 1);
    }

    // A hole at either edge, and a region that starts before the heap and runs past it.
    {
        Kernel kernel{{borrowed(base, MiB), heap(base + MiB, size - 2 * MiB), borrowed(base + size - MiB, MiB)}};
        const Layout layout = run(kernel, base, size);
        assert(layout.start == base + MiB && layout.size == size - 2 * MiB && layout.holes == 2);
        Kernel wide{{heap(base - 16 * MiB, size + 32 * MiB)}};
        const Layout clipped = run(wide, base, size);
        assert(clipped.start == base && clipped.size == size);
    }

    // Plain means read-write heap with no attributes: read-only heap, heap with any attribute,
    // and other types are all left out.
    for (const Region odd : {Region{0, 0, kTypeHeap, 0, 0x1}, Region{0, 0, kTypeHeap, 0x8, kPermRw},
                             Region{0, 0, kTypeStack, 0, kPermRw}, Region{0, 0, kTypeHeap | 0x100, 0, kPermRw}}) {
        Region middle = odd;
        middle.addr = base + 10 * MiB; middle.size = MiB;
        Kernel kernel{{heap(base, 10 * MiB), middle, heap(base + 11 * MiB, size - 11 * MiB)}};
        const Layout layout = run(kernel, base, size);
        if ((odd.type & 0xFF) == kTypeHeap && odd.attr == 0 && odd.perm == kPermRw) {
            assert(layout.size == size && layout.holes == 0);   // type's upper bits are state, not kind
        } else {
            assert(layout.start == base + 11 * MiB && layout.holes == 1);
        }
    }

    // The query fails part way (the old walk then took the unasked rest as heap): what follows
    // is left out with it.
    {
        const uint64_t hole = base + 300 * MiB;
        Kernel kernel{{heap(base, 300 * MiB), borrowed(hole, MiB), heap(hole + MiB, size - 301 * MiB)}};
        kernel.fail_at_call = 2;   // the third: the heap after the stack
        const Layout layout = run(kernel, base, size);
        assert(layout.walk_stopped);
        assert(layout.start == base && layout.size == 300 * MiB);
        assert(layout.holes == 2 && layout.hole[1].kind == HoleKind::QueryFailed);
        assert(layout.hole[1].addr == hole + MiB && layout.hole[1].addr + layout.hole[1].size == base + size);
    }

    // The very first query fails: nothing usable, never the whole heap.
    {
        Kernel kernel{{heap(base, size)}};
        kernel.fail_at_call = 0;
        const Layout layout = run(kernel, base, size);
        assert(!layout.usable() && layout.walk_stopped && layout.holes == 1);
    }

    // Answers (the one after the stack) that do not hold the address asked about: size 0 (the old walk stopped and took
    // the rest as heap), a region that ends at or before it (the old walk could stand still),
    // one that starts after it, and one whose end wraps.
    auto zero = +[](uint64_t, Region& region, int call) { if (call == 2) region.size = 0; return true; };
    auto behind = +[](uint64_t at, Region& region, int call) {
        if (call == 2) { region.addr = at - 2 * MiB; region.size = 2 * MiB; } return true; };
    auto ahead = +[](uint64_t at, Region& region, int call) { if (call == 2) region.addr = at + kPage; return true; };
    auto wraps = +[](uint64_t at, Region& region, int call) { if (call == 2) { region.addr = at; region.size = 0 - at + kPage; } return true; };
    for (auto rewrite : {zero, behind, ahead, wraps}) {
        Kernel kernel{{heap(base, 64 * MiB), borrowed(base + 64 * MiB, MiB), heap(base + 65 * MiB, size - 65 * MiB)}};
        kernel.rewrite = rewrite;
        const Layout layout = run(kernel, base, size);
        assert(layout.walk_stopped && layout.start == base && layout.size == 64 * MiB);
        assert(layout.holes == 2 && layout.hole[1].kind == HoleKind::Inconsistent);
        assert(kernel.calls == 3);   // nothing asked past the answer that made no sense
    }

    // A query that keeps answering a single page, forever if it could: the walk still ends.
    {
        Kernel kernel{{}};
        kernel.rewrite = +[](uint64_t at, Region& region, int) { region = heap(at, kPage); return true; };
        const Layout layout = run(kernel, base, 4 * MiB);
        assert(layout.size == 4 * MiB && kernel.calls == int(4 * MiB / kPage));
    }

    // Every page something else: nothing usable.
    {
        Kernel kernel{{borrowed(base, size)}};
        const Layout layout = run(kernel, base, size);
        assert(!layout.usable() && layout.holes == 1 && !layout.walk_stopped);
    }

    // Heaps that cannot be: empty, wrapping, or not on a page. Nothing asked, nothing given.
    {
        Kernel kernel{{heap(base, size)}};
        assert(!run(kernel, base, 0).usable());
        assert(!run(kernel, UINT64_MAX - MiB + 1, 2 * MiB).usable());
        assert(!run(kernel, base + 8, MiB).usable());
        assert(kernel.calls == 0);
    }

    // More holes than are kept: all counted, the first kHolesKept listed, the largest stretch
    // wherever it is.
    {
        Kernel kernel;
        uint64_t at = base;
        for (int i = 0; i < 40; ++i) {
            const uint64_t run_size = (i == 33 ? 50 : 2) * MiB;
            kernel.regions.push_back(heap(at, run_size)); at += run_size;
            kernel.regions.push_back(borrowed(at, MiB)); at += MiB;
        }
        const Layout layout = run(kernel, base, at - base);
        assert(layout.holes == 40);
        assert(layout.size == 50 * MiB && layout.start == base + 33 * 3 * MiB);
        assert(layout.hole[kHolesKept - 1].addr == base + kHolesKept * 3 * MiB - MiB);
    }

    // The ring of thread-stack records. A thread in its join keeps its record (a new thread took
    // it before, and the join then marked that new thread joined); ended ones are reused oldest
    // first; with every record live nothing is taken.
    {
        uint32_t states[6] = {kRunning, kJoining, kDetached, kJoined, kUnused, kRunning};
        size_t next = 0;
        auto at = [&](size_t i) { return states[i]; };
        assert(pick_slot(next, 6, at) == 3 && next == 4);
        assert(pick_slot(next, 6, at) == 4 && next == 5);
        states[3] = states[4] = kRunning;
        assert(pick_slot(next, 6, at) == 6);   // all live: none
        states[1] = kJoined;                   // the join returned
        assert(pick_slot(next, 6, at) == 1);
        for (uint32_t state : {kRunning, kDetached, kJoining}) assert(!reusable(state));
        for (uint32_t state : {kUnused, kJoined}) assert(reusable(state));
        size_t big = SIZE_MAX - 1;             // the counter wraps without trouble
        uint32_t two[2] = {kRunning, kJoined};
        assert(pick_slot(big, 2, [&](size_t i) { return two[i]; }) == 1);
    }

    std::puts("switch heap: ok");
    return 0;
}
