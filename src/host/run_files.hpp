#pragma once
// The files the check scripts drive the game and read its state and events by (control.txt,
// the *-state.json, the *-events.jsonl, the audio capture): everywhere, except on the PS Vita
// without the debug interface and on the Switch unless SRW64_RUN_FILES is set, where each
// costs a slow memory card's writes and the debug interface does the same. On the Switch the
// hooks wrote and flushed their event logs holding the locks the UI reads their state under:
// 30 ms waits a few times a second on the opening and the name page (SRW64_UI_SLOW), and
// Horizon will not open a file another handle is writing, so nothing could read them anyway.
#include <cstdlib>
#include <filesystem>
#include <fstream>

inline bool srw64_run_files() {
#if defined(__SWITCH__)
    static const bool enabled = std::getenv("SRW64_RUN_FILES") != nullptr;
    return enabled;
#elif defined(__vita__)
    static const bool enabled = std::getenv("SRW64_DEBUG") != nullptr;
    return enabled;
#else
    return true;
#endif
}

namespace srw64 {
inline void open_run_log(std::ofstream& log, const std::filesystem::path& path) {
    if (srw64_run_files()) log.open(path);
}
}
