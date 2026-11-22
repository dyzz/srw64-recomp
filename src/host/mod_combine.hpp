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
#include <map>
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

// --- The combine recipes -------------------------------------------------------------------
//
// The map's 合体 looks up the chosen unit's recipe: 800A8A90 walks the parts list D_800CAFEC
// (unit numbers, -1), D_800CB390 points at each part's 0x14-byte record (+0 match by unit 0 /
// by main pilot 1, +1 recipe count, +4 recipe pointer, +0x10 the leader), a recipe is 0xC
// bytes (+0 how many others, +2 four numbers, -1 past them, +0xA the combined unit). Both are
// read at copies (generate_cpu.py RECIPE_*) so a package's combined unit with no `over` gets
// a recipe on every part, matched by unit number, its first part the leader.
inline constexpr uint32_t recipe_list_ram=0x800CAFEC, recipe_pointers_ram=0x800CB390;
inline constexpr uint32_t recipe_list_copy=0x807D1400, recipe_pointers_copy=0x807D1480, recipe_records_copy=0x807D1580, recipe_records_end=0x807D2000;
inline constexpr unsigned recipe_capacity=0x40, recipe_record_size=0x14, recipe_size=0xC;

// The parts the packages give a recipe of their own: part -> its combined unit.
using Combines=std::map<unsigned,mod_units::Taken::Combine>;
inline std::map<unsigned,unsigned> recipe_parts(const uint8_t* ram,const Combines& all) {
    std::vector<unsigned> taken;
    for(unsigned i=0;i<recipe_capacity;++i) {
        const uint16_t unit=uint16_t(read(ram,recipe_list_ram+i*2,2));
        if(unit==0xFFFF)break;
        taken.push_back(unit);
    }
    std::map<unsigned,unsigned> parts;
    for(const auto& [unit,c]:all) {
        if(!c.over.empty() || c.parts.size()<2 || c.parts.size()>5)continue;
        if(std::any_of(c.parts.begin(),c.parts.end(),[&](unsigned p){return std::find(taken.begin(),taken.end(),p)!=taken.end() || parts.count(p);}))continue;
        for(const auto p:c.parts)parts[p]=unit;
    }
    return parts;
}
inline bool recipe_part(const uint8_t* ram,unsigned unit){return recipe_parts(ram,mod_data::combines()).count(unit)!=0;}

inline void fill_recipes(uint8_t* ram,const Combines& all=mod_data::combines()) {
    std::vector<uint16_t> list;std::vector<uint32_t> pointers;
    for(unsigned i=0;i<recipe_capacity-1;++i) {
        const uint16_t unit=uint16_t(read(ram,recipe_list_ram+i*2,2));
        if(unit==0xFFFF)break;
        list.push_back(unit);pointers.push_back(read(ram,recipe_pointers_ram+i*4,4));
    }
    uint32_t at=recipe_records_copy;
    const auto put=[&](uint32_t address,const uint8_t* bytes,unsigned size){
        for(unsigned b=0;b<size;++b)if(read(ram,address+b,1)!=bytes[b])write8(ram,address+b,bytes[b]);};
    for(const auto& [part,unit]:recipe_parts(ram,all)) {
        if(list.size()>=recipe_capacity-1 || at+recipe_record_size+recipe_size>recipe_records_end)break;
        const auto& c=all.at(unit);
        const uint32_t recipe=at+recipe_record_size;
        uint8_t record[recipe_record_size]{};
        record[0]=0;record[1]=1;   // by unit number, one recipe
        record[4]=uint8_t(recipe>>24);record[5]=uint8_t(recipe>>16);record[6]=uint8_t(recipe>>8);record[7]=uint8_t(recipe);
        record[0x10]=uint8_t(c.parts[0]>>8);record[0x11]=uint8_t(c.parts[0]);
        uint8_t body[recipe_size];std::memset(body,0xFF,sizeof body);
        body[0]=uint8_t(c.parts.size()-1);body[1]=0;
        unsigned k=0;
        for(const auto p:c.parts)if(p!=part){body[2+k*2]=uint8_t(p>>8);body[3+k*2]=uint8_t(p);++k;}
        body[0xA]=uint8_t(unit>>8);body[0xB]=uint8_t(unit);
        put(at,record,recipe_record_size);put(recipe,body,recipe_size);
        list.push_back(uint16_t(part));pointers.push_back(at);
        at+=recipe_record_size+recipe_size;
    }
    list.push_back(0xFFFF);
    for(size_t i=0;i<list.size();++i)if(read(ram,recipe_list_copy+uint32_t(i)*2,2)!=list[i])write16(ram,recipe_list_copy+uint32_t(i)*2,list[i]);
    for(size_t i=0;i<pointers.size();++i)if(read(ram,recipe_pointers_copy+uint32_t(i)*4,4)!=pointers[i])write32(ram,recipe_pointers_copy+uint32_t(i)*4,pointers[i]);
}

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

