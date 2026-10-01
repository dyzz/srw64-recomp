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
