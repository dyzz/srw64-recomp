#pragma once
// Rewind (docs/design/rewind.md, issue #6): the tactical map keeps its recent states in the
// original's suspend format (80093278), one on every quiet idle frame, the player's phase and
// the enemy's alike, and the Rewind button goes back one of them through the original's own
// restore (mode 0x11, 800936A0). From the map the host fades out to mode 0x11 itself; inside
// the battle animation it takes the original's abort (D_80225810), which picks 0x11 when
// START is held and a suspend record is there. The random numbers are not put back: the map
// seeds them again (801E00AC), so a battle fought again can end otherwise.
//
// Everything is the game's own state and save routine; the records live in the run's
// rewind/ folder only while the map lasts. Off unless the settings turn it on.
#include "game_speed.hpp"
#include "guest_memory.hpp"
#include "json/json.hpp"
#include "recomp.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <vector>


namespace srw64::rewind {
using guest::read;

inline std::atomic_bool on{false};   // the setting (presentation_settings.cpp "rewind")

// The game: its mode (3 / 0xB the tactical map, 2 the battle animation), the map's UI state
// (5 idle), the progress block (+0 phase 1 player 2 enemy, +2 turn, +9 chapter title), the
// script engine (script_inject.hpp idle_reason) and the defeat sequence.
inline constexpr uint32_t mode=game_speed::mode,map_state=game_speed::map_state,progress=game_speed::phase,
    engine=game_speed::engine,defeat=0x8010F6B0;
// The suspend serializer's buffer (80093278; 0 serializes only, 1 also writes SRAM).
inline constexpr uint32_t suspend_buffer=0x800FBEF0;
inline constexpr unsigned suspend_size=0x3AE0;
// The battle animation (battle_animation_probe.hpp): its state byte, its own fade request
// (-1 none; X's early end sets it), the original's abort flag (Z + START, 801C9A04) and the
// held buttons the abort branch reads (801C97F0: START held picks mode 0x11).
inline constexpr uint32_t battle_state=0x80250000,battle_fade=0x80250205,battle_abort=0x80225810,held=0x8015D9FA;
inline constexpr uint16_t start_button=0x1000;

inline bool tactical(unsigned m){return m==3 || m==0xB;}
// No event script runs and nothing else holds the map (idle_reason without the phase test).
inline bool quiet(const uint8_t* ram) {
    return tactical(read(ram,mode,1)) && read(ram,engine+4,2)==0xC0 && read(ram,engine+0x97C,2)==0x80 &&
        read(ram,engine+0x9AA,2)==0 && read(ram,defeat,2)==0;
}
// The map can be left for mode 0x11 this frame: quiet, and either standing idle (any phase:
// the enemy's stands idle between its units) or in one of the player's menus. Not on the
// pre-battle screens (game_speed menu_vi), whose native page follows the original's steps.
inline bool leavable(const uint8_t* ram,bool battle_screen) {
    if(battle_screen || !quiet(ram))return false;
    const unsigned state=read(ram,map_state,1);
    return state==5 || (read(ram,progress,1)==1 && game_speed::player_input(state));
}
// The battle animation can take the abort this frame: its actors are built (state 2 on; an
// end in 0 or 1 walks unset pointers, battle_animation_probe.hpp) and it is not already
// winding down (21 on) or ended early by X (its fade requested).
inline bool battle_abortable(const uint8_t* ram) {
    const int state=int8_t(read(ram,battle_state,1));
    return read(ram,mode,1)==2 && state>=2 && state<=20 && int8_t(read(ram,battle_fade,1))==-1 && read(ram,battle_abort,1)==0;
}

// The map's states, oldest first. A state equal to the newest is not kept twice; going back
// to one drops the ones after it, which are now the future.
struct Entry {
    uint64_t id{};
    std::vector<uint8_t> bytes;   // the serialized record, compared without its +0 word
    unsigned phase{},turn{},title{};
};
inline bool same(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    return a.size()==b.size() && a.size()>=2 && std::equal(a.begin()+2,a.end(),b.begin()+2);
}
class Ring {
    std::deque<Entry> kept;
    size_t capacity;
public:
    explicit Ring(size_t capacity=40):capacity(capacity){}
    const std::deque<Entry>& entries() const {return kept;}
    bool empty() const {return kept.empty();}
    bool matches_newest(const std::vector<uint8_t>& bytes) const {return !kept.empty() && same(kept.back().bytes,bytes);}
    // Adds a state; returns the ids that fell off the front.
    std::vector<uint64_t> push(Entry entry) {
        std::vector<uint64_t> gone;
        if(matches_newest(entry.bytes))return gone;
        kept.push_back(std::move(entry));
        while(kept.size()>capacity){gone.push_back(kept.front().id);kept.pop_front();}
        return gone;
    }
    // The state to go back to: the newest, or the one before it when the game stands at
    // the newest now (nothing done since it was kept).
    std::optional<size_t> target(bool at_newest) const {
        if(kept.empty() || (at_newest && kept.size()<2))return std::nullopt;
        return kept.size()-(at_newest?2:1);
    }
    // Back at `index`: the later states go; returns their ids.
    std::vector<uint64_t> keep_through(size_t index) {
        std::vector<uint64_t> gone;
        while(kept.size()>index+1){gone.push_back(kept.back().id);kept.pop_back();}
        return gone;
    }
    std::vector<uint64_t> clear() {
        std::vector<uint64_t> gone;
        for(const auto& e:kept)gone.push_back(e.id);
        kept.clear();
        return gone;
    }
};

// Where a request stands: waiting for a point the game can be left at, or leaving (the
// fade to mode 0x11 runs and the game gets no buttons) until the map is restored.
enum class Stage : uint8_t {idle,pending,leaving_map,leaving_battle};

// Host side. Needs the save library (save_store.hpp): the records go through its suspend
// redirection, into <run>/rewind/.
void configure(const std::filesystem::path& run_directory);
// Any thread (the Rewind button): go back one state as soon as the game allows.
void request();
// Game thread: the frame boundary (80085F30), the map's frame before its dispatcher
// (801DFBD0) and the battle animation's frame before its loop (801C9710).
void frame(uint8_t* ram);
void map_frame(uint8_t* ram,recomp_context* ctx);
void battle_frame(uint8_t* ram,recomp_context* ctx);
// The buttons the game reads (srw64_keyboard_input): none while leaving the map, START
// alone while leaving the battle animation (the abort branch reads it), else as they are.
uint32_t filter_input(uint32_t buttons);
nlohmann::json state();
}
