#pragma once
#include "guest_memory.hpp"
#include <atomic>
#include <cstdint>

// The short skip (R + START; docs/native/script-skip.md): the event script runs on to
// its next stop without its presentation. While it is on, the script poll (8009EFDC,
// game_hooks.cpp) runs again in the same frame each time a command completes, so a
// frame executes many commands; a command that waits is completed at once where its
// end state is known, and otherwise runs its own time. Every command still runs its
// original handler, so flags, variables, money, units and maps change as when read.
// The dialogue reader (native_dialogue.cpp) starts and stops the skip; its stop
// points are the reader's: the script's end, a choice, an overlay change.
namespace srw64::script_skip {
using guest::read;
using guest::write8;
using guest::write16;

struct State {
    std::atomic<uint32_t> owner{};      // the skipped script's VM context; 0: off
    std::atomic<bool> fresh{};           // started since the last poll: close the windows first
    std::atomic<bool> polling{};         // inside the skip's own polls (sound is muted)
    std::atomic<const char*> stop{};     // a stop point the loop reached, for the reader to end the skip
    // A dialogue page the skip read without showing: its text record and the speaker digits
    // of the record's header (8008CE54). The reader puts it in the history.
    void (*skipped)(const uint8_t*, uint16_t text, uint16_t speaker){};
};
inline State& state(){static State s;return s;}

inline void start(uint32_t owner){auto& s=state();s.stop=nullptr;s.fresh=true;s.owner=owner;}
inline void end(){state().owner=0;state().fresh=false;}
inline bool active(){return state().owner.load()!=0;}
inline bool skipping(uint32_t owner){return owner && state().owner.load()==owner;}
inline const char* take_stop(){return state().stop.exchange(nullptr);}

// Handlers (the dispatch table's entries, docs/script/stage-script-exploration.md).
namespace handler {
constexpr uint32_t dialogue_first=0x8009F654, dialogue_last=0x8009F6E0;  // 3D3E-3D43, 0x1C apart
constexpr uint32_t wait=0x800A00F0;          // 3D38: counter at +0x2E, done below zero
constexpr uint32_t close_wait=0x800A013C;    // 3D47: counts +0x2E up to 32
constexpr uint32_t close_yield=0x800A0468;   // 3D4E: done on its second call
constexpr uint32_t fade=0x8009F948;          // 3D3B: waits for the fade engine's record 1
constexpr uint32_t shake=0x8009F880;         // 3D36
constexpr uint32_t deploy=0x8009FC04;        // 3D45
constexpr uint32_t move=0x800A0360;          // 3D3C
constexpr uint32_t choice=0x8009FA94;        // 3D44
constexpr uint32_t sortie=0x800A09D0;        // 3D3D: the player picks the sortie
constexpr uint32_t victory=0x800A01CC;       // 3D4A: the stage's result screens
constexpr uint32_t game_over=0x800A02E0;     // 3D4C
constexpr uint32_t ending=0x800A02B8;        // 3D71
constexpr uint32_t battlefield=0x800A031C;   // 3D4D: engine+4 = 3; the overlay switches frames later
}
inline bool dialogue(uint32_t h){return h>=handler::dialogue_first && h<=handler::dialogue_last && (h-handler::dialogue_first)%0x1C==0;}

// Commands the skip stops after: from them on the player acts, or the stage leaves the
// story. The skip ends once the command has started, as the reader then waits for it.
inline const char* stop_point(uint32_t h) {
    switch(h) {
    case handler::choice:return "choice";
    case handler::sortie:return "sortie";
    case handler::victory:return "victory";
    case handler::game_over:return "game_over";
    case handler::ending:return "ending";
    // The world map hands over to the battlefield some frames after 3D4D, and the script
    // runs on meanwhile: the command after it (3D45's first 10 frames) lasts until the
    // tactical overlay it calls is in. Nothing after 3D4D may be hurried.
    case handler::battlefield:return "battlefield";
    default:return nullptr;
    }
}

// The fade engine's record i at 800FF9D8 + 16 i (8009AC84 sets it up, 8009AD64 steps it
// once a frame): +4 running, +5 target alpha, +C the alpha drawn. Its last step writes
// the target and clears +4, which is what finishing it here does.
constexpr uint32_t fade_record=0x800FF9D8+0x10;

// The game mode 8015DA02 is the tactical map (3 or 0xB).
inline bool tactical_map(const uint8_t* ram){const auto m=read(ram,0x8015DA02,1);return m==3 || m==0xB;}

// How the skip moves a waiting command (state +0x24 not 0) on in this frame.
enum class Finish {
    no,     // it runs its own time: the skip polls it again next frame
    once,   // its end state is written: the next poll completes it
    steps,  // it advances once per call: the skip polls it until it completes
    motion, // as steps, with the sprites' motion (80081BFC, once a frame) run before each call
};
inline Finish finish(uint8_t* ram,uint32_t vm) {
    const uint32_t h=read(ram,vm+0x30,4);
    if(dialogue(h))return Finish::once;  // 8008FED4 answers "read" while skipping (game_hooks.cpp)
    switch(h) {
    case handler::wait:
        if(read(ram,vm+0x26,2))write16(ram,vm+0x2E,0);
        return Finish::once;
    case handler::close_wait:
        if(read(ram,vm+0x26,2))write16(ram,vm+0x2E,0x1F);
        return Finish::once;
    case handler::close_yield:
        return Finish::once;
    case handler::fade:
        if(read(ram,fade_record+4,1)){write8(ram,fade_record+0xC,uint8_t(read(ram,fade_record+5,1)));write8(ram,fade_record+4,0);}
        return Finish::once;
    case handler::shake:
        // 8020D4B0 (tactical map) / 801C5138 (world map) step the shake once per call.
        return Finish::steps;
    case handler::deploy:
        // It calls the tactical overlay: on another map mode it runs its own time.
        if(!tactical_map(ram))return Finish::no;
        // 10 frames (+0x28), the records become units, then the appearance (8020C524,
        // stepped once per call) and 10 frames more (+0x2E): the counts go to their ends.
        if(!read(ram,vm+0x26,2) && int16_t(read(ram,vm+0x28,2))<9)write16(ram,vm+0x28,9);
        if(int16_t(read(ram,vm+0x2E,2))<9)write16(ram,vm+0x2E,9);
        return Finish::steps;
    case handler::move:
        // 8020A030 set the unit off; 8020A788 gives the unit's sprite a velocity toward
        // the target cell each call, the frame's sprite motion (80081BFC) moves it, and
        // at the cell 801CBEB8 writes the roster's new position (802279E8 reaches 3).
        return tactical_map(ram)?Finish::motion:Finish::no;
    default:
        return Finish::no;
    }
}
}
