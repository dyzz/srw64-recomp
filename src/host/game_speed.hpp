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
#include "draw_skip.hpp"
#include "guest_memory.hpp"
#include "script_skip.hpp"
#include "sched_rate.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>

uint64_t srw64_current_vi();

namespace srw64::game_speed {
using guest::read;

// The speed when hurried (presentation_settings.cpp): 1 is off, 2, or 3 and 4 with the
// 120 Hz scheduler (sched_rate.hpp). The speed button steps through them (frontend.cpp).
inline std::atomic<unsigned> level{1};
inline unsigned top_level(){return sched_rate::factor()==2 ? 4 : 2;}
inline std::atomic_bool running{false};   // this frame hurried (the debug status)
inline std::atomic<uint64_t> menu_vi{0};  // the pre-battle screens' last step

constexpr uint32_t interval=0x8010F0C8, mode=0x8015DA02, map_state=0x80172EB0, phase=0x8010F5E8,
    engine=0x8015F950, vm=engine+0x948;

// Game thread: the pre-battle confirmation and the enemy's attack response step every
// frame they are up (801D5064 / 801D5294, game_hooks.cpp). They are menus wherever they
// come, the enemy phase too.
inline void menu_step(){menu_vi=srw64_current_vi();}

// Experiment: SRW64_FORCE_FAST=1 hurries every frame while the setting is on.
inline bool force_fast(){static const bool f=sched_rate::env_number("SRW64_FORCE_FAST",0)==1;return f;}

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
    case 2:                         // the battle animation in a stage
    // Not 0x1A カラオケモード and 0x1C the title's demo and the battle viewer: they are
    // watched, and the lyrics count frames while the song keeps its tempo.
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

// The interval for a hurried step. At 60 Hz 2x is every retrace. At 120 Hz the original
// speed is every 4th, 2x every 2nd, 4x every one, and 3x averages 4/3: steps 1, 1 and 2
// retraces apart in turn. A step has run when the logic gate's count (D_80172D0C, raised
// after this hook) reads 0 here; `turn` counts them for 3x.
inline uint32_t fast_interval(const uint8_t* ram,unsigned speed) {
    if(sched_rate::factor()==1)return 1;
    if(speed!=3)return speed==4 ? 1 : 2;
    static unsigned turn=0;
    if(read(ram,sched_rate::logic_count,4)==0)++turn;
    return turn%3==2 ? 2 : 1;
}

// Logic steps a second now: 30, or 30 times the speed while hurried.
inline unsigned step_rate(){return running.load() ? 30*std::min(level.load(),top_level()) : 30;}
// Pictures a second RT64 gets: fewer than the steps while some go undrawn (draw_skip.hpp).
inline unsigned picture_rate(){return step_rate()/std::max(draw_skip::every.load(),1u);}

// Game thread, every VI (the frame boundary). Writes the interval once the game has set
// it, so the boot runs as it always did.
inline void frame(uint8_t* ram) {
    const uint32_t now=read(ram,interval,4);
    if(now!=1 && now!=2 && now!=4){running=false;draw_skip::observe(ram,false,1);return;}
    const uint64_t vi=srw64_current_vi();
    const unsigned speed=std::min(level.load(),top_level());
    const bool fast=speed>1 && (force_fast() || hurried(ram,vi-menu_vi.load()<4));
    running=fast;
    sched_rate::uneven=fast && speed==3;
    draw_skip::observe(ram,fast,speed);
    const uint32_t want=fast ? fast_interval(ram,speed) : 2*sched_rate::factor();
    if(now!=want)guest::write32(ram,interval,want);
}
}  // namespace srw64::game_speed
