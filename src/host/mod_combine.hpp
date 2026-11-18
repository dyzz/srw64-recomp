#pragma once
// A package's new unit combined from parts (data/new_units.json "combine", docs/design/
// unit-mod-kit.md §4.4): Final Dancouga from the four beast fighters and Black Wing.
//
// - The combined-unit table: its list of combined units D_800CB40C and the record pointers
//   D_800CB5A4 are read at copies in free RDRAM (generate_cpu.py COMBINE_*), refilled here
//   at every frame boundary with the ROM's 11 and the packages' after them. A record is a
//   byte count, a byte 0, then (part u16, flag u16) pairs; separating (801E8344, 801E7EA8),
//   upgrades carried to the parts and the のりかえ counts read it.
// - The unit is built by a story event (story_mod.hpp "build"): an instance of its own
//   (800A6E68, no pilot), its crew the parts' main pilots in the parts' order (separating
//   finds part i by its number and crew i), never deployed (+0xC 0x80 clear), its upgrade
//   levels and weapon levels taken from the unit it inherits from. 800A74D4 does not give it
//   parts of its own: the parts are the ones on the roster already.
// - The map's 合体 (801E977C, check 1): when the original would combine `over` (223 or 224)
//   and the unit's other parts stand next to the group, not acted, the combine takes them in
//   and makes this unit instead. Combining reads only 801E977C's list, so the HP and EN sums
//   count them.
#include "guest_memory.hpp"
#include "mod_data.hpp"
#include "rule_fixes.hpp"
#include "recomp.h"
#include "json/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace srw64::mod_combine {
using guest::read;using guest::write8;using guest::write16;using guest::write32;
inline constexpr uint32_t list_ram=0x800CB40C, pointers_ram=0x800CB5A4;
inline constexpr uint32_t list_copy=0x807D1000, pointers_copy=0x807D1080, records_copy=0x807D1100;
inline constexpr unsigned capacity=0x20, record_size=0x18;   // generate_cpu.py COMBINE_CAPACITY
inline constexpr uint32_t pool=0x8016A210, pool_side=0x2DF0, pool_size=0x54, pool_count=140;
inline constexpr uint32_t unit_number=0x02, unit_deploy=0x0C, unit_hp=0x04, unit_hp_max=0x06, unit_en=0x08, unit_en_max=0x0A,
    unit_crew=0x34, unit_crew_pilots=0x38, unit_levels=0x4C, pilot_number=0x02;
inline constexpr unsigned stat_count=5, crew_most=5;
inline constexpr uint8_t deploy_bit=0x80;
// 801E977C's state the combine runs on: participant count, their handles ([0] the chosen
// unit), the leader's handle, the combined unit's instance.
inline constexpr uint32_t participants=0x802278F0, participant_handles=0x802278F2, leader=0x802278FC, combined=0x802278E4;
inline constexpr unsigned participant_most=5;
// The map roster: 30 slots a side, 0x14 each (+0 1 on the map, +1 0x80 acted, +0xC the
// unit); handles 0x42 / 0x60 / 0x7E + slot; positions in pixels, 16 a tile.
inline constexpr uint32_t roster=0x8015E100, roster_side=0x258, roster_entry=0x14, positions=0x800FFA74, position_stride=0xC4;
inline constexpr int handle_bases[3]={0x42,0x60,0x7E};
inline constexpr float tile=16.0f;
inline constexpr uint32_t find_on_map=0x801E9514;   // (side, by pilot, number) -> handle or -1

inline void log(const nlohmann::json& row){std::fprintf(stderr,"SRW64_MOD_COMBINE %s\n",row.dump().c_str());}

// --- The table -----------------------------------------------------------------------------

