#pragma once
// The frame-rate readout (settings "show_fps", frontend.cpp): the game's display lists as
// graphics.cpp takes them, one per frame the game draws. The graphics thread counts them
// and keeps the longest gap between two; the readout takes the gap and starts it again.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace srw64::frame_rate {
inline std::atomic<uint64_t> lists{0};
inline std::atomic<uint32_t> longest_us{0};
// Presents: the frames shown, one per display list as the original runs, more when RT64
// interpolates between them (SRW64_REFRESH_RATE, docs/design/60fps.md §7).
inline std::atomic<uint64_t> presents{0};
// The gaps between presents, for the 120 Hz experiment's pacing: under 6 ms, 6-12, 12-20,
// 20-28, 28 and over.
inline std::atomic<uint64_t> present_gaps[5]{};
// How many workloads apart two presents are: the same one again, the next, every other, more.
inline std::atomic<uint64_t> present_steps[4]{};
inline void present_workload(uint64_t workload) {
    static uint64_t last = 0;
    if (last && workload >= last) present_steps[std::min<uint64_t>(workload - last, 3)].fetch_add(1, std::memory_order_relaxed);
    last = workload;
}
inline void present_sent() {
    static int64_t last = 0;
    const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (last) {
        const int64_t gap = now - last;
        present_gaps[gap < 6000 ? 0 : gap < 12000 ? 1 : gap < 20000 ? 2 : gap < 28000 ? 3 : 4].fetch_add(1, std::memory_order_relaxed);
    }
    last = now;
    presents.fetch_add(1, std::memory_order_relaxed);
}
// RT64's idea of the game's own frame rate (its VI history), for the 120 Hz experiment.
inline std::atomic<uint32_t> original_rate{0};
// Graphics thread, once per display list.
inline void list_sent() {
    static int64_t last = 0;
    const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (last) {
        const uint32_t gap = uint32_t(std::min<int64_t>(now - last, UINT32_MAX));
        uint32_t seen = longest_us.load(std::memory_order_relaxed);
        while (gap > seen && !longest_us.compare_exchange_weak(seen, gap, std::memory_order_relaxed)) {}
    }
    last = now;
    lists.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace srw64::frame_rate