// --- The map's 分離 -------------------------------------------------------------------------
//
// 801E8344(handle, kind) answers whether the unit at a map handle can separate. With kind 0 it
// finds the unit in the combined-unit table and, for each part k of its record, compares the
// main pilot of every pool unit with that part's number to the unit's crew k (+0x38 + 4k).
// A package's combined unit carries its parts' pilots only when the story's build made it
// (build above); one made any other way (a deployment record: a story MOD's group, a mini
// stage, the enemy side) has fewer crew than parts, and the original read guest address 0
// through the missing crew pointers: opening its unit menu crashed the host (Windows,
// 2026-11-22, Final Dancouga placed by a deployment record). Such a unit does not separate.
inline bool separable(const uint8_t* ram,int handle,const Combines& all=mod_data::combines()) {
    if(handle<handle_bases[0])return true;   // not a map unit: as the original
    unsigned side=0;
    for(unsigned s=0;s<3;++s)if(handle>=handle_bases[s])side=s;
    if(handle-handle_bases[side]>=int(roster_side/roster_entry))return true;
    const uint32_t made=read(ram,roster_at(side,handle)+0x0C,4);
    if(!guest::valid(made,pool_size))return true;
    const auto found=all.find(read(ram,made+unit_number,2));
    if(found==all.end())return true;   // the original's combined units, and everything else
    const size_t parts=found->second.parts.size();
    if(read(ram,made+unit_crew,1)<parts)return false;
    for(size_t k=0;k<parts;++k)
        if(!guest::valid(read(ram,made+unit_crew_pilots+uint32_t(k)*4,4),0x4C))return false;
    return true;
}

