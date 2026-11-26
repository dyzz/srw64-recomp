#pragma once
// The Switch heap's usable stretch and the thread-stack records' ring, without libnx, so a
// computer tests every way they can go wrong (tests/native_switch_heap.cpp). switch_platform.cpp
// feeds them svcQueryMemory and checks the constants below against libnx's own.
//
// Under hbloader the NRO gets the heap hbmenu used, and memory hbmenu left mapped there
// (a thread's 1 MiB stack, borrowed) must never reach newlib (2026-11-23). What is kept is
// the largest stretch whose every page the kernel said is plain read-write heap: a page the
// kernel did not describe, or described in a way that makes no sense, is not one.
#include <cstddef>
#include <cstdint>

namespace srw64::switch_heap {

// libnx: MemType_Heap, Perm_Rw (svc.h). static_assert'ed in switch_platform.cpp.
constexpr uint32_t kTypeHeap = 0x05;
constexpr uint32_t kPermRw = 0x3;
constexpr uint64_t kPage = 0x1000;

// One answer of svcQueryMemory: the region holding the address asked about.
struct Region { uint64_t addr = 0, size = 0; uint32_t type = 0, attr = 0, perm = 0; };

// A stretch left out. Why: the kernel described it as something else, or could not be
// trusted about it (the query failed, or answered with a region that does not hold the
// address asked about); the walk stops there and the rest of the heap is left out with it.
enum class HoleKind : uint32_t { Mapped, QueryFailed, Inconsistent };
struct Hole { uint64_t addr = 0, size = 0; uint32_t type = 0, attr = 0, perm = 0; HoleKind kind = HoleKind::Mapped; };

constexpr size_t kHolesKept = 16;
struct Layout {
    uint64_t given_start = 0, given_size = 0;   // what the kernel or hbloader handed over
    uint64_t start = 0, size = 0;               // the stretch newlib gets; size 0: none usable
    size_t holes = 0;                           // every one, also those past kHolesKept
    Hole hole[kHolesKept];
    bool walk_stopped = false;                  // the kernel's answers ran out before the end
    bool usable() const { return size != 0; }
};

inline bool plain(const Region& region) {
    return (region.type & 0xFF) == kTypeHeap && region.attr == 0 && region.perm == kPermRw;
}

// query(address, Region&) -> bool: svcQueryMemory. Any address range is accepted, a heap that
// wraps past the end of the address space or does not start on a page included: what cannot be
// a heap gives an empty layout, never a stretch outside what was given.
template <class Query> Layout choose(uint64_t start, uint64_t size, Query&& query) {
    Layout out;
    out.given_start = start;
    out.given_size = size;
    if (size == 0 || start + size < start || (start % kPage) != 0) return out;
    const uint64_t end = start + size;
    uint64_t best_start = start, best_size = 0, run_start = start;
    auto note = [&](uint64_t at, uint64_t to, const Region& region, HoleKind kind) {
        if (at - run_start > best_size) { best_start = run_start; best_size = at - run_start; }
        if (out.holes < kHolesKept) out.hole[out.holes] = {at, to - at, region.type & 0xFF, region.attr, region.perm, kind};
        ++out.holes;
        run_start = to;
    };
    uint64_t at = start;
    // Each answer moves the walk on by at least a page, so it ends; the bound is there for a
    // query that would not.
    for (uint64_t steps = 0; at < end; ++steps) {
        Region region;
        if (steps > size / kPage || !query(at, region)) {
            note(at, end, region, HoleKind::QueryFailed);
            out.walk_stopped = true;
            break;
        }
        const uint64_t region_end = region.addr + region.size;
        // It must hold `at`, and end past it without wrapping.
        if (region.size == 0 || region.addr > at || region_end <= at || region_end < region.addr) {
            note(at, end, region, HoleKind::Inconsistent);
            out.walk_stopped = true;
            break;
        }
        const uint64_t next = region_end < end ? region_end : end;
        if (!plain(region)) note(at, next, region, HoleKind::Mapped);
        at = next;
    }
    // The last stretch, unless the walk stopped (its hole then runs to the end: run_start == end).
    if (run_start <= at && at - run_start > best_size) { best_start = run_start; best_size = at - run_start; }
    out.start = best_start;
    out.size = best_size;
    return out;
}

inline const char* kind_name(HoleKind kind) {
    switch (kind) {
    case HoleKind::Mapped: return "mapped";
    case HoleKind::QueryFailed: return "query-failed";
    case HoleKind::Inconsistent: return "inconsistent";
    }
    return "?";
}

// The thread-stack records (switch_platform.cpp StackRecord): a ring whose slot a new thread
// takes only from one that has ended. A thread in its join (it may wait there for long) still
// owns its record: the join writes it once the thread has ended.
enum StackState : uint32_t { kUnused = 0, kRunning = 1, kDetached = 2, kJoining = 3, kJoined = 4 };
inline bool reusable(uint32_t state) { return state == kUnused || state == kJoined; }

// The next reusable slot from `next` on (advanced past it), or `count` when every slot is in
// use: the new thread then goes unrecorded rather than taking a live thread's record.
template <class StateAt> size_t pick_slot(size_t& next, size_t count, StateAt&& state_at) {
    for (size_t tries = 0; tries < count; ++tries) {
        const size_t slot = next++ % count;
        if (reusable(state_at(slot))) return slot;
    }
    return count;
}

}  // namespace srw64::switch_heap
