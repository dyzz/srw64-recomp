#pragma once
// What a slow display list spent its time on (docs/design/switch-port.md, "first-use
// stalls"). The first draw of a picture reads and decodes its file, the first use of a
// combiner links a program, a new host picture is uploaded: all on the render thread,
// inside the list that needs them, and only that once. On the Switch each is tens of
// milliseconds, and a list that meets several holds the screen for a quarter of a second
// ("the menu stutters the first time it opens"). The performance lines say only that a
// list was slow. The places that do such work note it here (Timer, add), and the render
// thread prints the list's notes when the list took 50 ms or more:
//   SRW64_GFX_SLOW 123.4ms interpret=3.1 draw=118.0 host=2.3: png 16x 98.2ms (worst 9.1 a.png); shader 2x 81.0ms
// The notes are per thread, so work on other threads is not counted against a list.
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace srw64::frame_cost {
struct Kind {
    const char* name = nullptr;
    unsigned count = 0;
    double ms = 0, worst = 0;
    char worst_what[72] = {};
};
inline std::array<Kind, 8>& kinds() {
    thread_local std::array<Kind, 8> list{};
    return list;
}
// `kind` is a string literal: the notes are kept by its address.
inline void add(const char* kind, const std::string& what, double ms) {
    for (Kind& k : kinds()) {
        if (k.name && k.name != kind) continue;
        k.name = kind;
        ++k.count;
        k.ms += ms;
        if (ms >= k.worst) {
            k.worst = ms;
            std::snprintf(k.worst_what, sizeof k.worst_what, "%s", what.c_str());
        }
        return;
    }
}
struct Timer {
    const char* kind;
    std::string what;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    Timer(const char* kind, std::string what) : kind(kind), what(std::move(what)) {}
    ~Timer() { add(kind, what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()); }
};
// The end of a display list on the render thread: its notes printed if it was slow, and
// cleared either way.
inline void end_list(double list_ms, double interpret_ms, double draw_ms, double host_ms) {
    auto& list = kinds();
    // SRW64_GFX_SLOW_MS: another threshold, to see the notes on a fast machine.
    static const double limit = [] { const char* asked = std::getenv("SRW64_GFX_SLOW_MS"); return asked ? std::atof(asked) : 50.0; }();
    if (list_ms >= limit) {
        std::string text;
        for (const Kind& k : list) {
            if (!k.name || k.ms < 1) continue;
            char part[160];
            if (k.worst_what[0])
                std::snprintf(part, sizeof part, "%s %s %ux %.1fms (worst %.1f %s)", text.empty() ? ":" : ";", k.name, k.count, k.ms, k.worst, k.worst_what);
            else
                std::snprintf(part, sizeof part, "%s %s %ux %.1fms", text.empty() ? ":" : ";", k.name, k.count, k.ms);
            text += part;
        }
        std::fprintf(stderr, "SRW64_GFX_SLOW %.1fms interpret=%.1f draw=%.1f host=%.1f%s\n", list_ms, interpret_ms, draw_ms, host_ms,
                     text.empty() ? ": nothing noted" : text.c_str());
    }
    list = {};
}
}
