#pragma once
#include "battle_effects.hpp"
#include "state_probe.hpp"

namespace srw64::battle_ui_probe {
inline void poll(uint8_t* ram,recomp_context* ctx,uint32_t owner) {
    static bool done=false;
    const char* enabled=std::getenv("SRW64_BATTLE_UI_PROBE");
    if(done || !enabled || std::string_view(enabled)!="1" || rules::directory().empty())return;
    if(guest::read(ram,guest::read(ram,owner+0x1C,4),2)!=0x3D38 || !rule_probe::tactical_overlay(ram))return;
    const auto players=rule_probe::roster(ram,0),enemies=rule_probe::roster(ram,1);
    if(players.empty() || enemies.empty())return;
    done=true;
    std::vector<uint8_t> original(ram,ram+0x800000), scratch=original;
    const recomp_context saved=*ctx;
    const auto gameplay_before=state_probe::regions(ram);
    nlohmann::json rows=nlohmann::json::array();
    for(unsigned fixes:{0u,rules::all_fixes})
    for(const auto& pair:{std::pair{players.front(),enemies.front()},std::pair{enemies.front(),players.front()}})
    for(unsigned hp:{100u,5u})for(int modifier:{-20,0,20,30}) {
        rules::Override scope(fixes);
        scratch=original;auto* copy=scratch.data();auto call=saved;
        const auto& [a,d]=pair;
        // Controlled one-variable experiments; all writes are to the isolated
        // guest image. A 21-point skill difference covers fractional enemy rates.
        guest::write16(copy,a.pilot+0x2C,121);guest::write16(copy,d.pilot+0x2C,100);
        guest::write32(copy,a.pilot+0x1C,0);guest::write32(copy,d.pilot+0x1C,0);
        guest::write8(copy,a.pilot+0x36,4);guest::write8(copy,a.pilot+6,4);
        guest::write16(copy,a.unit+4,std::max(1u,guest::read(copy,a.unit+6,2)*hp/100));
        guest::write8(copy,a.weapon+0xA,uint8_t(modifier));guest::write8(copy,a.weapon+0x14,uint8_t(modifier));
        const int estimated=rule_probe::estimate(copy,&call,a,d);
        const int actual=rule_probe::battle(copy,&call,a,d);
        const int predicted=combat_preview::critical_percent(copy,a,d);
        (void)combat_preview::estimate(copy,&call,a,d);
        unsigned successes=0;
        for(int roll=0;roll<100;++roll) {
            call=saved;call.r29=rule_probe::pointer(uint32_t(saved.r29)-0x200);
            call.r4=a.handle;call.r5=rule_probe::pointer(a.pilot);call.r6=rule_probe::pointer(a.unit);call.r7=rule_probe::pointer(a.weapon);
            guest::write32(copy,uint32_t(call.r29)+0x10,d.pilot);
            combat_preview::test_roll=roll;LOOKUP_FUNC(0x801F47B0)(copy,&call);combat_preview::test_roll=-1;
            successes+=call.r2!=0;
        }
        const bool scratch_rng_unchanged=std::memcmp(copy+0xD49D0,original.data()+0xD49D0,0x834)==0;
        rows.push_back({{"fixes",fixes},{"side",a.side},{"hp_percent",hp},{"modifier",modifier},{"estimated_hit",estimated},{"battle_hit",actual},
            {"predicted_critical",predicted},{"original_critical_successes",successes},{"rolls",100},
            {"scratch_rng_unchanged",scratch_rng_unchanged},{"match",scratch_rng_unchanged && predicted==int(successes) && std::clamp(estimated,0,100)==std::clamp(actual,0,100)}});
    }
    nlohmann::json skills=nlohmann::json::array();
    for(unsigned fixes:{0u,unsigned(rules::potential_bands),unsigned(rules::potential_half),rules::all_fixes})
    for(unsigned flag:{4u,8u,16u,32u,64u})for(unsigned level:{0u,1u,4u,9u})
    for(unsigned hp:{100u,90u,89u,50u,49u,40u,39u,10u,9u,1u})for(unsigned side:{0u,1u,2u}) {
        rules::Override scope(fixes);scratch=original;auto* copy=scratch.data();auto c=players.front();c.side=side;
        guest::write8(copy,c.pilot+0x36,flag);guest::write8(copy,c.pilot+6,level);
        guest::write32(copy,c.pilot+0x1C,0);guest::write16(copy,c.unit+6,10000);guest::write16(copy,c.unit+4,hp*100);
        const auto rows=combat_preview::pilot_effects(copy,c);const auto& row=rows.at(0);
        const auto oracle=[&](unsigned kind) {
            auto call=saved;call.r29=rule_probe::pointer(uint32_t(saved.r29)-0x200);
            call.r2=flag;call.r4=kind;call.r5=rule_probe::pointer(c.pilot);call.r6=rule_probe::pointer(c.unit);
            LOOKUP_FUNC(flag==4?0x801E1D64:flag<32?0x801E1EDC:flag==32?0x801E1F08:0x801E1F10)(copy,&call);
            return int(call.r2);
        };
        const int h=flag==32?0:oracle(flag==4?2:0),e=oracle(1),critical=flag==4 && side==0?oracle(0):0;
        skills.push_back({{"fixes",fixes},{"flag",flag},{"level",level},{"hp_percent",hp},{"side",side},
            {"effect",row},{"match",row.at("hit")==h && row.at("evade")==e && row.at("critical")==critical}});
    }
    nlohmann::json defenses=nlohmann::json::array(),barriers=nlohmann::json::array();
    for(unsigned side:{0u,1u,2u})for(unsigned level:{0u,1u,4u,9u})for(bool cut:{false,true}) {
        scratch=original;auto* copy=scratch.data();auto call=saved;
        auto a=players.front(),d=enemies.front();d.side=side;d.handle=(side==0?0x42:side==1?0x60:0x7E);
        guest::write8(copy,d.unit+0x20,3);guest::write8(copy,d.pilot+0x36,3);
        guest::write8(copy,d.pilot+7,level);guest::write8(copy,d.pilot+8,level);guest::write8(copy,a.weapon+4,cut?8:0);
        int parry=0,shield=0,clone=0;
        guest::write32(copy,d.unit+0x28,0x10);guest::write16(copy,d.pilot+0x20,130);
        for(int roll=0;roll<100;++roll) {
            combat_preview::test_roll=roll;
            parry+=combat_preview::original_call(copy,&call,0x801F6D10,a.weapon,d.unit,d.handle)>=0;
            shield+=combat_preview::original_call(copy,&call,0x801F6FDC,d.unit,d.handle)>=0;
            clone+=combat_preview::original_call(copy,&call,0x801F6C44,d.unit)>=0;
        }
        combat_preview::test_roll=-1;
        const int predicted=combat_preview::defense_percent(level,side);
        defenses.push_back({{"side",side},{"level",level},{"cuttable",cut},{"parry",parry},{"shield",shield},{"clone",clone},
            {"match",parry==(cut?predicted:0) && shield==predicted && clone==50}});
    }
    // Use the complete original special-defense resolver as the independent
    // oracle, with no clone/parry/shield skill and a guaranteed hit. Include all
    // four barrier types, combined flags, both weapon classes, EN boundaries,
    // defend, and the values just below/at/above their threshold.
    for(unsigned flags:{0u,0x20u,0x200u,0x800u,0x4000u,0xA20u,0x4820u})
    for(bool beam:{false,true})for(int command:{0,2})for(int budget:{4,5})
    for(int damage:{999,1000,1001,1999,2000,2001,3199,3200,3201,5001,6401}) {
        scratch=original;auto* copy=scratch.data();auto call=saved;
        const auto a=players.front(),d=enemies.front();
        for(const auto& [slot,c]:{std::pair{0u,&a},std::pair{1u,&d}}) {
            const auto row=rules::participants+slot*rules::participant_size;
            for(unsigned i=0;i<rules::participant_size;++i)guest::write8(copy,row+i,0);
            guest::write16(copy,row,c->handle);guest::write32(copy,row+4,c->unit);
            guest::write32(copy,row+8,c->weapon);guest::write32(copy,row+12,c->pilot);
        }
        guest::write8(copy,d.pilot+0x36,0x20);guest::write8(copy,d.pilot+6,1);
        guest::write32(copy,d.unit+0x28,flags);guest::write8(copy,d.unit+0x20,0);
        guest::write16(copy,d.unit+8,budget+3);guest::write8(copy,d.weapon+0xD,3);
        guest::write8(copy,a.weapon+4,beam?2:0);
        guest::write8(copy,0x8015E101+d.side*0x258+d.slot*0x14,0); // no dummy
        guest::write16(copy,rules::participants+0x12,100);guest::write16(copy,rules::participants+0x14,damage);
        guest::write8(copy,rules::participants+rules::participant_size+0x10,command);
        const auto predicted=combat_preview::barrier_for(copy,a,d,damage,command);
        combat_preview::test_roll=49;combat_preview::original_call(copy,&call,0x801F7204,1,0);combat_preview::test_roll=-1;
        const int actual=guest::read(copy,rules::participants+0x14,2);
        const int en=guest::read(copy,rules::participants+rules::participant_size+0x17,1);
        const int reaction=int8_t(guest::read(copy,rules::participants+rules::participant_size+0x26,1));
        barriers.push_back({{"abilities",flags},{"beam",beam},{"command",command},{"en_budget",budget},{"raw",damage},
            {"predicted",predicted.damage},{"actual",actual},{"en_cost",en},{"reaction",reaction},
            {"match",actual==predicted.damage && en==predicted.en_cost && (reaction>=0)==predicted.blocks_shield}});
    }
    // RSP/graphics work can complete concurrently while the game thread is here.
    // Keep full-RAM differences for diagnosis, but assert the known gameplay/RNG
    // regions, combat table and CPU context rather than live presentation buffers.
    nlohmann::json changes=nlohmann::json::array();
    for(size_t i=0;i<original.size();++i)if(original[i]!=ram[i]) {
        const size_t begin=i;while(i+1<original.size() && original[i+1]!=ram[i+1])++i;
        changes.push_back({{"offset",begin},{"bytes",i-begin+1}});
    }
    const bool context_unchanged=std::memcmp(&saved,ctx,sizeof(saved))==0;
    const bool unchanged=changes.empty() && context_unchanged;
    const bool gameplay_unchanged=gameplay_before==state_probe::regions(ram) &&
        std::memcmp(original.data()+(rules::participants&0x1FFFFFFF),ram+(rules::participants&0x1FFFFFFF),2*rules::participant_size)==0;
    bool passed=gameplay_unchanged && context_unchanged;for(const auto& row:rows)passed=passed && row.at("match").get<bool>();
    for(const auto& row:skills)passed=passed && row.at("match").get<bool>();
    for(const auto& row:defenses)passed=passed && row.at("match").get<bool>();
    for(const auto& row:barriers)passed=passed && row.at("match").get<bool>();
    std::ofstream out(rules::directory()/"battle-ui-probe.json");
    out<<nlohmann::json({{"schema","srw64.battle-ui-probe.v1"},{"passed",passed},{"live_ram_and_context_unchanged",unchanged},
        {"gameplay_and_rng_unchanged",gameplay_unchanged},{"context_unchanged",context_unchanged},{"changed_ranges",changes},{"vi",srw64_current_vi()},{"rows",rows},{"skills",skills},{"defenses",defenses},{"barriers",barriers}}).dump(2)<<'\n';
    std::fprintf(stderr,"SRW64_BATTLE_UI_PROBE passed=%d rows=%zu\n",passed,rows.size());
}
}