// The copies as they should be: the ROM's list and pointers, then the packages' units.
inline void fill(uint8_t* ram) {
    std::vector<uint16_t> list;std::vector<uint32_t> pointers;
    for(unsigned i=0;i<capacity-1;++i) {
        const uint16_t unit=uint16_t(read(ram,list_ram+i*2,2));
        if(unit==0xFFFF)break;
        list.push_back(unit);pointers.push_back(read(ram,pointers_ram+i*4,4));
    }
    unsigned slot=0;
    for(const auto& [unit,c]:mod_data::combines()) {
        if(list.size()>=capacity-1 || c.parts.size()>crew_most)break;
        const uint32_t record=records_copy+slot++*record_size;
        uint8_t bytes[record_size]{};
        bytes[0]=uint8_t(c.parts.size());
        for(size_t k=0;k<c.parts.size();++k){bytes[2+k*4]=uint8_t(c.parts[k]>>8);bytes[3+k*4]=uint8_t(c.parts[k]);}
        for(unsigned b=0;b<record_size;++b)if(read(ram,record+b,1)!=bytes[b])write8(ram,record+b,bytes[b]);
        list.push_back(uint16_t(unit));pointers.push_back(record);
    }
    list.push_back(0xFFFF);
    for(size_t i=0;i<list.size();++i)if(read(ram,list_copy+uint32_t(i)*2,2)!=list[i])write16(ram,list_copy+uint32_t(i)*2,list[i]);
    for(size_t i=0;i<pointers.size();++i)if(read(ram,pointers_copy+uint32_t(i)*4,4)!=pointers[i])write32(ram,pointers_copy+uint32_t(i)*4,pointers[i]);
}
inline bool combined_unit(unsigned unit){return unit>=table_window::rom_units && mod_data::combines().count(unit);}

// --- Instances -----------------------------------------------------------------------------

// Our (side 0) instance of `unit` in use, the first, or the one whose main pilot is `pilot`.
inline std::optional<uint32_t> instance(const uint8_t* ram,unsigned side,unsigned unit,uint32_t pilot=0) {
    for(uint32_t i=0;i<pool_count;++i) {
        const uint32_t at=pool+side*pool_side+i*pool_size;
        if(!read(ram,at,1) || read(ram,at+unit_number,2)!=unit)continue;
        if(pilot && read(ram,at+unit_crew_pilots,4)!=pilot)continue;
        return at;
    }
    return std::nullopt;
}
inline void call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0,
                 uint32_t* result=nullptr,uint32_t stack1=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);
    c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);c.r7=int32_t(a3);
    write32(ram,uint32_t(c.r29)+0x10,0);write32(ram,uint32_t(c.r29)+0x14,stack1);
    function(ram,&c);
    if(result)*result=uint32_t(c.r2);
}

// The story's build (story_mod.hpp builds): our instance of the combined unit, if there is none.
inline bool build(uint8_t* ram,recomp_context* ctx,unsigned unit,void(*allocate)(uint8_t*,recomp_context*),
                  void(*recompute)(uint8_t*,recomp_context*)) {
    const auto all=mod_data::combines();
    const auto found=all.find(unit);
    if(found==all.end()){log({{"action","build"},{"unit",unit},{"done",false},{"why","no combine"}});return false;}
    if(instance(ram,0,unit)){log({{"action","build"},{"unit",unit},{"done",false},{"why","built already"}});return false;}
    std::vector<uint32_t> crew;
    for(const auto part:found->second.parts) {
        const auto at=instance(ram,0,part);
        const uint32_t pilot=at && read(ram,*at+unit_crew,1)?read(ram,*at+unit_crew_pilots,4):0;
        if(!guest::valid(pilot,0x4C)){log({{"action","build"},{"unit",unit},{"done",false},{"why","part without a pilot"},{"part",part}});return false;}
        crew.push_back(pilot);
    }
    // 800A6E68(unit, no pilot, 0, our side; 0, 1): the second stack word 1 gives it its own
    // weapons (800A68BC), as 800A9970 registers a unit; 800A74D4's parts pass 0 and share.
    uint32_t made=0;
    call(ram,ctx,allocate,unit,0xFFFFFFFF,0,0,&made,1);
    if(!guest::valid(made,pool_size)){log({{"action","build"},{"unit",unit},{"done",false},{"why","no instance"}});return false;}
    for(unsigned k=0;k<crew_most;++k)write32(ram,made+unit_crew_pilots+k*4,k<crew.size()?crew[k]:0);
    write8(ram,made+unit_crew,uint8_t(crew.size()));
    write8(ram,made+unit_deploy,uint8_t(read(ram,made+unit_deploy,1)&~deploy_bit));
    // The levels of the unit it comes from (inherits_from), stats and the carried weapons.
    nlohmann::json carried=nlohmann::json::array();
    if(const auto from=mod_data::predecessor(unit))
        if(const auto old=instance(ram,0,*from)) {
            for(unsigned k=0;k<stat_count;++k)write8(ram,made+unit_levels+k,uint8_t(read(ram,*old+unit_levels+k,1)));
            const uint32_t count=read(ram,*old+rules::unit_weapon_count,1),list=read(ram,*old+rules::unit_weapon_list,4);
            for(const auto& [to,was]:mod_data::carries(unit))
                for(uint32_t i=0;i<count && guest::valid(list,count*rules::weapon_size);++i) {
                    const uint32_t weapon=list+i*rules::weapon_size;
                    if(read(ram,weapon+rules::weapon_number,2)!=was)continue;
                    const uint8_t level=uint8_t(read(ram,weapon+rules::weapon_level,1));
                    if(rules::write_weapon_level(ram,made,uint16_t(to),level))carried.push_back({to,level});
                    break;
                }
        }
    call(ram,ctx,recompute,made);
    write16(ram,made+unit_hp,uint16_t(read(ram,made+unit_hp_max,2)));
    write16(ram,made+unit_en,uint16_t(read(ram,made+unit_en_max,2)));
    nlohmann::json pilots=nlohmann::json::array();
    for(const auto p:crew)pilots.push_back(read(ram,p+pilot_number,2));
    log({{"action","build"},{"unit",unit},{"done",true},{"instance",made},{"crew",pilots},{"weapons",carried}});
    return true;
}

