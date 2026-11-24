#pragma once

#ifdef __SWITCH__
#include <chrono>
#include <cstdio>
#include <cstdint>

namespace srw64 {
// Slow dialogue operations on either side of the presentation mutex. Absolute
// steady-clock times allow the game thread and graphics thread to be correlated.
class PresentStage {
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now();
    const char* name;
    uint64_t id;
public:
    PresentStage(const char* name, uint64_t id) : name(name), id(id) {}
    ~PresentStage() {
        const auto end = Clock::now();
        const auto ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (ms >= 20.0)
            std::fprintf(stderr, "SRW64_PRESENT_STAGE %s id=%llu start_us=%lld %.1f ms\n", name,
                         (unsigned long long)id, (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                             start.time_since_epoch()).count(), ms);
    }
};
}
#endif
