#pragma once
// A package's new weapon as a combination attack (docs/design/unit-mod-kit.md §9). The
// original knows its 25 by the table D_80218A6C: 8020178C (each time a weapon list opens)
// and the settlement 801F7D6C look the weapon's number up there and do nothing for one
// they do not find. So for a package's, the host does what 80200530 does
// (combo_attacks.hpp; the package's units each lead with the others as partners):
//   locks()  after 8020178C: usable -> the power (the participants' component weapons'
//            mean current power times the multiplier) at +0x06 and the lock (+0x22 0x04)
//            off; not usable -> the lock on.
//   spend()  before 801FC83C (EN and ammo after an attack): the participants into the
//            participant table's list (+0x44 count, +0x48 unit pointers, the attacker's
//            own first), which the settlement leaves empty for a weapon it does not know;
//            with no one listed nobody's EN would go down.
#include "combo_attacks.hpp"
#include "enemy_pilot_page.hpp"
#include "guest_memory.hpp"
#include "mod_pilots.hpp"
#include "recomp.h"
#include "funcs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <optional>
#include <vector>

namespace srw64::mod_combos {
using guest::read;
inline constexpr uint32_t roster=0x8015E100,roster_side=0x258,roster_entry=0x14,positions=0x800FFA74,position_stride=0xC4;
inline constexpr int handle_bases[3]={0x42,0x60,0x7E};
inline constexpr uint32_t participants=0x8018B6E8,participant_size=0x5C,listed=0x44,list=0x48;
inline constexpr unsigned list_most=5;
inline constexpr int tile=16;

inline unsigned side_of(int handle){return handle<0x60?0:handle<0x7E?1:2;}
inline uint32_t roster_unit(const uint8_t* ram,unsigned side,int handle) {
    const int slot=handle-handle_bases[side];
    if(slot<0 || slot>=30 || read(ram,roster+side*roster_side+uint32_t(slot)*roster_entry,1)!=1)return 0;
    const uint32_t unit=read(ram,roster+side*roster_side+uint32_t(slot)*roster_entry+0xC,4);
    return guest::valid(unit,0x54)?unit:0;
}
// The handle of a unit on the field, -1 when it is not.
inline int handle_of(const uint8_t* ram,uint32_t unit) {
    for(unsigned side=0;side<3;++side)
        for(unsigned slot=0;slot<30;++slot)
            if(roster_unit(ram,side,handle_bases[side]+int(slot))==unit)return handle_bases[side]+int(slot);
    return -1;
}
inline int lead_pilot(const uint8_t* ram,uint32_t unit) {
    const uint32_t pilot=read(ram,unit+0x38,4);
    return guest::valid(pilot,0x4C)?int(mod_pilots::combo_number(read(ram,pilot+2,2))):-1;
}

struct Result { bool ok=false; std::vector<uint32_t> units; unsigned power=0; };
// 80200530 for a package's attack: the unit's own row, every partner the side's first on
// the field (801E9514), one connected group, each one's EN and morale for the weapon and
// the skill it names, a pilot pairing, then the power.
inline Result evaluate(uint8_t* ram,recomp_context* ctx,int handle,uint32_t unit,uint32_t weapon,const combo_attacks::Attack& attack) {
    Result r;
    const unsigned side=side_of(handle);
    const int number=int(read(ram,unit+2,2));
    const auto* row=combo_attacks::lead_row(attack,number,lead_pilot(ram,unit));
    if(!row)return r;
    std::vector<std::pair<uint32_t,int>> members={{unit,handle}};
    for(const int16_t id:row->partners) {
        uint32_t found=0;
        enemy_pilot_page::call(ram,ctx,LOOKUP_FUNC(0x801E9514),side,0,uint32_t(uint16_t(id)),0,0,&found);
        const int h=int16_t(found);
        const uint32_t u=h>=0?roster_unit(ram,side,h):0;
        if(!u)return r;
        members.push_back({u,h});
    }
    const auto pos=[&](int h,unsigned axis){float f;const uint32_t bits=read(ram,positions+uint32_t(h)*position_stride+axis*4,4);std::memcpy(&f,&bits,4);return int(f);};
    std::vector<bool> seen(members.size(),false);std::vector<size_t> open={0};seen[0]=true;
    while(!open.empty()) {
        const size_t a=open.back();open.pop_back();
        for(size_t b=0;b<members.size();++b)
            if(!seen[b] && std::abs(pos(members[a].second,0)-pos(members[b].second,0))<=tile && std::abs(pos(members[a].second,1)-pos(members[b].second,1))<=tile){seen[b]=true;open.push_back(b);}
    }
    if(std::find(seen.begin(),seen.end(),false)!=seen.end())return r;
    std::vector<int> leads;
    for(const auto& [u,h]:members) {
        const uint32_t pilot=read(ram,u+0x38,4);
        if(read(ram,u+8,2)<read(ram,weapon+0xD,1))return r;
        if(guest::valid(pilot,0x4C) && read(ram,pilot+0x20,2)<read(ram,weapon+0xE,1))return r;
        if(read(ram,weapon+0xF,1)>=2) {
            uint32_t refused=0;
            enemy_pilot_page::call(ram,ctx,LOOKUP_FUNC(0x801E6214),side,pilot,weapon,u,1,&refused);
            if(refused&0xFF)return r;
        }
        leads.push_back(lead_pilot(ram,u));
    }
    if(!combo_attacks::pilots_met(attack,leads))return r;
    double sum=0;
    for(const auto& [u,h]:members) {
        const auto part=combo_attacks::component(attack,int(read(ram,u+2,2)));
        const uint32_t w=part?combo_attacks::unit_weapon(ram,u,*part):0;
        if(!w)return r;
        sum+=read(ram,w+6,2);
    }
    const double power=std::trunc(sum/double(members.size())*double(attack.multiplier));
    if(power<1)return r;
    r.ok=true;r.power=unsigned(std::min(power,65535.0));
    for(const auto& [u,h]:members)r.units.push_back(u);
    return r;
}
// The package's attack a weapon instance is, when it is one.
inline std::optional<combo_attacks::Attack> package_attack(const uint8_t* ram,uint32_t weapon) {
    if(!guest::valid(weapon,0x24) || !(read(ram,weapon+0x22,1)&combo_attacks::flag_combo))return std::nullopt;
    auto attack=combo_attacks::find(recomp::get_rom(),uint16_t(read(ram,weapon+2,2)));
    if(!attack || !combo_attacks::from_package(*attack))return std::nullopt;
    return attack;
}

// After 8020178C(handle, unit, 0).
inline void locks(uint8_t* ram,recomp_context* ctx,int handle,uint32_t unit) {
    if(handle<0 || !guest::valid(unit,0x54))return;
    const unsigned count=read(ram,unit+0x2C,1);
    const uint32_t array=read(ram,unit+0x30,4);
    if(!count || !guest::valid(array,count*0x24))return;
    for(unsigned i=0;i<count;++i) {
        const uint32_t w=array+i*0x24;
        const auto attack=package_attack(ram,w);
        if(!attack)continue;
        const auto r=evaluate(ram,ctx,handle,unit,w,*attack);
        const uint8_t flags=uint8_t(read(ram,w+0x22,1));
        if(r.ok){guest::write16(ram,w+6,uint16_t(r.power));guest::write8(ram,w+0x22,uint8_t(flags&~combo_attacks::flag_locked));}
        else guest::write8(ram,w+0x22,uint8_t(flags|combo_attacks::flag_locked));
    }
}
// Before 801FC83C(k): participant k's list when it attacks with a package's attack.
inline void spend(uint8_t* ram,recomp_context* ctx,unsigned k) {
    const uint32_t entry=participants+k*participant_size;
    const uint32_t weapon=read(ram,entry+8,4),unit=read(ram,entry+4,4);
    if(!guest::valid(unit,0x54) || read(ram,entry+listed,1))return;
    const auto attack=package_attack(ram,weapon);
    if(!attack)return;
    const int handle=handle_of(ram,unit);
    auto r=handle>=0?evaluate(ram,ctx,handle,unit,weapon,*attack):Result{};
    if(!r.ok)r.units={unit};   // at least its own EN, as for any weapon
    const unsigned n=unsigned(std::min<size_t>(r.units.size(),list_most));
    for(unsigned i=0;i<n;++i)guest::write32(ram,entry+list+i*4,r.units[i]);
    guest::write8(ram,entry+listed,uint8_t(n));
}
}
