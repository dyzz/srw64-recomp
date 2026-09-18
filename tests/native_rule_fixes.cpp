#include "rule_fixes.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <vector>
uint64_t srw64_current_vi(){return 0;}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    using namespace srw64::rules;
    // Rule ids: catalog order in names(), unknown and empty ids rejected.
    assert(parse("")==0);
    assert(parse("limit-cap,esp-level")==(limit_cap|esp_level));
    assert(names(parse("limit-cap,seisenshi-level,esp-level"))=="esp-level,seisenshi-level,limit-cap");
    assert(parse("potential-half,potential-bands")==(potential_bands|potential_half));
    assert(parse("boss-dummy-none,boss-dummy-half")==(boss_dummy_half|boss_dummy_none));
    assert(parse(names(all_fixes))==all_fixes);
    // Boss dummies: halving keeps one, "none" wins, and the original count passes through.
    assert(scaled_dummies(3,false,false)==3 && scaled_dummies(0,true,false)==0);
    assert(scaled_dummies(2,true,false)==1 && scaled_dummies(3,true,false)==1);
    assert(scaled_dummies(5,true,false)==2 && scaled_dummies(7,true,false)==3);
    assert(scaled_dummies(1,true,false)==1 && scaled_dummies(7,false,true)==0 && scaled_dummies(7,true,true)==0);
    assert(record_faction(3)==0 && record_faction(4)==2 && record_faction(1)==1 && record_faction(0)==0);
    for(const char* bad:{"esp","esp-level,","esp-level,,limit-cap",",esp-level","ESP-LEVEL"}) {
        bool threw=false;
        try{parse(bad);}catch(const std::runtime_error&){threw=true;}
        assert(threw);
    }
    // Launch choice, report, and the probe's scoped override.
    setenv("SRW64_RULE_FIXES","limit-cap,esp-level",1);
    configure(dir);
    std::ifstream report(dir/"rule-fixes.json");
    const auto applied=nlohmann::json::parse(report);
    assert(applied["schema"]=="srw64.rule-fixes-applied.v1" && applied["rules_version"]==version);
    assert(applied["enabled"]==nlohmann::json::array({"esp-level","limit-cap"}));
    assert(enabled(esp_level) && enabled(limit_cap) && !enabled(seisenshi_level));
    {
        Override original(0);
        assert(!enabled(esp_level) && !enabled(limit_cap));
        {Override all(esp_level|seisenshi_level|limit_cap);assert(enabled(seisenshi_level));}
        assert(!enabled(seisenshi_level));
    }
    assert(enabled(esp_level) && !enabled(seisenshi_level));

    // Runtime switching: the current set is what enabled() reads, and a change is
    // written back to the launcher's settings file.
    const auto settings=dir/"rules.json";
    setenv("SRW64_RULE_SETTINGS",settings.c_str(),1);
    set_fixes(seisenshi_level|potential_bands);
    assert(enabled(seisenshi_level) && enabled(potential_bands) && !enabled(limit_cap));
    {
        std::ifstream saved(settings);
        const auto document=nlohmann::json::parse(saved);
        assert(document["schema"]=="srw64.rule-settings.v1" && document["rules_version"]==version);
        assert(document["fixes"]==nlohmann::json::array({"seisenshi-level","potential-bands"}));
    }
    {
        Override scope(0);
        assert(!enabled(seisenshi_level));   // The probe's override still wins.
    }
    set_fixes(0);
    assert(!enabled(seisenshi_level) && nlohmann::json::parse(std::ifstream(settings))["fixes"].empty());
    bool threw=false;
    try{set_fixes(all_fixes|0x80000000u);}catch(const std::runtime_error&){threw=true;}
    assert(threw);
    set_fixes(launch_fixes());

    std::vector<uint8_t> ram(0x800000);
    {
        // DummyScale rewrites the record for one call and restores it afterwards.
        auto record_put=[&](uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));};
        const uint32_t record=0x80199400;
        record_put(record+record_side,1,2);record_put(record+record_behaviour,record_dummy_bit,2);record_put(record+record_dummies,7,2);
        {DummyScale scope(ram.data(),record);assert(read(ram.data(),record+record_dummies,2)==7);}   // no rule on yet
        {
            Override half(boss_dummy_half);
            DummyScale scope(ram.data(),record);
            assert(read(ram.data(),record+record_dummies,2)==3);
        }
        assert(read(ram.data(),record+record_dummies,2)==7);
        {Override none(boss_dummy_none);DummyScale scope(ram.data(),record);assert(read(ram.data(),record+record_dummies,2)==0);}
        assert(read(ram.data(),record+record_dummies,2)==7);
        // The player's own records and records without the dummy bit are left alone.
        record_put(record+record_side,3,2);
        {Override none(boss_dummy_none);DummyScale scope(ram.data(),record);assert(read(ram.data(),record+record_dummies,2)==7);}
        record_put(record+record_side,1,2);record_put(record+record_behaviour,0,2);
        {Override none(boss_dummy_none);DummyScale scope(ram.data(),record);assert(read(ram.data(),record+record_dummies,2)==7);}
    }
    auto put=[&](uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));};
    auto get=[&](uint32_t p,unsigned n){return read(ram.data(),p,n);};
    // Level bonus reads the skill table row by pilot +0x06; out-of-range levels give 0.
    const uint8_t curve[]={0,10,14,18,21,24,26,28,29,30};
    for(unsigned i=0;i<10;++i)put(skill_bonus_table+i,curve[i],1);
    const uint32_t pilot=0x80172F40, unit=0x8016A210;
    for(unsigned level=0;level<10;++level){put(pilot+6,level,1);assert(level_bonus(ram.data(),pilot)==curve[level]);}
    put(pilot+6,10,1);assert(level_bonus(ram.data(),pilot)==0);
    assert(level_bonus(ram.data(),0)==0);

    // Limit cap: lowered for the scope only when stat + mobility exceeds the limit.
    auto set=[&](uint32_t stat,uint32_t mobility,uint32_t limit){put(pilot+pilot_hit,stat,2);put(unit+unit_mobility,mobility,2);put(unit+unit_limit,limit,2);};
    set(150,100,200);
    {
        StatCap cap(ram.data(),pilot,unit,pilot_hit);
        assert(get(pilot+pilot_hit,2)==150 && get(unit+unit_mobility,2)==50);
    }
    assert(get(pilot+pilot_hit,2)==150 && get(unit+unit_mobility,2)==100 && get(unit+unit_limit,2)==200);
    set(250,100,200);
    {
        StatCap cap(ram.data(),pilot,unit,pilot_hit);
        assert(get(pilot+pilot_hit,2)==200 && get(unit+unit_mobility,2)==0);
    }
    assert(get(pilot+pilot_hit,2)==250 && get(unit+unit_mobility,2)==100);
    set(100,100,200);
    {StatCap cap(ram.data(),pilot,unit,pilot_hit);assert(get(pilot+pilot_hit,2)==100 && get(unit+unit_mobility,2)==100);}
    set(0xFFFF,0xFFFF,0xFFFF);
    {StatCap cap(ram.data(),pilot,unit,pilot_hit);assert(get(pilot+pilot_hit,2)==0xFFFF && get(unit+unit_mobility,2)==0);}
    assert(get(unit+unit_mobility,2)==0xFFFF);
    // Invalid pointers leave memory alone.
    set(250,100,200);
    {StatCap cap(ram.data(),0,unit,pilot_hit);assert(get(unit+unit_mobility,2)==100);}
    {StatCap cap(ram.data(),pilot,0x00001000,pilot_hit);assert(get(pilot+pilot_hit,2)==250);}
    // Nested caps on one unit (attacker and defender scopes) restore in order.
    set(150,100,200);
    put(pilot+pilot_evade,180,2);
    {
        StatCap hit(ram.data(),pilot,unit,pilot_hit);
        assert(get(unit+unit_mobility,2)==50);
        StatCap evade(ram.data(),pilot,unit,pilot_evade);
        assert(get(pilot+pilot_evade,2)==180 && get(unit+unit_mobility,2)==20);
    }
    assert(get(pilot+pilot_hit,2)==150 && get(pilot+pilot_evade,2)==180 && get(unit+unit_mobility,2)==100);

    // 底力 bands: original 1..9 with the top band below 20%; corrected 0..9 with it below 10%.
    assert(potential_band(100,false)==1 && potential_band(90,false)==1 && potential_band(89.9f,false)==2);
    assert(potential_band(20,false)==8 && potential_band(19.9f,false)==9 && potential_band(0,false)==9);
    assert(potential_band(100,true)==0 && potential_band(90,true)==0 && potential_band(89.9f,true)==1);
    assert(potential_band(20,true)==7 && potential_band(10,true)==8 && potential_band(9.9f,true)==9);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(potential_band(nan,false)==9 && potential_band(nan,true)==9);
    for(uint32_t level=0;level<10;++level)for(uint32_t band=0;band<10;++band)
        put(potential_table+level*10+band,band+level>=10?(band+level-9)*10:0,1);
    put(pilot+6,9,1);
    put(unit+4,1000,2);put(unit+6,1000,2);
    assert(potential_bonus(ram.data(),pilot,unit,false)==0);   // flag 0x04 absent
    put(pilot+0x36,0x04,1);
    assert(potential_bonus(ram.data(),pilot,unit,false)==10 && potential_bonus(ram.data(),pilot,unit,true)==0);
    put(unit+4,150,2);
    assert(potential_bonus(ram.data(),pilot,unit,false)==90 && potential_bonus(ram.data(),pilot,unit,true)==80);
    put(unit+4,99,2);
    assert(potential_bonus(ram.data(),pilot,unit,false)==90 && potential_bonus(ram.data(),pilot,unit,true)==90);
    put(pilot+6,4,1);put(unit+4,450,2);
    assert(potential_bonus(ram.data(),pilot,unit,false)==10 && potential_bonus(ram.data(),pilot,unit,true)==0);
    put(unit+6,0,2);   // 450/0 = +inf: the top of the scale
    assert(potential_bonus(ram.data(),pilot,unit,false)==0 && potential_bonus(ram.data(),pilot,unit,true)==0);
    put(unit+4,0,2);   // 0/0 = NaN: every comparison fails, bottom band
    assert(potential_bonus(ram.data(),pilot,unit,false)==40 && potential_bonus(ram.data(),pilot,unit,true)==40);

    // Weapon inheritance gaps: only the three missing pairs are written, only with
    // the rule on, and only when both machines carry the weapon.
    {
        const uint32_t machine=0x8016A210+unit_size, weapons=0x80178F80, snapshot=0x80190000;
        auto weapon_at=[&](uint32_t index){return weapons+index*weapon_size;};
        auto build=[&] {
            put(machine+unit_weapon_count,4,1);
            put(machine+unit_weapon_list,weapons,4);
            const uint16_t numbers[]={1057,1058,1059,1060};
            for(uint32_t i=0;i<4;++i){put(weapon_at(i)+weapon_number,numbers[i],2);put(weapon_at(i)+weapon_level,0,1);}
            // The predecessor's weapons as 800AA868 saved them.
            const uint16_t saved_numbers[]={1070,1071,1072,1073};
            for(uint32_t i=0;i<4;++i){put(snapshot+i*saved_size+saved_number,saved_numbers[i],2);
                                      put(snapshot+i*saved_size+saved_level,i+2,1);}
        };
        build();
        assert(saved_weapon_level(ram.data(),snapshot,4,1070)==2);
        assert(saved_weapon_level(ram.data(),snapshot,4,1072)==4);
        assert(saved_weapon_level(ram.data(),snapshot,4,999)==-1);
        inherit_missing_weapons(ram.data(),machine,snapshot,4);   // rule off
        for(uint32_t i=0;i<4;++i)assert(get(weapon_at(i)+weapon_level,1)==0);
        {
            Override on(weapon_inherit_map);
            inherit_missing_weapons(ram.data(),machine,snapshot,4);
            assert(get(weapon_at(0)+weapon_level,1)==2);   // 1070 → 1057
            assert(get(weapon_at(2)+weapon_level,1)==4);   // 1072 → 1059
            assert(get(weapon_at(1)+weapon_level,1)==0 && get(weapon_at(3)+weapon_level,1)==0);
            // 470 → 1291 is not in this machine's list, so nothing is written.
            assert(!write_weapon_level(ram.data(),machine,1291,5));
            // A count of 0, an invalid machine or an invalid snapshot write nothing.
            build();
            inherit_missing_weapons(ram.data(),machine,snapshot,0);
            inherit_missing_weapons(ram.data(),0,snapshot,4);
            inherit_missing_weapons(ram.data(),machine,0,4);
            for(uint32_t i=0;i<4;++i)assert(get(weapon_at(i)+weapon_level,1)==0);
            // The predecessor's level wins, as it does for the pairs the ROM lists.
            put(weapon_at(0)+weapon_level,7,1);
            inherit_missing_weapons(ram.data(),machine,snapshot,4);
            assert(get(weapon_at(0)+weapon_level,1)==2);
        }
    }

    // 聖戦士 Hyper Aura power: flag 0x20, weapon condition 15, table row times 10.
    const uint8_t attack[]={0,20,40,60,80,100,120,130,140,150};
    for(unsigned i=0;i<10;++i)put(aura_attack_table+i,attack[i],1);
    const uint32_t weapon=0x8016B000;
    put(pilot+0x36,0x20,1);put(pilot+6,9,1);
    put(weapon+weapon_condition,hyper_aura_condition,1);put(weapon+weapon_power,2400,2);
    assert(aura_slash_bonus(ram.data(),pilot,weapon)==1500);
    put(pilot+6,3,1);assert(aura_slash_bonus(ram.data(),pilot,weapon)==600);
    put(weapon+weapon_condition,14,1);assert(aura_slash_bonus(ram.data(),pilot,weapon)==0);   // オーラ斬り
    put(weapon+weapon_condition,hyper_aura_condition,1);
    put(pilot+0x36,0x40,1);assert(aura_slash_bonus(ram.data(),pilot,weapon)==0);               // 超能力 only
    put(pilot+0x36,0x20,1);
    assert(aura_slash_bonus(ram.data(),0,weapon)==0 && aura_slash_bonus(ram.data(),pilot,0)==0);
    {
        WeaponPower power(ram.data(),weapon,600);
        assert(get(weapon+weapon_power,2)==3000);
    }
    assert(get(weapon+weapon_power,2)==2400);
    put(weapon+weapon_power,0xFF00,2);
    {WeaponPower power(ram.data(),weapon,1500);assert(get(weapon+weapon_power,2)==0xFFFF);}
    assert(get(weapon+weapon_power,2)==0xFF00);
    {WeaponPower power(ram.data(),weapon,0);assert(get(weapon+weapon_power,2)==0xFF00);}

    // Participant table: slot stride 0x5C, unit at +0x04 and pilot at +0x0C.
    put(participants+participant_size+0x04,unit,4);
    put(participants+participant_size+0x0C,pilot,4);
    const auto combatant=participant(ram.data(),0x101);
    assert(combatant.unit==unit && combatant.pilot==pilot);
    return 0;
}
