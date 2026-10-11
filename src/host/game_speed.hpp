#pragma once
// 2x speed (settings "game_speed", docs/native/game-speed.md). The game runs one logic
// frame every D_8010F0C8 VIs: 8008163C returns while the VI count since the last frame
// (D_80172D0C) is below it, and 8007FC48 sets it to 2 once at boot (docs/design/60fps.md
// §2.1). Every wait, step and scroll counts frames, so 1 runs the whole game twice as
// fast; the VI rate and the audio thread stay as they are, so the music keeps its tempo.
//
// It hurries only what the player watches: the tactical map while nothing waits for the
// player (scripts, the enemy phase, units moving), the battle animation and the world
// map's story. The player's menus and cursor, the intermission, the title and the other
// screens keep the original speed, as their key repeats count frames too.
#include "guest_memory.hpp"
#include "script_skip.hpp"
#include <atomic>
#include <cstdint>

uint64_t srw64_current_vi();

namespace srw64::game_speed {
using guest::read;

inline std::atomic_bool on{false};        // the setting (presentation_settings.cpp)
inline std::atomic_bool running{false};   // this frame hurried (the debug status)
inline std::atomic<uint64_t> menu_vi{0};  // the pre-battle screens' last step

constexpr uint32_t interval=0x8010F0C8, mode=0x8015DA02, map_state=0x80172EB0, phase=0x8010F5E8,
    engine=0x8015F950, vm=engine+0x948;

// Game thread: the pre-battle confirmation and the enemy's attack response step every
// frame they are up (801D5064 / 801D5294, game_hooks.cpp). They are menus wherever they
// come, the enemy phase too.
inline void menu_step(){menu_vi=srw64_current_vi();}

// The tactical UI's states that wait for the player (docs/gameplay/original-controls.md §3):
// the idle cursor and its motion, the unit menus, the move grid, the weapon and spirit
// lists, the information windows.
inline bool player_input(unsigned state) {
    switch(state) {
    case 5: case 6: case 8: case 0xC: case 0xD: case 0x16: case 0x17: case 0x19: case 0x1B: case 0x22: case 0x3A: case 0x3C:
        return true;
    default:
        return state>=0x2E && state<=0x37;
    }
}

// Whether this frame runs at 2x, from the game's state. MENU: a pre-battle screen stepped
// within the last few VIs.
inline bool hurried(const uint8_t* ram,bool menu) {
    if(menu)return false;
    switch(read(ram,mode,1)) {
    case 2: case 0x1A: case 0x1C:   // the battle animation
    case 12: case 13:               // the world map: story only
        return true;
    case 3: case 0xB: case 0x11: case 0x16: case 0x22: {   // the tactical map
        // An event script runs (script_inject.hpp idle_reason): hurried, but for the
        // commands that hand the player a choice or the sortie list.
        if(read(ram,engine+0x97C,2)!=0x80) {
            const uint32_t h=read(ram,vm+0x30,4);
            return h!=script_skip::handler::choice && h!=script_skip::handler::sortie;
        }
        if(read(ram,phase,1)!=1)return true;   // the enemy's and the third side's phases
        return !player_input(read(ram,map_state,1));
    }
    default:
        return false;
    }
}

// Game thread, every VI (the frame boundary). Writes the interval once the game has set
// it, so the boot runs as it always did.
inline void frame(uint8_t* ram) {
    const uint32_t now=read(ram,interval,4);
    if(now!=1 && now!=2){running=false;return;}
    const uint64_t vi=srw64_current_vi();
    const bool fast=on.load() && hurried(ram,vi-menu_vi.load()<4);
    running=fast;
    if(now!=(fast?1u:2u))guest::write32(ram,interval,fast?1:2);
}
}  // namespace srw64::game_speed
