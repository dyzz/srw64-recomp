#pragma once
#include "combat_preview.hpp"
#include <tuple>

namespace srw64::combat_preview {
// Formula terms, not a second addition to the already-calculated hit rate.
inline nlohmann::json pilot_effects(const uint8_t* ram,const rule_probe::Combatant& c) {
    using nlohmann::json;
    json out=json::array();
    const unsigned flags=read(ram,c.pilot+0x36,1),level=read(ram,c.pilot+6,1);
    const auto add=[&](const char* id,int hit,int evade,int critical=0){
        out.push_back({{"id",id},{"level",level},{"hit",hit},{"evade",evade},{"critical",critical},
            {"active",hit!=0 || evade!=0 || critical!=0}});
    };
    if(flags&0x18) {const int b=rules::level_bonus(ram,c.pilot);add((flags&0x10)?"enhanced":"newtype",b,b);}
    if(flags&4) {
        const int b=rules::potential_bonus(ram,c.pilot,c.unit,rules::enabled(rules::potential_bands));
        const int he=rules::enabled(rules::potential_half)?b/2:b;
        add("potential",he,he,c.side==0?b:0);
        auto& row=out.back();row["base"]=b;row["active"]=b>0;
        row["critical_suppressed"]=c.side==0 && bool(read(ram,c.pilot+0x1C,4)&0x20800);
        // Find the first nonzero column in the actual ROM table, rather than
        // assuming that every level starts at the same HP percentage.
        int threshold=0;bool full=false;
        for(unsigned band=0;band<10;++band)if(read(ram,rules::potential_table+level*10+band,1)) {
            full=!rules::enabled(rules::potential_bands) && band<=1;
            threshold=(10-int(band)+(rules::enabled(rules::potential_bands)?0:1))*10;break;
        }
        row["threshold"]=threshold;row["full_hp"]=full;
    }
    // The original empty functions retain the caller's masked flag in v0.
    // Holy warriors have NO hit bonus: only the defender's call exists.
    if(flags&0x20)add("holy",0,rules::enabled(rules::seisenshi_level)?rules::level_bonus(ram,c.pilot):32);
    if(flags&0x40){const int b=rules::enabled(rules::esp_level)?rules::level_bonus(ram,c.pilot):64;add("esp",b,b);}
    return out;
}

inline nlohmann::json defensive_effects(const uint8_t* ram,const rule_probe::Combatant& c,const nlohmann::json& defense,const nlohmann::json& incoming) {
    using nlohmann::json;
    json out=json::array();
    const auto pilot=read(ram,c.unit+0x38,4),flags=read(ram,pilot+0x36,1),abilities=read(ram,c.unit+0x28,4);
    for(const auto& [id,bit,offset]:{std::tuple{"parry",1u,7u},std::tuple{"shield",2u,8u}})
        if(flags&bit)out.push_back({{"id",id},{"level",read(ram,pilot+offset,1)},
            {"chance",defense.at(id)},{"reason",(read(ram,c.unit+0x20,1)&bit)?defense.at(std::string(id)+"_reason"):"no_equipment"}});
    for(const auto& [bit,id]:{std::pair{0x10u,"clone"},{0x1000u,"mach"},{0x2000u,"true_mach"},{0x1u,"god_shadow"},
        {0x20000u,"getter_vision"},{0x40000u,"shungeki"},{0x100000u,"jammer"}})
        if(abilities&bit)out.push_back({{"id",id},{"chance",defense.at("clone")},{"reason",defense.at("clone_reason")}});
    const auto& b=incoming.at("barrier");
    for(const auto& [bit,id,strength]:{std::tuple{0x4000u,"aura_barrier",3000},std::tuple{0x20u,"i_field",2000},
        std::tuple{0x200u,"beam_coat",1000},std::tuple{0x800u,"planet",2000}})
        if(abilities&bit)out.push_back({{"id",id},{"strength",strength+(bit==0x4000 && (flags&0x20)?int(read(ram,0x80218930+2*read(ram,pilot+6,1),2)):0)},
            {"reason",incoming.at("weapon").get<int>()<0?"no_attack":(bit!=0x4000 && (abilities&0x4000))?"aura_first":b.value("status","none")}});
    const auto slot=c.handle-(c.side==0?0x42:c.side==1?0x60:0x7E);
    if(c.side!=0 && (read(ram,0x8015E101+c.side*0x258+slot*0x14,1)&0x80))
        out.push_back({{"id","dummy"},{"remaining",read(ram,pilot+0x14,2)}});
    if(read(ram,c.unit+2,2)==149)out.push_back({{"id","fixed_damage"},{"damage",10}});
    return out;
}
}
