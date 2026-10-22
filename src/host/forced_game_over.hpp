#pragma once
// A GAME OVER on request, for tests of what follows it (the retry, the turn autosave; docs/
// design/save-slots-autosave.md): the debug interface's game_over. It does what 3D4C does
// (800A02E0, which calls 801DF280 once): the tactical overlay's start of the GAME OVER, which
// stops the music and puts the map's state 80172EB0 at 0x54. It is called from the map's idle
// state (801C8B04) on the player's phase with no event running, the place of the script
// injection's idle (script_inject::idle_reason), and that frame's idle is not run.
#include "script_inject.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

uint64_t srw64_current_vi();
namespace srw64::forced_game_over {
struct State {
    std::mutex mutex;
    bool pending{};
    uint64_t applied_vi{};    // the VI it was started at; 0: not yet
    std::string waiting;      // why the last idle frame could not take it
};
inline State& state(){static State s;return s;}

// Any thread: the next idle frame starts the GAME OVER.
inline void request(){auto& s=state();std::lock_guard lock(s.mutex);s.pending=true;s.applied_vi=0;s.waiting.clear();}
// Any thread: the VI the request was taken at, 0 while it waits.
inline uint64_t applied(){auto& s=state();std::lock_guard lock(s.mutex);return s.applied_vi;}
// Any thread: withdraws a request not taken; why the last idle frame could not take it
// (empty: no idle frame came, the map's idle state was not reached).
inline std::string cancel(){auto& s=state();std::lock_guard lock(s.mutex);s.pending=false;return s.waiting;}

// Game thread, in the map's idle state: true when the caller is to call 801DF280 now.
inline bool due(const uint8_t* ram) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.pending)return false;
    s.waiting=script_inject::idle_reason(ram);
    if(!s.waiting.empty())return false;
    s.pending=false;s.applied_vi=srw64_current_vi();
    return true;
}
}
