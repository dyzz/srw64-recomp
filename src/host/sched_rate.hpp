#pragma once
// 120 Hz scheduler experiment (docs/design/60fps.md §10), off unless SRW64_VI_RATE=120.
// The VI thread then retraces at 120 Hz (prepare_runtime_lifecycle.py: the runtime's speed
// multiplier, which only the VI thread reads), the game's logic interval doubles so 1x
// stays 30 steps a second, the host's own VI count keeps 60 Hz units, and the audio
// thread hears every other retrace so it synthesizes 60 times a second as before.
//
// SRW64_VI_AUDIO=all sends the audio thread every retrace (the A/B for the audio claim);
// SRW64_FAST_INTERVAL=1|2 is the hurried interval at 120 Hz (default 1, 4x).
#include "guest_memory.hpp"
#include "ultramodern/ultra64.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace srw64::sched_rate {
inline uint32_t env_number(const char* name,uint32_t fallback) {
    const char* v=std::getenv(name);
    return v && *v ? uint32_t(std::strtoul(v,nullptr,10)) : fallback;
}
inline uint32_t factor() {
    static const uint32_t f=env_number("SRW64_VI_RATE",60)==120 ? 2 : 1;
    return f;
}
inline uint32_t fast_interval() {
    static const uint32_t i=env_number("SRW64_FAST_INTERVAL",1)==2 ? 2 : 1;
    return i;
}
inline bool gate_audio() {
    static const bool g=[]{const char* v=std::getenv("SRW64_VI_AUDIO");return !(v && std::string_view(v)=="all");}();
    return g;
}

inline std::atomic<uint64_t> retraces{0}, audio_skipped{0};
// Ticks between logic steps and between pictures the VI shows (1..5, 6 and more), to see
// the cadence; a picture latches when the host allows (latch(), runtime events.cpp).
inline std::atomic<uint64_t> logic_ticks[6]{}, picture_ticks[6]{};
inline bool latch_paced() {
    static const bool p=[]{const char* v=std::getenv("SRW64_VI_LATCH");return !(v && std::string_view(v)=="0");}();
    return p;
}
// VI thread, every tick: whether the next picture may latch now. In 2x at 120 Hz (interval 2)
// a new picture waits for an even tick: a frame drawn every second tick then shows every
// second tick, not after one or three as the GPU's finish falls either side of a tick (Mac).
// A GPU slower than two ticks a frame (the Seeker) only loses by the wait: each picture held
// for an even tick holds back the next frame's start (80080C94), 46 a second instead of 53.
// So one already three ticks late goes at once, and when 4 of the last 32 changes came that
// late the device is taken to be slow and pictures latch freely for 10 seconds.
// 1x (a frame every 4 ticks) keeps its cadence without this, and its interpolation is smoother
// on the 120 Hz grid (60 presents on Mac instead of 54); 4x draws on every tick.
inline std::atomic<uint64_t> latch_slow_spells{0};
inline bool latch(const uint8_t* ram,uint64_t tick) {
    if(factor()==1 || !latch_paced() || !ram || guest::read(ram,0x8010F0C8,4)!=2)return true;
    static uint64_t last_change=0,free_until=0;
    static unsigned changes=0,late=0;
    if(tick<free_until)return true;
    if(osViGetNextFramebuffer()==osViGetCurrentFramebuffer())return true;   // nothing new to show
    if(tick%2!=0 && tick-last_change<3)return false;
    if(tick-last_change>=3)++late;
    if(++changes==32) {
        if(late>=4){free_until=tick+1200;++latch_slow_spells;}
        changes=late=0;
    }
    last_change=tick;
    return true;
}

// The original's scheduler (osScheduler at 8015D730) sends each retrace to every client in
// its list (+0x260, nodes {next, queue}); the audio thread's node is 800D2360 (8007E6D4).
constexpr uint32_t audio_client=0x800D2360;

// Scheduler thread, 80080A14 (the retrace handler). On the retraces the audio thread should
// not hear, the audio node leaves the list for this one call and goes back where it was.
template<class F> void retrace(uint8_t* ram,uint32_t sc,F&& original) {
    const uint64_t tick=++retraces;
    {
        // The logic gate's count (D_80172D0C) is 1 on the first tick after a step reset it.
        static uint64_t last_logic=0,last_picture=0;
        static uint32_t picture=0;
        if(guest::read(ram,0x80172D0C,4)==1) {
            if(last_logic)++logic_ticks[std::min<uint64_t>(tick-last_logic,6)-1];
            last_logic=tick;
        }
        if(const uint32_t now=uint32_t(osViGetCurrentFramebuffer());now!=picture) {
            if(last_picture && picture)++picture_ticks[std::min<uint64_t>(tick-last_picture,6)-1];
            picture=now;last_picture=tick;
        }
    }
    if(factor()==1 || !gate_audio() || tick%factor()==0) { original(); return; }
    uint32_t slot=sc+0x260;
    while(uint32_t node=guest::read(ram,slot,4)) {
        if(node==audio_client)break;
        slot=node;
    }
    if(guest::read(ram,slot,4)!=audio_client) { original(); return; }
    guest::write32(ram,slot,guest::read(ram,audio_client,4));
    ++audio_skipped;
    original();
    guest::write32(ram,slot,audio_client);
}
}  // namespace srw64::sched_rate
