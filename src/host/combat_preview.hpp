#pragma once
#include "rule_probe.hpp"
#include "critical_probability.hpp"
#include "defense_preview.hpp"
#include <cmath>
#include <cstring>

// Game-thread only. The original estimate routines run against an isolated RAM
// image, including their guest stack and scratch data. Never roll combat RNG in
// the live game to populate a panel.
namespace srw64::combat_preview {
using namespace guest;
inline thread_local int test_roll=-1;
inline float critical_threshold(const uint8_t* ram,const rule_probe::Combatant& a,const rule_probe::Combatant& d) {
    return critical_threshold(a.side,read(ram,a.pilot+0x2C,2),read(ram,d.pilot+0x2C,2),int8_t(read(ram,a.weapon+0x14,1)),
        rules::potential_bonus(ram,a.pilot,a.unit,rules::enabled(rules::potential_bands)));
}
inline int critical_percent(const uint8_t* ram,const rule_probe::Combatant& a,const rule_probe::Combatant& d) {
    if(!a.weapon || (read(ram,a.pilot+0x1C,4)&0x20800))return 0;
    // The game compares integer rand(100) < a floating threshold. 4.25 means
    // five successful outcomes, not four. This is conditional on a landed hit.
    return critical_outcomes(critical_threshold(ram,a,d));
}
inline rule_probe::Combatant participant(const uint8_t* ram,unsigned slot) {
    const uint32_t base=rules::participants+slot*rules::participant_size;
    const uint32_t handle=read(ram,base,2),side=handle<0x60?0:handle<0x7E?1:2;
    return {side,0,handle,read(ram,base+4,4),read(ram,base+12,4),read(ram,base+8,4)};
}
struct Estimate {int hit{},damage{},critical{},hit_modifier{},critical_modifier{};};
inline Estimate estimate(uint8_t* scratch,recomp_context* ctx,const rule_probe::Combatant& a,const rule_probe::Combatant& d) {
    Estimate out;
    if(!valid(a.weapon,0x24) || !valid(a.pilot,0x4C) || !valid(d.pilot,0x4C) || !valid(a.unit,0x54) || !valid(d.unit,0x54))return out;
    out.hit=std::clamp(rule_probe::estimate(scratch,ctx,a,d),0,100);
    out.damage=rule_probe::damage(scratch,ctx,a,d);
    out.critical=critical_percent(scratch,a,d);
    out.hit_modifier=int8_t(read(scratch,a.weapon+0xA,1));
    out.critical_modifier=int8_t(read(scratch,a.weapon+0x14,1));
    return out;
}
// Calls below only accept the isolated snapshot used by battle_page. The roll
// override selects a non-critical branch; it never advances the live RNG.
inline int original_call(uint8_t* copy,recomp_context* ctx,uint32_t function,uint32_t a0,uint32_t a1=0,uint32_t a2=0) {
    auto call=*ctx;call.r29=rule_probe::pointer(uint32_t(ctx->r29)-0x200);
    call.r4=rule_probe::pointer(a0);call.r5=rule_probe::pointer(a1);call.r6=rule_probe::pointer(a2);
    LOOKUP_FUNC(function)(copy,&call);return int32_t(call.r2);
}
inline Barrier barrier_for(const uint8_t* copy,const rule_probe::Combatant& attacker,const rule_probe::Combatant& defender,int damage,int command) {
    const uint32_t pilot=read(copy,defender.unit+0x38,4);
    const int bonus=(read(copy,pilot+0x36,1)&0x20)?read(copy,0x80218930+2*read(copy,pilot+6,1),2):0;
    return barrier(damage,read(copy,defender.unit+0x28,4),bonus,read(copy,defender.unit+8,2),
        defender.weapon?read(copy,defender.weapon+0xD,1):0,attacker.weapon && (read(copy,attacker.weapon+4,1)&2),command==2);
}
inline nlohmann::json damage_preview(uint8_t* copy,recomp_context* ctx,const rule_probe::Combatant& a,const rule_probe::Combatant& d,unsigned slot) {
    if(!a.weapon)return {{"damage",0},{"damage_raw",0},{"critical_damage",0},{"damage_if_shield",0},{"barrier",{{"kind","none"},{"status","none"}}}};
    const uint32_t row=rules::participants+slot*rules::participant_size;
    const int command=read(copy,rules::participants+(1-slot)*rules::participant_size+0x10,1);
    const auto old_critical=read(copy,row+0x16,1);
    const int previous_roll=test_roll;test_roll=1000000;
    const int raw=uint16_t(original_call(copy,ctx,0x801F5628,slot,1-slot,command==2));
    test_roll=0;
    const int critical_raw=uint16_t(original_call(copy,ctx,0x801F5628,slot,1-slot,command==2));
    test_roll=previous_roll;write8(copy,row+0x16,old_critical);
    const auto b=barrier_for(copy,a,d,raw,command),cb=barrier_for(copy,a,d,critical_raw,command);
    // Mercy and scripted survival restrictions apply after special defenses.
    const auto finish=[&](int damage){return uint16_t(original_call(copy,ctx,0x801F5B78,slot,1-slot,damage));};
    return {{"damage",finish(b.damage)},{"damage_raw",raw},{"critical_damage",finish(cb.damage)},
        {"damage_if_shield",finish(shield_damage(b.damage))},
        {"barrier",{{"kind",b.kind},{"status",b.status},{"strength",b.strength},{"en_cost",b.en_cost},{"blocks_shield",b.blocks_shield}}}};
}
inline nlohmann::json defense_preview(const uint8_t* copy,const rule_probe::Combatant& self,const rule_probe::Combatant& incoming,const nlohmann::json& attack) {
    const uint32_t pilot=read(copy,self.unit+0x38,4),skills=read(copy,pilot+0x36,1),equipment=read(copy,self.unit+0x20,1);
    const bool parry_skill=(skills&equipment&1) && read(copy,pilot+7,1),shield_skill=(skills&equipment&2) && read(copy,pilot+8,1);
    const bool clone_skill=read(copy,self.unit+0x28,4)&0x163011;
    const bool sure=incoming.weapon && (read(copy,read(copy,incoming.unit+0x38,4)+0x1C,4)&0x80);
    const bool cut=incoming.weapon && (read(copy,incoming.weapon+4,1)&8);
    const bool no_hit=!incoming.weapon; // rates are conditional on reaching the defense check
    const bool barrier_blocks=attack.at("barrier").value("blocks_shield",false);
    const char* parry_reason=no_hit?"no_attack":!parry_skill?"no_skill":sure?"sure_hit":!cut?"uncuttable":"ready";
    const char* clone_reason=no_hit?"no_attack":!clone_skill?"no_skill":sure?"sure_hit":read(copy,pilot+0x20,2)<130?"morale_low":"ready";
    const char* shield_reason=no_hit?"no_attack":!shield_skill?"no_skill":barrier_blocks?"barrier_first":"ready";
    return {{"parry",std::string_view(parry_reason)=="ready"?defense_percent(read(copy,pilot+7,1),self.side):0},
        {"shield",std::string_view(shield_reason)=="ready"?defense_percent(read(copy,pilot+8,1),self.side):0},
        {"clone",std::string_view(clone_reason)=="ready"?50:0},{"parry_reason",parry_reason},{"shield_reason",shield_reason},{"clone_reason",clone_reason},
        {"incoming_cuttable",cut}};
}

}
