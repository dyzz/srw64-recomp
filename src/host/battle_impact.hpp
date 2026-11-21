#pragma once
#include "battle_spread.hpp"
#include "voice_wait.hpp"

namespace srw64::battle_impact {

// 801C8A58 preserves the round's total in the damage queue. The participant's
// remaining damage decreases while a multi-hit sequence is still playing.
constexpr uint32_t round_damage=0x80250270+0x1C;

inline bool defender_slot(const uint8_t* ram,unsigned slot) {
    const auto state=guest::read(ram,voice_wait::state,1);
    return (state==9 || state==20) && slot<2 && slot==guest::read(ram,0x80178B50,2);
}

// The unit actor is the first actor in the reaction block's header (before
// its optional effect actors). These fields belong to the original animation.
inline bool before_finishing_recoil(const uint8_t* ram,uint32_t actor) {
    using guest::read;
    if (!read(ram,actor+0x36,1)) return false;
    const auto phase=read(ram,actor+0x3C,2);
    const auto beat=read(ram,actor+0x3E,2);
    switch (read(ram,actor+0x4C,4)) {
    case 0x801DC984: // Throw: the landing sends 0x55 to the attacker.
        return phase<3 || (phase==3 && beat==0);
    case 0x801DCBA4: // Follow-up throw: last 0x45 launches the longer recoil.
        return phase<5 || (phase==5 && beat!=0);
    case 0x801DCF30: // Three punches: the last has the heavy knockback.
        return beat<3;
    case 0x801D2C54: // Last punch in the ground melee sequence.
        return phase<6;
    case 0x801D4510:
    case 0x801D5128:
        return phase<5;
    case 0x801D363C: // Last kick starts a new knockback in phase four.
        return phase<4 || (phase==4 && read(ram,actor+0x44,2)<10);
    case 0x801D5BEC: // The sash releases its target with a final flash.
        return phase<3;
    case 0x801DF840: // V-MAX passes, with a recovery gap before the last pass.
        return phase<4 || (phase==4 && !read(ram,actor+0x44,2));
    case 0x801D7870: // Three separately animated clone strikes.
        return beat<3;
    case 0x801DA964: // Beam ends and the target is launched.
    case 0x801D9D2C:
    case 0x801D180C: // Held in the attack, then thrown back in a white flash.
    case 0x801D8A28: // Beam pulses followed by a screen flash and launch.
        return phase==0;
    case 0x801D7298: // Last scatter shot starts its flash in phase three.
        return read(ram,actor+0x42,2)<0x55 || phase<3;
    case 0x801CC058: // Four separate funnel shots around the target.
    case 0x801CF240:
    case 0x801CF48C:
    case 0x801D06CC:
        return read(ram,actor+0x42,2)<4;
    case 0x801CB698: // Three crossfire hits, with opposite recoil in the middle.
    case 0x801E18D8: // Three attackers: the last leaves the target in recoil.
        return read(ram,actor+0x42,2)<3;
    case 0x801CB49C: // Returning projectile: second pass reverses direction.
        return read(ram,actor+0x40,2)<2;
    case 0x801D5D98: // Two passes: phase zero starts a fresh flash each pass.
        return beat==0 || (beat==1 && phase==0 && !read(ram,actor+0x44,2));
    case 0x801D8890: // Second pass flashes at 0x2D on the actor's own clock.
        return read(ram,actor+0x34,2)<0x2D;
    case 0x801D5F14: // Three separated passes; the third leads to the long recoil.
        return beat<2 || (beat==2 && (phase==1 || !read(ram,actor+0x44,2)));
    default:
        return false;
    }
}

inline bool waiting(const uint8_t* ram,uint32_t block) {
    namespace bs=srw64::battle_spread;
    using srw64::guest::read;
    const auto defender=read(ram,0x80178B50,2);
    if(!defender_slot(ram,defender))return false;
    const auto side=bs::kSides+defender*bs::kSideSize;
    if(block!=side+bs::kBlocks[1] || !read(ram,round_damage,2))return false;
    // 801C87C8 sets +0x88 on the first hit. 80217F94 (attack-name
    // lettering) releases its actors and flashes on the finishing impact.
    if(!read(ram,bs::kHitBlock+0x88,2))return true;
    // 801C8A58 reserves damage for an optional killing blow. 801C8D6C
    // consumes it when the attack script explicitly signals that strike.
    if(read(ram,0x80250270+0x26,2))return true;
    if(before_finishing_recoil(ram,block+0x4C))return true;
    const auto attack=bs::kSides+(1-defender)*bs::kSideSize+bs::kBlocks[0];
    const auto count=std::min(read(ram,attack+0x26,1),bs::kActorCount);
    unsigned blades=0;
    bool blade_before_strike=false;
    bool storm_active=false, storm_impact=false;
    for(unsigned i=0;i<count;++i) {
        const auto actor=attack+bs::kActors+i*bs::kActorSize;
        const auto routine=read(ram,actor+0x4C,4);
        const bool active=read(ram,actor+0x36,1)!=0;
        if(routine==0x80217F94 && active)return true;
        // The paired fists flash the target on the last change of direction.
        if(routine==0x80210050 && active && read(ram,actor+0x3C,2)<7)return true;
        if(routine==0x8021D6FC && active) {
            // The volley actor crossing the far edge issues 0x54 and enters
            // phase five. Other projectiles retire without issuing that cue.
            storm_active=true;
            storm_impact|=read(ram,actor+0x3C,2)>=5;
        }
        // Sword actors enter phase three when their strike sprite signals contact.
        if(routine==0x80208268) {
            ++blades;
            blade_before_strike|=active && read(ram,actor+0x3C,2)<3;
        }
    }
    return (blades>1 && blade_before_strike) || (storm_active && !storm_impact);
}
}