// --- Which side deploys --------------------------------------------------------------------
//
// Deploying takes the instances with +0xC 0x80 (800A8CE8 copies it from the unit record at
// registration; 3D5D, 800AB5C0, sets it on Dancouga or the beast fighters between stages);
// the map's 合体 and 分離 leave it as it is. A package's combined unit deploys combined, as
// Dancouga does after 3D5D 1: it takes the bit while its parts are all on the roster with the
// pilots it carries. With `over`, it follows those units: when 3D5D gives the bit to one of
// them, the package's unit takes it (and its parts that are not theirs, Black Wing, lose it);
// when it goes to their parts, it gives it back to them.
inline void deploy_sync(uint8_t* ram,const Combines& all=mod_data::combines()) {
    const auto bit=[&](uint32_t at){return (read(ram,at+unit_deploy,1)&deploy_bit)!=0;};
    const auto set=[&](uint32_t at,bool on){
        const uint8_t was=uint8_t(read(ram,at+unit_deploy,1)),now=uint8_t(on?was|deploy_bit:was&~deploy_bit);
        if(was!=now)write8(ram,at+unit_deploy,now);};
    for(const auto& [unit,c]:all) {
        const auto own=instance(ram,0,unit);
        if(!own)continue;
        // Its parts as it carries them: part i with crew i as its main pilot.
        std::vector<uint32_t> parts;bool whole=read(ram,*own+unit_crew,1)==c.parts.size();
        for(size_t k=0;k<c.parts.size() && whole;++k) {
            const auto at=instance(ram,0,c.parts[k],read(ram,*own+unit_crew_pilots+uint32_t(k)*4,4));
            if(at)parts.push_back(*at);else whole=false;
        }
        if(c.over.empty()) {
            if(whole){set(*own,true);for(const auto at:parts)set(at,false);}
            else if(bit(*own)){set(*own,false);for(const auto at:parts)set(at,true);}
            continue;
        }
        // The parts the `over` units have of their own (the beast fighters), and the others.
        std::vector<uint32_t> overs;std::vector<unsigned> their;
        for(const auto o:c.over) {
            for(uint32_t i=0;i<pool_count;++i) {
                const uint32_t at=pool+i*pool_size;
                if(read(ram,at,1) && read(ram,at+unit_number,2)==o)overs.push_back(at);
            }
            for(unsigned i=0;i<capacity;++i) {
                const uint16_t u=uint16_t(read(ram,list_ram+i*2,2));
                if(u==0xFFFF)break;
                if(u!=o)continue;
                const uint32_t record=read(ram,pointers_ram+i*4,4);
                const unsigned n=read(ram,record,1);
                for(unsigned k=0;k<n && k<crew_most;++k)their.push_back(read(ram,record+2+k*4,2));
            }
        }
        const auto theirs=[&](uint32_t at){return std::find(their.begin(),their.end(),read(ram,at+unit_number,2))!=their.end();};
        const bool over_on=std::any_of(overs.begin(),overs.end(),bit);
        const bool parts_on=std::any_of(parts.begin(),parts.end(),[&](uint32_t at){return theirs(at) && bit(at);});
        if(whole && over_on) {
            set(*own,true);
            for(const auto at:overs)set(at,false);
            for(const auto at:parts)if(!theirs(at))set(at,false);
        } else if(bit(*own) && (!whole || parts_on)) {
            set(*own,false);
            for(const auto at:parts)if(!theirs(at))set(at,true);
            // Its parts gone (Black Wing left): the original combined unit in use deploys again.
            if(!whole && !parts_on)
                for(auto it=overs.rbegin();it!=overs.rend();++it)if(read(ram,*it+unit_crew,1)){set(*it,true);break;}
        }
    }
}

// 800A5924 copies a combined unit's upgrade levels (+0x4C, five) or 強化パーツ (+0x22 count,
// four) to its parts, as the original's parts are one machine. A package's combine without
// `over` joins units that stand on their own (Earthgain and Virose): they keep theirs.
inline constexpr uint32_t unit_parts_held=0x22;
inline constexpr unsigned kept_size=5;
struct Kept { uint32_t at; uint8_t levels[kept_size], held[kept_size]; };
inline std::vector<Kept> keep_parts(const uint8_t* ram,uint32_t made) {
    std::vector<Kept> kept;
    if(!guest::valid(made,pool_size))return kept;
    const auto all=mod_data::combines();
    const auto found=all.find(read(ram,made+unit_number,2));
    if(found==all.end() || !found->second.over.empty())return kept;
    for(size_t k=0;k<found->second.parts.size();++k)
        for(unsigned side=0;side<2;++side)
            if(const auto at=instance(ram,side,found->second.parts[k],read(ram,made+unit_crew_pilots+uint32_t(k)*4,4))) {
                Kept one{*at,{},{}};
                for(unsigned b=0;b<kept_size;++b){one.levels[b]=uint8_t(read(ram,*at+unit_levels+b,1));one.held[b]=uint8_t(read(ram,*at+unit_parts_held+b,1));}
                kept.push_back(one);break;
            }
    return kept;
}
inline void restore_parts(uint8_t* ram,const std::vector<Kept>& kept) {
    for(const auto& one:kept)
        for(unsigned b=0;b<kept_size;++b){write8(ram,one.at+unit_levels+b,one.levels[b]);write8(ram,one.at+unit_parts_held+b,one.held[b]);}
}
}
