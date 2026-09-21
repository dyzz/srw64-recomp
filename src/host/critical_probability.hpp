#pragma once
#include <algorithm>
#include <cmath>
namespace srw64::combat_preview {
// 801F47B0 compares rand(100), an integer 0..99, with this float.
inline float critical_threshold(unsigned side,int attacker_skill,int defender_skill,int signed_weapon_bonus,int potential) {
    float rate=attacker_skill-defender_skill+signed_weapon_bonus;
    rate=side==0?rate+potential:rate/4.f;
    return std::max(1.f,rate);
}
inline int critical_outcomes(float threshold,bool boosted=false) {
    return boosted?0:std::clamp(int(std::ceil(threshold)),0,100);
}
}
