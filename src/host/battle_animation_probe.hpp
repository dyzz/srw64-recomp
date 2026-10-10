#pragma once
#include "guest_memory.hpp"
#include "rule_probe.hpp"
#include "graphics.hpp"
#include "input_mode.hpp"
#include "json/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>

// Battle-animation control (overlay load_00121560, main loop 801C9710, state
// machine at D_80250000).
//
//   X  ends the presentation early and returns to the map, which then shows the
//      result exactly as it does when the battle animation is switched off.
//
// How the original works, which is what makes this possible:
//
//   State 24 (801D6534) is the attack sequence. Its sub-states 2/3 settle the
//   battle (801F7D6C fills the participant table at 8018B6E8: damage dealt at
//   +0x14, reaction at +0x26, ...), sub-state 8 asks for the fork, and
//   801DFBD0 calls 801D4E6C once, which reads the option bit (8015DDA8 & 4):
//
//     animation on  : 80080188(2) loads the battle overlay and fades. The
//                     overlay is a pure presentation of the side blocks
//                     801F3578 filled; it never touches the roster. When it
//                     returns, 801C7168 resumes the map at state 24 sub-state 4,
//                     whose 801FCA78 call applies the result SILENTLY: 801FB460
//                     writes the HP into the roster, 801FC83C takes EN/ammo,
//                     and no round is queued. Then sub-state 9: the rewards.
//     animation off : the same 801FCA78, but with the bit set 801FB460 leaves
//                     the roster alone and queues each round instead
//                     (D_802277E8 count, D_802277E9[] target, D_80227862[]
//                     damage). The map goes to state 0x4B (801D92B0), whose
//                     sub-states present them: 3 picks the round, 4 the hit
//                     effect, 5 drains the roster HP frame by frame and seeds
//                     the figure, 6 floats it, 7..10 the next round and the
//                     end, which is again state 24 sub-state 9.
//
// So an aborted battle only has to take over the resume frame: when the map is
// back at state 24 sub-state 4, run the animation-off branch of 801D4E6C in its
// place. The roster still holds the pre-battle HP (sub-state 4 has not run),
// the participant table survived the overlay swap, and the chain then computes,
// paces and draws everything itself. The only inputs that branch reads from the
// overlay's own memory are remembered at the fork (Fork below).
//
// This replaces two earlier attempts: drawing the figure from the host, which
// had no settled damage to show, and entering the 0x4B chain at sub-state 0,
// which is not an entry - sub-state 2 (801D8110) is the "next round" step and
// reads the previous round's tables as input.
//
// There is deliberately no "skip to the counterattack" control. Jumping the
// animation's state machine forward (4 -> 10) looks right in the state trace but
// breaks the picture: states 3..9 load the round's sprites, atlases and pilot
// lines and state 9 (801C7960) runs its teardown, while state 10 (801C7BC0)
// only moves the camera. Skipping them left the counterattacker invisible and
// the message box stacked. X is safe because the heavy teardown (8008B950
// releasing all 300 sprite slots, 8008DB2C, 8008AC78) runs after the fade in the
// shared cleanup, not in the states being skipped.
namespace srw64::battle_animation_probe {
// Battle overlay, confirmed live in build/recomp/debug/20260925T015448.967379Z.
inline constexpr uint32_t state_byte=0x80250000;   // s8  master state
inline constexpr uint32_t frame_counter=0x8025020C;// u16 per-state frames
inline constexpr uint32_t fade_request=0x80250205; // s8  -1 = not requested
inline constexpr uint32_t side_base=0x800F97E0;
inline constexpr uint32_t side_stride=0x1074;
inline constexpr uint32_t press_b=0x4000;          // B, i.e. keyboard X or pad B

// Map overlay.
inline constexpr uint32_t map_state=0x80172EB0;
inline constexpr uint32_t map_sub=0x80172EB2;
inline constexpr uint32_t pending_action=0x801602FA;   // u16: 4 = fork this frame
inline constexpr uint32_t animation_flags=0x8015DDA8;  // option bits
inline constexpr uint8_t animation_off=4;
inline constexpr uint32_t attack_context_copy=0x80172EE4; // s16, set at the fork
inline constexpr uint32_t sub_frame=0x8021F464;
inline constexpr uint32_t round_index=0x8022731C;
inline constexpr uint32_t round_count=0x802277E8;  // u8
inline constexpr uint32_t round_cells=0x802277E9;  // u8 target handle per round
inline constexpr uint32_t round_damage=0x80227862; // u16 per round
inline constexpr uint32_t support_flag=0x8022796C; // u8, camera to the defender first
inline constexpr uint32_t support_cell_x=0x80227250,support_cell_y=0x80227254;
inline constexpr uint32_t figure_flag=0x80228528;
inline constexpr uint32_t cell_table=0x800FFA74,cell_stride=0xC4; // by handle
inline constexpr uint8_t map_settling=24;    // 801D6534, the attack sequence
inline constexpr uint8_t settling_apply=4;   // 801D5498, the silent resume
inline constexpr uint8_t map_resolving=0x4B; // 801D92B0, the presentation chain
// Combat participants, stride 0x5C: +0 the map handle, +4 the mech record
// (HP at +4): slot 0 attacks, slot 1 defends, as 801D4E6C reads them
// (D_8018B6E8 / D_8018B744).
inline constexpr uint32_t participants=0x8018B6E8,participant_stride=0x5C;

inline bool probe_enabled() {
    static const bool value=[]{
        const char* p=std::getenv("SRW64_BATTLE_ANIMATION_PROBE");
        return p && std::string_view(p)=="1";
    }();
    return value;
}
// The controls ship on by default; the probe variable only adds the JSON trace.
inline bool controls_enabled() {
    static const bool value=[]{
        const char* p=std::getenv("SRW64_BATTLE_ANIMATION_CONTROLS");
        return !p || std::string_view(p)!="0";
    }();
    return value;
}

inline nlohmann::json& rows() {static nlohmann::json value=nlohmann::json::array();return value;}

inline void record(const uint8_t* ram,int state,const char* note) {
    if(!probe_enabled() || rules::directory().empty())return;
    nlohmann::json row={{"state",state},{"vi",srw64_current_vi()},
        {"frame",guest::read(ram,frame_counter,2)},
        {"fade_requested",int8_t(guest::read(ram,fade_request,1))}};
    if(note)row["note"]=note;
    for(unsigned side=0;side<2;++side) {
        const uint32_t base=side_base+side*side_stride;
        row["side"+std::to_string(side)]={
            {"reaction",int8_t(guest::read(ram,base+0x08,1))},
            {"hp",guest::read(ram,base+0x22,2)},
            {"max_hp",guest::read(ram,base+0x20,2)}};
    }
    rows().push_back(row);
    std::ofstream(rules::directory()/"battle-animation-states.json")<<rows().dump(2)<<'\n';
}

// The guest's own fade request, 80099814(5, 1, 2) - the same call state 21 makes
// once its camera and frame gates pass.
inline void request_fade(uint8_t* ram,recomp_context* ctx) {
    auto call=*ctx;call.r29=rule_probe::pointer(uint32_t(ctx->r29)-0x200);
    call.r4=rule_probe::pointer(5);call.r5=rule_probe::pointer(1);call.r6=rule_probe::pointer(2);
    LOOKUP_FUNC(0x80099814)(ram,&call);
}

// The map owes the player a result once the overlay is gone.
inline bool& owed(){static bool value=false;return value;}
inline unsigned& waited(){static unsigned value=0;return value;}
inline constexpr unsigned give_up_frames=600;

// Game thread, once per frame of the animation loop, before the original runs.
inline void step(uint8_t* ram,recomp_context* ctx) {
    const int state=int8_t(guest::read(ram,state_byte,1));
    static int previous=-128;
    static uint64_t entered=0;
    const uint64_t vi=srw64_current_vi();
    if(state!=previous) {
        if(previous!=-128 && !rows().empty())rows().back()["vi_span"]=vi-entered;
        previous=state;entered=vi;
        record(ram,state,nullptr);
    }
    if(!controls_enabled())return;
    // Read the host's held-input level rather than the guest words: the guest
    // only updates 0x8015CAF0/0x8015D9FA once its own input pass has run.
    // srw64_keyboard_state() is keyboard | controller in the N64 mask plus the
    // host-only bits, so one read covers keyboard X, pad B and R2
    // (docs/design/steam-deck-controls.md).
    //
    // This only applies once a battle is playing. Holding the key beforehand is
    // not a feature and cannot be: X is "cancel" on the pre-battle confirmation
    // page (battle_page.cpp maps back to 0x4000), so holding it while confirming
    // stops the battle from starting at all.
    const uint32_t held=srw64_keyboard_state();
    const bool ending=(held&(press_b|srw64::input::pad_r2))!=0;

    // NOT the original Z-trigger + START path: that sets D_80225810, and the
    // abort branch at 801C97E4 calls 800A5138 and then selects game mode 7 or
    // 0x11 - the title/reset route. Driving it dropped the run to the title
    // screen with the scenario lost (verified 2026-09-25,
    // build/recomp/debug/20260925T023507.767082Z). It is "quit the battle
    // scenario", not "skip this animation".
    //
    // Instead jump to the normal wind-down at state 21, which leaves through the
    // ordinary return path that reloads the map. State 21 would otherwise wait
    // for 801C9DD0 to report the camera settled and its frame counter to pass 10
    // (slti 0xA at 801C813C), which made X take 7.8-8.8s; since the player asked
    // for this battle to be over, request the same fade here and mark it
    // requested (D_80250205 = 1) so state 21 does not ask a second time.
    //
    // States 2..20 qualify. 21+ are excluded: the wind-down already requested the
    // fade. 0 and 1 are too: they build the two sides' actors (the behaviour pointer
    // at side +0x848 +0x98 is written during state 1), and state 21 (801C80C0 ->
    // 801C5E30 -> 801C5F34) walks them; an abort in state 0 read an unset pointer and
    // crashed (a press right after the battle starts, 2026-10-10, every platform).
    // A press in 0 or 1, held or not, takes effect at state 2, the opening, within a
    // fifth of a second of the start.
    static bool early=false;
    if(state<0 || state>20)early=false;
    else if(ending && state<2)early=true;
    const bool abort=(ending || early) && state>=2 && state<=20;
    if(abort)early=false;
    if(abort && int8_t(guest::read(ram,fade_request,1))==-1) {
        owed()=true;waited()=0;
        guest::write8(ram,state_byte,21);
        guest::write16(ram,frame_counter,0);
        request_fade(ram,ctx);
        guest::write8(ram,fade_request,1);
        record(ram,21,"abort-to-winddown");
        std::fprintf(stderr,"SRW64_BATTLE_ANIMATION abort %d->21 vi=%llu\n",
                     state,(unsigned long long)vi);
        previous=21;entered=vi;
    }
}

// The frame before the map forks with the animation ON, remember what the
// animation-off branch would have read from the overlay's own memory. Those
// bytes sit in the region the battle overlay overwrites; everything else that
// branch needs (the participant table at 8018B6E8, the roster at 8015E100, the
// attack context at 80172EE2) lives below the overlays and survives.
struct Fork {
    bool armed=false;
    uint8_t support=0;          // D_8022796C: camera to the defender first
    uint32_t cell_x=0,cell_y=0; // D_80227250/54, the words that camera move uses
    uint16_t handle[2]{},hp[2]{};
};
inline Fork& fork(){static Fork value;return value;}
inline uint32_t mech_of(const uint8_t* ram,unsigned slot) {
    return guest::read(ram,participants+slot*participant_stride+4,4);
}
inline uint16_t hp_of(const uint8_t* ram,unsigned slot) {
    const uint32_t mech=mech_of(ram,slot);
    return mech?uint16_t(guest::read(ram,mech+4,2)):0;
}
inline void remember_fork(uint8_t* ram) {
    Fork& f=fork();
    f.armed=true;
    f.support=uint8_t(guest::read(ram,support_flag,1));
    f.cell_x=guest::read(ram,support_cell_x,4);f.cell_y=guest::read(ram,support_cell_y,4);
    for(unsigned slot=0;slot<2;++slot) {
        f.handle[slot]=uint16_t(guest::read(ram,participants+slot*participant_stride,2));
        f.hp[slot]=hp_of(ram,slot);
    }
}

inline uint32_t call(uint8_t* ram,recomp_context* ctx,uint32_t function,
                     uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=rule_probe::pointer(uint32_t(ctx->r29)-0x200);
    c.r4=rule_probe::pointer(a0);c.r5=rule_probe::pointer(a1);c.r6=rule_probe::pointer(a2);
    LOOKUP_FUNC(function)(ram,&c);
    return uint32_t(c.r2);
}

inline bool& replayed(){static bool value=false;return value;}

// The animation-off branch of 801D4E6C (from .L801D4EE0), run in place of state
// 24 sub-state 4. 801FC994 is left out: the fork already ran it, and it only
// asks 801FB460 with a1=1, which never applies anything. The acted flags (0x40)
// at the end of 801D4E6C are left out too: the fork's animation-on branch set
// them, and the write is an OR anyway.
inline void replay_animation_off(uint8_t* ram,recomp_context* ctx) {
    const Fork& f=fork();
    nlohmann::json row={{"note","replay-animation-off"},{"vi",srw64_current_vi()}};
    for(unsigned slot=0;slot<2;++slot)
        row["hp_before"].push_back({{"handle",f.handle[slot]},{"at_fork",f.hp[slot]},{"now",hp_of(ram,slot)}});
    guest::write8(ram,figure_flag,0);
    call(ram,ctx,0x8009DB8C);
    guest::write8(ram,support_flag,f.support);
    if(f.support==1) {
        call(ram,ctx,0x8008B888,0x9C);
        const uint32_t cell=guest::read(ram,participants+participant_stride,2);
        const auto as_float=[](uint32_t word){float v=float(int32_t(word));uint32_t bits;std::memcpy(&bits,&v,4);return bits;};
        guest::write32(ram,cell_table+cell*cell_stride,as_float(f.cell_x));
        guest::write32(ram,cell_table+cell*cell_stride+4,as_float(f.cell_y));
        call(ram,ctx,0x801CBEB8,cell);
    }
    // 801FCA78 reads the animation-off bit through 801FB460: set, it queues the
    // rounds for the chain to present and leaves the roster alone; clear, it
    // writes the HP straight into the roster and queues nothing.
    const uint8_t flags=uint8_t(guest::read(ram,animation_flags,1));
    guest::write8(ram,animation_flags,flags|animation_off);
    call(ram,ctx,0x801FCA78);
    guest::write8(ram,animation_flags,flags);
    const unsigned rounds=guest::read(ram,round_count,1);
    for(unsigned i=0;i<rounds && i<8;++i)
        row["rounds"].push_back({{"target",guest::read(ram,round_cells+i,1)},
                                 {"damage",guest::read(ram,round_damage+i*2,2)}});
    if(rounds==0) {
        call(ram,ctx,0x801C29DC,uint32_t(int16_t(guest::read(ram,attack_context_copy,2))));
        call(ram,ctx,0x801C8AB4);
    } else {
        guest::write8(ram,map_state,map_resolving);
        guest::write8(ram,map_sub,3);
    }
    guest::write32(ram,round_index,0);
    guest::write32(ram,sub_frame,0);
    if(probe_enabled() && !rules::directory().empty()) {
        rows().push_back(row);
        std::ofstream(rules::directory()/"battle-animation-states.json")<<rows().dump(2)<<'\n';
    }
    std::fprintf(stderr,"SRW64_BATTLE_ANIMATION replay animation-off rounds=%u vi=%llu\n",
                 rounds,(unsigned long long)srw64_current_vi());
    replayed()=true;
}

// Map overlay, once per frame (801DFBD0), before the original dispatcher.
inline void map_step(uint8_t* ram,recomp_context* ctx) {
    if(!controls_enabled())return;
    // 801DF96C asked for the fork; the dispatcher calls 801D4E6C this frame.
    if(guest::read(ram,pending_action,2)==4 && !(guest::read(ram,animation_flags,1)&animation_off))
        remember_fork(ram);
    const uint8_t state=uint8_t(guest::read(ram,map_state,1));
    if(replayed() && state==map_settling) {
        replayed()=false;
        if(probe_enabled() && !rules::directory().empty()) {
            nlohmann::json row={{"note","chain-done"},{"vi",srw64_current_vi()}};
            for(unsigned slot=0;slot<2;++slot)row["hp_after"].push_back(hp_of(ram,slot));
            rows().push_back(row);
            std::ofstream(rules::directory()/"battle-animation-states.json")<<rows().dump(2)<<'\n';
        }
    }
    if(!owed())return;
    // Give up if the map never comes back, so a stale hand-over cannot surface
    // over a later, unrelated battle.
    if(++waited()>give_up_frames){owed()=false;fork().armed=false;return;}
    // The map resumes from an animated battle at state 24 sub-state 4
    // (801C7168 with D_8022722C==1), whose 801FCA78 call applies the result
    // silently. Take that frame over and present it the animation-off way.
    if(state!=map_settling || guest::read(ram,map_sub,1)!=settling_apply)return;
    owed()=false;
    if(!fork().armed) {
        std::fprintf(stderr,"SRW64_BATTLE_ANIMATION no fork snapshot; leaving the silent result\n");
        return;
    }
    fork().armed=false;
    replay_animation_off(ram,ctx);
}
}
