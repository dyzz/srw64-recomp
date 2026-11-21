#pragma once
// A battle quote waits for its MOD voice (docs/design/audio-mods.md §12). The original shows
// each quote for a time fixed in its text record (32 logic frames for nearly all of them,
// about a second), and the animation's quote states (801C840C, states 2/7/13/18) wait for the
// box to close before the attack goes on. A spoken line is usually longer, so the next line
// cut it off. Here the box stays open while its own line is still being spoken: where the
// animation waits for the box, it waits for the voice too, as in later games where the pilot
// finishes the shout before the attack plays.
//
// The text engine (8008D748, every logic frame, slots at D_800FBAB0 step 0x218): +0x02 the
// status (1 shown), +0x20E the mode (2 timed, 3 timed and followed by the next record, as in
// a combination attack's exchange), +0x210 the timer, counted down; when it goes below zero
// the box turns to its next segment (+0x215 of +0x214) or, on the last, closes (mode 2) or
// loads the next record (mode 3). During extra voice time the wrapper compensates for this decrement.
//
// Not while the game is hurried (the voice is then spoken faster instead, music.cpp
// voice_speed), nor past `limit` frames of holding one line. A pressed A lets the line go.
#include "guest_memory.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace srw64::voice_wait {

constexpr uint32_t slots=0x800FBAB0, stride=0x218, pressed=0x8015CAF0;
constexpr uint16_t a_button=0x8000;
constexpr unsigned limit=300;   // logic frames, 10 s at the original speed
// The animation state in overlay load_00121560; states 0/1 begin a battle.
constexpr uint32_t state=0x80250000;

struct Box {uint8_t status{};int8_t mode{};int16_t timer{};uint8_t segments{},segment{};uint16_t record{};};

inline Box read_box(const uint8_t* ram,unsigned slot) {
    const uint32_t p=slots+slot*stride;
    return {uint8_t(guest::read(ram,p+2,1)),int8_t(guest::read(ram,p+0x20E,1)),int16_t(guest::read(ram,p+0x210,2)),
            uint8_t(guest::read(ram,p+0x214,1)),uint8_t(guest::read(ram,p+0x215,1)),uint16_t(guest::read(ram,p,2))};
}

// A timed box's last segment closes on the update after its timer reaches zero.
inline bool timed_last(const Box& b) {
    return b.status==1 && (b.mode==2 || b.mode==3) && (b.segments==0 || b.segment==b.segments);
}

// The state of the wait across frames: the line held and for how long, and a line let go.
struct Wait {int record{-1};unsigned frames{};int released{-1};};
inline Wait wait;   // the game's (game_hooks.cpp hold_quote_for_voice)

// Play the excess first, then let the animation and the rest of the line overlap.
inline bool hold(Wait& w,const Box& b,std::optional<double> seconds,bool hurried,bool a_pressed) {
    if(!timed_last(b) || hurried)return false;
    if(seconds && *seconds<=std::max(0,int(b.timer))/30.0)return false;
    if(int(b.record)!=w.record){w.record=b.record;w.frames=0;}
    if(a_pressed)w.released=b.record;
    if(w.released==int(b.record) || w.frames>=limit)return false;
    ++w.frames;
    return true;
}

// A quote that opens while another line is still being spoken: the reaction to a hit, which
// opens in the middle of the animation (states 9/20) and so is not waited for, is often still
// speaking when the counterattack's quote opens. That quote then waits: its voice is held back
// and the animation's state machine stopped (game_hooks.cpp, wait_for_previous_line) until the
// previous line has been spoken, so the next line no longer cuts it off. The picture is still
// drawn and the text engine still runs; the waiting quote's box is kept full meanwhile.
struct Pending {int record{-1};unsigned frames{};};
inline Pending pending;

// The quote `record` opened while `current` (-1 for none) is being spoken: whether it waits.
// Not for a line A let go (`released`, Wait::released): the player chose to go on.
inline bool defer(int current,unsigned record,bool enabled,bool hurried,int released=-1) {
    return enabled && !hurried && current>=0 && current!=int(record) && current!=released;
}

// One logic frame with a quote waiting: true while it goes on waiting (the animation stays
// stopped). False lets it go: its voice is then started. Not past `limit` frames, nor once A is
// pressed or the game is hurried.
inline bool keep(Pending& p,int current,bool enabled,bool hurried,bool a_pressed) {
    if(p.record<0)return false;
    if(!enabled || hurried || a_pressed || current<0 || current==p.record || p.frames>=limit)return false;
    ++p.frames;
    return true;
}
}
