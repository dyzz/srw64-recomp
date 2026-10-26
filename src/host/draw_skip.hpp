#pragma once
// Pictures left undrawn at 3x and 4x on devices that cannot draw that many
// (docs/design/120hz-scheduler.md §3.5). The game still runs and builds every frame; the
// graphics thread hands only some of its display lists to RT64 and completes the others
// at once, and a skipped frame's swap latches when the game makes it instead of on the
// next tick (runtime events.cpp, prepare_runtime_lifecycle.py). The game's logic and its
// results are those of a frame drawn; the screen keeps the last picture drawn.
//
// RT64 takes the Seeker about 9 ms a display list, a little over a tick of 8.3 ms, and the
// next frame starts only once the swap latches on a tick (80080C94): 3x and 4x ran at about
// 55 steps a second, as 2x does, and at 77 drawing one in three. A device is taken to be slow
// when two seconds in a row at 3x or 4x reach under three quarters of the steps they should;
// it stays so for the session.
//
// A slow device draws one display list in three: an odd share, so the game's two pictures
// take turns and neither is left older than three frames. Two pictures and one in two or
// four drawn left one of them undrawn for good, and RT64 then presented 5 times a second at
// one in two. SRW64_DRAW_EVERY=N draws one in N at 3x and 4x whatever the device;
// SRW64_DRAW_EVERY=1 never skips (the comparison).
#include "guest_memory.hpp"
#include "sched_rate.hpp"
#include "gbi_stats.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace srw64::draw_skip {
inline std::atomic_bool slow{false};
inline std::atomic<uint64_t> skipped{0}, holds{0};
// Set with a skipped list, taken by the swap that follows it (srw64_vi_swap_now).
inline std::atomic_bool swap_now{false};
// Set by the game thread: one display list in this many is drawn now; 1 draws them all.
inline std::atomic<unsigned> every{1};

inline unsigned forced() {
    static const unsigned n=sched_rate::env_number("SRW64_DRAW_EVERY",0);
    return n;
}
// SRW64_SWAP_NOW=0: an undrawn frame's swap waits for a tick like any other (the comparison).
inline bool swap_at_once() {
    static const bool a=sched_rate::env_number("SRW64_SWAP_NOW",1)!=0;
    return a;
}

// Game thread, every tick (game_speed::frame): FAST and SPEED as that frame runs. Counts
// the logic steps in one-second windows while hurried at 3x or 4x.
inline void observe(const uint8_t* ram,bool fast,unsigned speed) {
    static unsigned ticks=0,steps=0,low=0;
    const bool counted=fast && speed>=3 && sched_rate::factor()==2;
    if(!counted) {
        ticks=steps=0;low=0;every=1;
        return;
    }
    if(guest::read(ram,sched_rate::logic_count,4)==0)++steps;
    if(++ticks==120) {
        const unsigned want=speed==4 ? 120 : 90;
        low=steps*4<want*3 ? low+1 : 0;
        if(low>=2)slow=true;
        ticks=steps=0;
    }
    const unsigned n=forced();
    every=n ? n : slow.load() ? 3 : 1;
}

// Whether a display list samples a picture the game drew (a texture inside one of the
// screen-sized color images seen so far), as the way into a battle does: it darkens the
// last picture stripe by stripe. RT64 holds nothing current for a frame left undrawn.
// RDRAM as the host keeps it (aligned words read as they are); START physical.
inline bool reads_picture(const uint8_t* rdram,uint32_t start) {
    constexpr uint32_t ram=8u<<20, height=240;
    static uint32_t images[4]{}, image_bytes[4]{};
    static unsigned image_count=0;
    uint32_t segments[16]{}, stack[16];
    unsigned depth=0;
    auto word=[&](uint32_t p){uint32_t v;std::memcpy(&v,rdram+p,4);return v;};
    auto resolve=[&](uint32_t a){return (segments[(a>>24)&0xF]+(a&0x00FFFFFF))&0x1FFFFFFF;};
    uint32_t cursor=start&0x1FFFFFFF;
    for(unsigned n=0;n<200000 && cursor+8<=ram;++n) {
        const uint32_t w0=word(cursor),w1=word(cursor+4);
        cursor+=8;
        switch(w0>>24) {
        case 0xDE:   // G_DL
            if(((w0>>16)&0xFF)==0) {
                if(depth==std::size(stack))return false;
                stack[depth++]=cursor;
            }
            cursor=resolve(w1);
            break;
        case 0xDF:   // G_ENDDL
            if(depth==0)return false;
            cursor=stack[--depth];
            break;
        case 0xDB:   // G_MOVEWORD: G_MW_SEGMENT
            if(((w0>>16)&0xFF)==6)segments[((w0&0xFFFF)>>2)&0xF]=w1&0x00FFFFFF;
            break;
        case 0x64: { // RT64's extended commands (the wide map's)
            const int words=gbi_stats::extended_words(w0&0x00FFFFFF);
            if(words==0)return false;
            cursor+=uint32_t(words-2)*4;
            break;
        }
        case 0xFF: { // G_SETCIMG: a 16-bit image at least 256 wide is a picture
            const uint32_t width=(w0&0xFFF)+1, address=resolve(w1);
            if(((w0>>19)&3)!=2 || width<256)break;
            if(std::find(images,images+image_count,address)!=images+image_count)break;
            const unsigned slot=image_count<std::size(images) ? image_count++ : 0;
            images[slot]=address;image_bytes[slot]=width*height*2;
            break;
        }
        case 0xFD: { // G_SETTIMG
            const uint32_t address=resolve(w1);
            for(unsigned i=0;i<image_count;++i)
                if(address>=images[i] && address<images[i]+image_bytes[i])return true;
            break;
        }
        }
    }
    return false;
}

// Graphics thread, once per display list (RDRAM as the host keeps it, START physical):
// whether this one goes undrawn. A list that samples a picture is drawn, and so are the 60
// after it.
inline bool skip(const uint8_t* rdram,uint32_t start) {
    static unsigned turn=0,hold=0;
    const unsigned n=every.load();
    swap_now=false;
    if(n<=1){turn=0;hold=0;return false;}
    if(reads_picture(rdram,start)) {
        if(hold==0)++holds;
        hold=60;
    }
    if(hold>0){--hold;turn=0;return false;}
    if(turn++%n==0)return false;
    ++skipped;
    swap_now=swap_at_once();
    return true;
}
}  // namespace srw64::draw_skip
