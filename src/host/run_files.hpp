#pragma once
// The files the check scripts drive the game and read its state and events by (control.txt,
// the *-state.json, the *-events.jsonl, the audio capture): everywhere, except on the PS Vita
// and the Switch without the debug interface, where each costs a slow memory card's writes
// and the debug interface does the same.
#include <cstdlib>
#include <filesystem>
#include <fstream>

inline bool srw64_run_files() {
#if defined(__vita__) || defined(__SWITCH__)
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
