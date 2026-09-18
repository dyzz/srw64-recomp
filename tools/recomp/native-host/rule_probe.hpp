#pragma once
#include "rule_fixes.hpp"
#include <array>
#include <vector>

// Opt-in check (SRW64_RULE_PROBE=1). Once both map sides hold units and the script
// waits on a 3D38, call the two original hit-rate routines through the loaded
// overlay for every player/enemy pairing at several HP levels, once per rule set,
// and log the results. Unit HP, the combat participant table and all registers
// are restored afterwards; the only other guest writes are below the current
// stack pointer.
namespace srw64::rule_probe {
using guest::read;
using guest::valid;
using guest::write8;
using guest::write16;
using guest::write32;

inline bool enabled() {
    static const bool value=[]{const char* p=std::getenv("SRW64_RULE_PROBE");return p && std::string_view(p)=="1";}();
    return value;
}

struct Combatant {uint32_t side,slot,handle,unit,pilot,weapon;};
// Map roster: 30 slots per side at 8015E100 + side*0x258 + slot*0x14; +0 status
// (1 = on the map), +0x0C unit. Unit +0x2C/+0x30 weapon count/array (stride
// 0x24), +0x34/+0x38 pilot count/pointers. Handles are 0x42/0x60/0x7E + slot
// per side, as decoded by 801E510C.
inline std::vector<Combatant> roster(const uint8_t* ram,uint32_t side) {
    std::vector<Combatant> result;
    for(uint32_t slot=0;slot<30;++slot) {
        const uint32_t entry=0x8015E100+side*0x258+slot*0x14, unit=read(ram,entry+0x0C,4);
        if(read(ram,entry,1)!=1 || !valid(unit,0x54))continue;
        const uint32_t pilot=read(ram,unit+0x38,4), weapons=read(ram,unit+0x30,4), count=read(ram,unit+0x2C,1);
        if(!read(ram,unit+0x34,1) || !valid(pilot,0x4C) || !count || !valid(weapons,0x24))continue;
        // 聖戦士 pilots attack with their Hyper Aura weapon when the unit has one,
        // so the aura-slash-power rule has something to act on; others use weapon 0.
        uint32_t weapon=weapons;
        if(read(ram,pilot+0x36,1)&0x20)
            for(uint32_t index=0;index<count && index<32;++index)
                if(read(ram,weapons+index*0x24+rules::weapon_condition,1)==rules::hyper_aura_condition)
                    {weapon=weapons+index*0x24;break;}
        result.push_back({side,slot,(side==0?0x42u:side==1?0x60u:0x7Eu)+slot,unit,pilot,weapon});
    }
    return result;
}

inline bool tactical_overlay(const uint8_t* ram) {
    return read(ram,0x801F4384,4)==0x27BDFFA8 && read(ram,0x80204254,4)==0x27BDFFB0 &&
           read(ram,0x801E1F08,4)==0x03E00008 && read(ram,0x801E1F10,4)==0x03E00008;
}

inline nlohmann::json describe(const uint8_t* ram,const Combatant& c) {
    return {{"side",c.side},{"slot",c.slot},{"actor",read(ram,c.pilot+0x02,2)},{"unit_id",read(ram,c.unit+0x02,2)},
            {"weapon_id",read(ram,c.weapon+0x02,2)},{"weapon_power",read(ram,c.weapon+rules::weapon_power,2)},
            {"weapon_condition",read(ram,c.weapon+rules::weapon_condition,1)},
            {"aura_bonus",rules::aura_slash_bonus(ram,c.pilot,c.weapon)},{"level",read(ram,c.pilot+0x05,1)},
            {"skills",read(ram,c.pilot+0x36,1)},{"skill_level",read(ram,c.pilot+0x06,1)},
            {"hit",read(ram,c.pilot+rules::pilot_hit,2)},{"evade",read(ram,c.pilot+rules::pilot_evade,2)},
            {"reaction",read(ram,c.pilot+0x2A,2)},{"mobility",read(ram,c.unit+rules::unit_mobility,2)},
            {"limit",read(ram,c.unit+rules::unit_limit,2)},{"level_bonus",rules::level_bonus(ram,c.pilot)},
            {"hp",read(ram,c.unit+0x04,2)},{"max_hp",read(ram,c.unit+0x06,2)},
            {"potential",{rules::potential_bonus(ram,c.pilot,c.unit,false),rules::potential_bonus(ram,c.pilot,c.unit,true)}}};
}

// Current HP at a percentage of the maximum for the scope, so 底力 bands can be
// compared without a battle.
class HpScope {
public:
    HpScope(uint8_t* ram,uint32_t unit,uint32_t percent):ram(ram),unit(unit),old(uint16_t(read(ram,unit+0x04,2))) {
        const uint32_t max=read(ram,unit+0x06,2);
        write16(ram,unit+0x04,uint16_t(std::max<uint32_t>(1,max*percent/100)));
    }
    ~HpScope(){write16(ram,unit+0x04,old);}
    HpScope(const HpScope&)=delete;
    HpScope& operator=(const HpScope&)=delete;
private:
    uint8_t* ram;
    uint32_t unit;
    uint16_t old;
};

// Every call of the two hit-rate routines, including the game's own battles and
// AI estimates, goes to rule-calls.jsonl while the probe is enabled.
inline void observe(const uint8_t* ram,const char* function,const rules::Combatant& a,const rules::Combatant& d,int32_t result) {
    if(!enabled() || rules::directory().empty())return;
    const bool from_probe=rules::override_fixes()!=rules::no_override;
    const unsigned fixes=from_probe?rules::override_fixes():rules::launch_fixes();
    static std::ofstream out(rules::directory()/"rule-calls.jsonl",std::ios::app);
    out<<nlohmann::json({{"schema","srw64.rule-call.v1"},{"vi",srw64_current_vi()},{"function",function},
        {"source",from_probe?"probe":"game"},{"fixes",rules::names(fixes)},
        {"attacker",{read(ram,a.pilot+0x02,2),read(ram,a.unit+0x02,2)}},
        {"defender",{read(ram,d.pilot+0x02,2),read(ram,d.unit+0x02,2)}},{"hit",result}}).dump()<<'\n';
}

inline gpr pointer(uint32_t value){return gpr(int32_t(value));}

// 801F4384(attacker slot, defender slot, halve): slots 0 and 1 of the table.
inline int32_t battle(uint8_t* ram,recomp_context* ctx,const Combatant& a,const Combatant& d) {
    const recomp_context saved=*ctx;
    std::array<uint8_t,2*rules::participant_size> table{};
    for(uint32_t i=0;i<table.size();++i)table[i]=uint8_t(read(ram,rules::participants+i,1));
    for(const auto& [slot,c]:{std::pair{0u,&a},std::pair{1u,&d}}) {
        const uint32_t base=rules::participants+slot*rules::participant_size;
        write16(ram,base,uint16_t(c->handle));
        write32(ram,base+0x04,c->unit);
        write32(ram,base+0x08,c->weapon);
        write32(ram,base+0x0C,c->pilot);
    }
    ctx->r4=0;ctx->r5=1;ctx->r6=0;
    ctx->r29=pointer(uint32_t(saved.r29)-0x100);
    LOOKUP_FUNC(0x801F4384)(ram,ctx);
    const int32_t result=int16_t(ctx->r2);
    for(uint32_t i=0;i<table.size();++i)write8(ram,rules::participants+i,table[i]);
    *ctx=saved;
    return result;
}

// 80204254(attacker handle, weapon, attacker pilot, attacker unit; stack +0x10
// defender handle, +0x14 defender pilot, +0x18 defender unit).
inline int32_t estimate(uint8_t* ram,recomp_context* ctx,const Combatant& a,const Combatant& d) {
    const recomp_context saved=*ctx;
    const uint32_t sp=uint32_t(saved.r29)-0x100;
    write32(ram,sp+0x10,d.handle);
    write32(ram,sp+0x14,d.pilot);
    write32(ram,sp+0x18,d.unit);
    ctx->r4=a.handle;ctx->r5=pointer(a.weapon);ctx->r6=pointer(a.pilot);ctx->r7=pointer(a.unit);
    ctx->r29=pointer(sp);
    LOOKUP_FUNC(0x80204254)(ram,ctx);
    const int32_t result=int16_t(ctx->r2);
    *ctx=saved;
    return result;
}

// 80203418(attacker handle, weapon, attacker pilot, attacker unit; stack +0x12
// defender handle, +0x14 defender pilot, +0x18 defender unit): the AI's damage
// estimate. Unlike 801F5628 it rolls no critical hit, so it leaves the RNG alone.
inline int32_t damage(uint8_t* ram,recomp_context* ctx,const Combatant& a,const Combatant& d) {
    const recomp_context saved=*ctx;
    const uint32_t sp=uint32_t(saved.r29)-0x100;
    write32(ram,sp+0x10,d.handle);
    write32(ram,sp+0x14,d.pilot);
    write32(ram,sp+0x18,d.unit);
    ctx->r4=a.handle;ctx->r5=pointer(a.weapon);ctx->r6=pointer(a.pilot);ctx->r7=pointer(a.unit);
    ctx->r29=pointer(sp);
    LOOKUP_FUNC(0x80203418)(ram,ctx);
    const int32_t result=int32_t(uint16_t(ctx->r2));
    *ctx=saved;
    return result;
}

// Game thread, after each script poll.
inline void poll(uint8_t* ram,recomp_context* ctx,uint32_t owner) {
    static bool done=false;
    if(done || !enabled() || rules::directory().empty())return;
    const uint32_t pc=read(ram,owner+0x1C,4);
    if(read(ram,pc,2)!=0x3D38 || !tactical_overlay(ram))return;
    const auto players=roster(ram,0), enemies=roster(ram,1);
    if(players.empty() || enemies.empty())return;
    done=true;
    std::vector<std::pair<std::string,unsigned>> sets{{"original",0}};
    for(const auto& entry:rules::catalog)sets.emplace_back(entry.id,entry.fix);
    sets.emplace_back("all",rules::all_fixes);
    std::ofstream out(rules::directory()/"rule-probe.jsonl");
    unsigned rows=0;
    for(const auto& p:players)for(const auto& e:enemies)for(const auto& [a,d]:{std::pair{&p,&e},std::pair{&e,&p}})
    for(const uint32_t percent:{100u,85u,15u,5u}) {
        HpScope attacker_hp(ram,a->unit,percent), defender_hp(ram,d->unit,percent);
        for(const auto& [name,call]:{std::pair{"battle",&battle},std::pair{"estimate",&estimate},std::pair{"damage",&damage}}) {
            nlohmann::json hit=nlohmann::json::object();
            for(const auto& [set,fixes]:sets) {
                rules::Override scope(fixes);
                hit[set]=call(ram,ctx,*a,*d);
            }
            out<<nlohmann::json({{"schema","srw64.rule-probe.v1"},{"vi",srw64_current_vi()},{"function",name},
                {"hp_percent",percent},{"attacker",describe(ram,*a)},{"defender",describe(ram,*d)},{"hit",hit}}).dump()<<'\n';
            ++rows;
        }
    }
    std::fprintf(stderr,"SRW64_RULE_PROBE rows=%u players=%zu enemies=%zu\n",rows,players.size(),enemies.size());
}
}
