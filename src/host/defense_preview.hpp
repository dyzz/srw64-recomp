#pragma once
#include <algorithm>
#include <cstdint>

namespace srw64::combat_preview {
// Discrete rand(100) < level * 100 / (16 * faction scale), 801F6D10/6FDC.
inline int defense_percent(unsigned level,unsigned side) {
    const unsigned divisor=side==0?16:32;
    return std::min(100u,(level*100+divisor-1)/divisor);
}
struct Barrier {
    int damage{},strength{},en_cost{};
    bool blocks_shield{};
    const char* kind="none";
    const char* status="none";
};
// 801F7204: only beam attacks; reserve selected weapon EN, then require 5 EN.
// Aura is an all-or-nothing threshold. Other barriers subtract their combined
// strength. Every barrier reaction (including aura breakthrough) skips shields.
inline Barrier barrier(int damage,uint32_t abilities,int aura_bonus,int en,int weapon_cost,bool beam,bool defend) {
    Barrier b;b.damage=damage;
    if(abilities&0x4000){b.kind="aura";b.strength=3000+aura_bonus;}
    else if(abilities&0xA20){b.kind="barrier";b.strength=((abilities&0x800)?2000:0)+((abilities&0x20)?2000:0)+((abilities&0x200)?1000:0);}
    else return b;
    if(defend)b.strength*=2;
    if(!beam){b.status="non_beam";return b;}
    if(en-weapon_cost<5){b.status="en_low";return b;}
    b.blocks_shield=true;
    if(abilities&0x4000) {
        if(damage>b.strength){b.status="broken";return b;}
        b.damage=0;b.status="absorbed";
    } else {
        b.damage=damage<=b.strength?0:std::max(10,damage-b.strength);
        b.status=b.damage?"reduced":"absorbed";
    }
    b.en_cost=5;
    return b;
}
inline int shield_damage(int damage){return damage<20?damage:std::max(10,damage/20*10);}
}