// --- The map's 合体 -------------------------------------------------------------------------

inline float position(const uint8_t* ram,int handle,unsigned axis) {
    const uint32_t bits=read(ram,positions+uint32_t(handle)*position_stride+axis*4,4);
    float f;std::memcpy(&f,&bits,4);return f;
}
inline uint32_t roster_at(unsigned side,int handle){return roster+side*roster_side+uint32_t(handle-handle_bases[side])*roster_entry;}

// After 801E977C(handle, check) found a combine (`ok`): a package's unit that takes it over.
inline void checked(uint8_t* ram,recomp_context* ctx,unsigned check,bool ok,void(*find)(uint8_t*,recomp_context*)) {
    if(!ok || check!=1)return;
    const uint32_t made=read(ram,combined,4);
    if(!guest::valid(made,pool_size))return;
    const unsigned result=read(ram,made+unit_number,2);
    const int lead=int16_t(read(ram,leader,2));
    if(lead<handle_bases[0] || lead>=handle_bases[1])return;   // our side only
    const unsigned side=0;
    for(const auto& [unit,c]:mod_data::combines()) {
        if(std::find(c.over.begin(),c.over.end(),result)==c.over.end())continue;
        const auto own=instance(ram,side,unit,read(ram,made+unit_crew_pilots,4));
        if(!own)continue;
        // The group so far, and the parts it has.
        unsigned count=read(ram,participants,1);
        std::vector<int> group;std::vector<unsigned> have;
        for(unsigned i=0;i<count && i<participant_most;++i) {
            const int h=int16_t(read(ram,participant_handles+i*2,2));
            group.push_back(h);
            const uint32_t u=read(ram,roster_at(side,h)+0xC,4);
            if(guest::valid(u,pool_size))have.push_back(read(ram,u+unit_number,2));
        }
        std::vector<int> extra;bool fine=true;
        for(const auto part:c.parts) {
            if(std::find(have.begin(),have.end(),part)!=have.end())continue;
            uint32_t got=0;
            call(ram,ctx,find,side,0,part,0,&got);
            const int h=int16_t(got);
            if(h<handle_bases[0] || h>=handle_bases[1]){fine=false;break;}
            if(read(ram,roster_at(side,h),1)!=1 || (read(ram,roster_at(side,h)+1,1)&0x80)){fine=false;break;}
            extra.push_back(h);
        }
        if(!fine || extra.empty() || count+extra.size()>participant_most)continue;
        // Each joins the group when it stands next to a member (eight neighbours).
        std::vector<int> pending=extra;bool grew=true;
        while(grew && !pending.empty()) {
            grew=false;
            for(auto it=pending.begin();it!=pending.end();) {
                const bool next=std::any_of(group.begin(),group.end(),[&](int g){
                    return std::fabs(position(ram,*it,0)-position(ram,g,0))<=tile && std::fabs(position(ram,*it,1)-position(ram,g,1))<=tile;});
                if(next){group.push_back(*it);it=pending.erase(it);grew=true;}else ++it;
            }
        }
        if(!pending.empty())continue;
        for(const int h:extra)write16(ram,participant_handles+count++*2,uint16_t(h));
        write8(ram,participants,uint8_t(count));
        write32(ram,combined,*own);
        log({{"action","combine"},{"over",result},{"unit",unit},{"participants",count},{"added",extra}});
        return;
    }
}
}
