#pragma once
// The threads a frame waits on, kept on a phone's big cores (docs/design/120hz-scheduler.md
// §3.6). Android places them by load, and each is busy only a few milliseconds a frame, so
// on the Seeker (four A55 and four A78) the graphics thread ran 94% of the time on a little
// core, the game's threads 90%, the audio's RSP task 99%. Bound to the cores of the highest
// capacity: the guest's threads (the game, its scheduler and its audio thread), the runtime's
// graphics, RSP task and VI threads, RT64's workload and present threads. The rest (the UI,
// SDL, RT64's texture and shader workers, the driver's) stay where Android puts them.
//
// SRW64_BIG_CORES=1 binds them all the time, =0 never; otherwise only while the game runs
// hurried (game_speed.hpp). Nothing on a device whose cores are all alike.
#if defined(__ANDROID__)
#include <dirent.h>
#include <sched.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

namespace srw64::big_cores {
inline std::atomic<uint64_t> bound{0}, released{0};
inline std::atomic_bool on{false};

inline int mode() {   // 0 never, 1 always, 2 while hurried
    static const int m = [] {
        const char* v = std::getenv("SRW64_BIG_CORES");
        return v && *v ? std::atoi(v) == 1 ? 1 : 0 : 2;
    }();
    return m;
}

inline bool key_thread(const char* name) {
    static constexpr const char* prefixes[] = {"Game Thread", "Gfx Thread", "SP Task Thread", "VI Thread",
                                               "RT64 Workload", "RT64 Present"};
    return std::any_of(std::begin(prefixes), std::end(prefixes),
                       [&](const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; });
}

// The CPUs of the highest capacity, and all of them; the two alike where there is no big.LITTLE.
struct Sets { cpu_set_t big, all; bool split = false; };
inline const Sets& sets() {
    static const Sets s = [] {
        Sets s{};
        CPU_ZERO(&s.big); CPU_ZERO(&s.all);
        const long count = std::min<long>(sysconf(_SC_NPROCESSORS_CONF), CPU_SETSIZE);
        int capacity[CPU_SETSIZE] = {}, top = 0;
        for (long cpu = 0; cpu < count; ++cpu) {
            CPU_SET(cpu, &s.all);
            if (FILE* f = std::fopen(("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpu_capacity").c_str(), "r")) {
                if (std::fscanf(f, "%d", &capacity[cpu]) != 1) capacity[cpu] = 0;
                std::fclose(f);
            }
            top = std::max(top, capacity[cpu]);
        }
        for (long cpu = 0; cpu < count; ++cpu) if (top && capacity[cpu] == top) CPU_SET(cpu, &s.big);
        s.split = CPU_COUNT(&s.big) > 0 && CPU_COUNT(&s.big) < CPU_COUNT(&s.all);
        return s;
    }();
    return s;
}

// Every key thread now running onto the big cores (BIG) or back to all of them.
inline void apply(bool big) {
    const Sets& s = sets();
    DIR* tasks = opendir("/proc/self/task");
    if (!tasks) return;
    unsigned changed = 0;
    while (dirent* entry = readdir(tasks)) {
        const int tid = std::atoi(entry->d_name);
        if (tid <= 0) continue;
        char name[32] = {};
        if (FILE* f = std::fopen(("/proc/self/task/" + std::to_string(tid) + "/comm").c_str(), "r")) {
            if (!std::fgets(name, sizeof name, f)) name[0] = 0;
            std::fclose(f);
        }
        if (key_thread(name) && sched_setaffinity(tid, sizeof(cpu_set_t), big ? &s.big : &s.all) == 0) ++changed;
    }
    closedir(tasks);
    (big ? bound : released) += changed;
}

// Graphics thread, once per display list. HURRIED: this frame runs hurried. Threads made
// later (a guest thread started for a scene) are caught by binding again now and then.
inline void update(bool hurried) {
    if (!sets().split || mode() == 0) return;
    static unsigned turn = 0;
    const bool want = mode() == 1 || hurried;
    if (want != on.load() || (want && ++turn % 600 == 0)) {
        apply(want);
        on = want;
    }
}
}  // namespace srw64::big_cores
#endif
