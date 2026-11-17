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
// loads the next record (mode 3). Keeping the timer at 1 on the last segment holds it there.
//
// Not while the game is hurried (the voice is then spoken faster instead, music.cpp
// voice_speed), nor past `limit` frames of holding one line. A pressed A lets the line go.
#include "guest_memory.hpp"
#include <cstdint>

namespace srw64::voice_wait {

constexpr uint32_t slots=0x800FBAB0, stride=0x218, pressed=0x8015CAF0;
constexpr uint16_t a_button=0x8000;
constexpr unsigned limit=300;   // logic frames, 10 s at the original speed

struct Box {uint8_t status{};int8_t mode{};int16_t timer{};uint8_t segments{},segment{};uint16_t record{};};

inline Box read_box(const uint8_t* ram,unsigned slot) {
    const uint32_t p=slots+slot*stride;
    return {uint8_t(guest::read(ram,p+2,1)),int8_t(guest::read(ram,p+0x20E,1)),int16_t(guest::read(ram,p+0x210,2)),
            uint8_t(guest::read(ram,p+0x214,1)),uint8_t(guest::read(ram,p+0x215,1)),uint16_t(guest::read(ram,p,2))};
}

// Whether this box, shown on its own clock and at its last segment, is about to close or
// turn to the next record this frame.
inline bool closing(const Box& b) {
    return b.status==1 && (b.mode==2 || b.mode==3) && (b.segments==0 || b.segment==b.segments) && b.timer<1;
}

// The state of the wait across frames: the line held and for how long, and a line let go.
struct Wait {int record{-1};unsigned frames{};int released{-1};};

// One logic frame: true when the box's timer is to be set back to 1. `speaking` is whether
// `b.record` is still being spoken (decoding, its delay, or playing).
inline bool hold(Wait& w,const Box& b,bool speaking,bool hurried,bool a_pressed) {
    if(!closing(b) || !speaking || hurried)return false;
    if(int(b.record)!=w.record){w.record=b.record;w.frames=0;}
    if(a_pressed)w.released=b.record;
    if(w.released==int(b.record) || w.frames>=limit)return false;
    ++w.frames;
    return true;
}
}
