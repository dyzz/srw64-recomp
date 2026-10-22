#pragma once
#include "guest_memory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Damage past 65535 (docs/gameplay/cheats.md §8). 801F5628 rounds a battle's damage down
// to 65535.0 (801F5ACC) and the settlement keeps it in the attacker's participant row
// (8018B6E8 + slot * 0x5C, u16 at +0x14). The special defenses (801F7204(defender,
// attacker): a barrier takes a fixed amount off, S防御 halves to tens; they stop at the
// first that works) and てかげん (801F5B78(attacker, defender, damage)) change that u16
// afterwards, in both settlements (801F7D6C, the map weapon's 801FE068), and the kill, the
// money and the EXP all read it. With the switch on the uncapped damage is kept per slot and taken through the
// same changes, and the u16 ends as min(real, 65535): HP is a u16 too, so 65535 kills.
namespace srw64::overkill {
inline constexpr uint32_t participants=0x8018B6E8,participant_size=0x5C,damage_field=0x14;
inline constexpr unsigned slots=8;
inline constexpr double cap=65535;

struct State {double last=-1;double real[slots]{};};
inline State& state(){static State value;return value;}
inline uint32_t field(unsigned slot){return participants+slot*participant_size+damage_field;}

// The generated 801F5ACC, with the damage before the cap.
inline void seen(float damage){state().last=damage;}
// The last 801F5628's damage as it would have returned it without the cap (it truncates).
inline double uncapped(){return std::floor(std::min(double(state().last),4.0e9));}
// Around 801F5628 (a0 the attacker's slot): the slot keeps the uncapped damage when it
// went past the cap, and nothing otherwise.
inline void begin(){state().last=-1;}
inline void end(unsigned slot,bool on) {
    auto& s=state();
    if(slot<slots)s.real[slot]=on && s.last>cap?uncapped():0;
}
inline double real(unsigned slot){return slot<slots?state().real[slot]:0;}
// The damage the u16 shows: the uncapped value while one is kept, else what is there.
inline uint32_t held(unsigned slot,uint32_t value){return real(slot)>0?uint32_t(std::min(real(slot),cap)):value;}

// S防御: halved and rounded down to tens, at least 10 (801F6FDC).
inline double halved(double damage){return std::max(10.0,double(int64_t(damage/2/10))*10);}
// 801F7204 changed the slot's u16 from `before` to `after`; the same change to the
// uncapped damage, and the u16 it should hold.
inline uint32_t defended(unsigned slot,uint32_t before,uint32_t after) {
    if(slot>=slots || state().real[slot]<=0)return after;
    double& real=state().real[slot];
    if(after==before)return held(slot,after);
    if(after==0)real=0;                                     // absorbed
    else if(after==uint32_t(halved(before)))real=halved(real);  // S防御
    else if(after<before)real=std::max(10.0,real-double(before-after));  // a barrier
    else real=0;                                            // anything else: the game's value
    return held(slot,after);
}
// The figures on screen. Both presentations show min(damage, HP left), and drain the HP
// by that same value from a copy; only the digits are given the full damage here: up to
// 65535 by the game itself, past it drawn by ui_text (real_figure) over the game's 65535.
//
// The map (animation off): 801FB8B4(handle, damage) queues min(damage, HP) at 80227862[]
// (count 802277E8, target 802277E9[]); 801FBBD4 may sort the queue after. Each push is kept
// as (target, queued, damage) and the float's read (801D8CB0, round 8022731C) finds its
// entry by target and queued value. The damage is the u16 801FB460 passes (the attacker's
// row +0x14), or past 65535 the attacker's kept value (the row whose handle is not the
// target's). Machine 0x95 (always 10) and ガイヤー 0xB9 (HP - 10) keep the game's figure.
inline constexpr uint32_t round_count=0x802277E8,round_cells=0x802277E9,round_damage=0x80227862,round_index=0x8022731C;
inline constexpr uint32_t roster=0x8015E100,roster_side=0x258,roster_entry=0x14,roster_unit=0xC;
struct Figure {uint8_t cell;uint16_t queued;uint32_t damage;};
inline std::vector<Figure>& figures(){static std::vector<Figure> value;return value;}
inline uint32_t unit_of(const uint8_t* ram,unsigned handle) {
    const unsigned side=handle<0x60?0:handle<0x7E?1:2,slot=handle-(side==0?0x42:side==1?0x60:0x7E);
    return guest::read(ram,roster+side*roster_side+slot*roster_entry+roster_unit,4);
}
inline void queued(const uint8_t* ram,unsigned handle,uint32_t damage,unsigned before) {
    if(!before)figures().clear();
    if(guest::read(ram,round_count,1)!=before+1)return;
    const uint16_t shown=uint16_t(guest::read(ram,round_damage+2*before,2));
    const uint32_t unit=unit_of(ram,handle);
    const unsigned number=guest::valid(unit,0x54)?guest::read(ram,unit+2,2):0;
    uint32_t full=damage&0xFFFF;
    if(full==0xFFFF)
        for(unsigned slot=0;slot<2;++slot)
            if(guest::read(ram,participants+slot*participant_size,2)!=handle && real(slot)>0){full=uint32_t(real(slot));break;}
    figures().push_back({uint8_t(handle),shown,number==0x95 || number==0xB9?shown:std::max<uint32_t>(shown,full)});
}
inline uint32_t map_figure(const uint8_t* ram,uint32_t shown) {
    const unsigned i=guest::read(ram,round_index,4);
    if(i>=0x3C)return shown;
    const uint8_t cell=uint8_t(guest::read(ram,round_cells+i,1));
    for(const auto& f:figures())if(f.cell==cell && f.queued==shown)return f.damage;
    return shown;
}
// The battle animation: the two sides' records at 800F97E0 (0x1074 each) keep the full damage
// dealt at +0x1A (801F3578 copies row 0's +0x14 into record D_80178C78, row 1's into the
// other); the side taking it (D_80178B50) shows min(that, HP) from +0x1C. Past 65535 the
// kept value of the row that copied it.
inline constexpr uint32_t battle_records=0x800F97E0,battle_record_size=0x1074,battle_record_damage=0x1A,battle_target=0x80178B50,
    battle_row0_record=0x80178C78;
inline uint32_t battle_figure(const uint8_t* ram,uint32_t shown) {
    if(!shown)return shown;   // nothing to show: the original's text-only banner
    const int side=int16_t(guest::read(ram,battle_target,2));
    const unsigned record=side==0?1:0;
    const uint32_t dealt=guest::read(ram,battle_records+record*battle_record_size+battle_record_damage,2);
    if(dealt==0xFFFF) {
        int row=-1;
        for(unsigned slot=0;slot<2;++slot)
            if(guest::read(ram,field(slot),2)==0xFFFF && real(slot)>0)row=row<0?int(slot):-2;
        if(row==-2)row=guest::read(ram,battle_row0_record,2)==record?0:1;   // both past it: by the copy
        if(row>=0 && real(unsigned(row))>0)return uint32_t(real(unsigned(row)));
    }
    return std::max(shown,dealt);
}

// てかげん changed the damage: the game's value stands.
inline void spared(unsigned slot,uint32_t before,uint32_t after) {
    if(slot<slots && after!=before)state().real[slot]=0;
}
}
