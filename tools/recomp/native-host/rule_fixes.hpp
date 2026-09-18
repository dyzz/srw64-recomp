#pragma once
#include "recomp.h"
#include "guest_memory.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

uint64_t srw64_current_vi();

// Optional rules (docs/rule-fixes.md): corrections for original defects and
// difficulty options. SRW64_RULE_FIXES gives the set a run starts with, and the
// 选项 menu or the settings window can change it at any time; a rule that is off
// leaves its hook wrapper in game_hooks.cpp calling the original routine unchanged.
namespace srw64::rules {
using guest::read;
using guest::valid;
using guest::write8;
using guest::write16;

enum Fix : unsigned {esp_level=1,seisenshi_level=2,limit_cap=4,potential_bands=8,potential_half=16,
                     boss_dummy_half=32,boss_dummy_none=64,weapon_inherit_map=128,upgrade_cap_break=256,
                     aura_slash_power=512};
// Each entry says which kind it is, so the UI grouping and the default set come
// from the catalog itself instead of a second list that can drift: corrections
// repair places where the original contradicts its own data or UI and are on by
// default, difficulty options change the original balance on purpose and start off.
enum class Kind {correction,difficulty};
struct Entry {Fix fix;std::string_view id;Kind kind;};
inline constexpr Entry catalog[]={
    {esp_level,"esp-level",Kind::correction},
    {seisenshi_level,"seisenshi-level",Kind::correction},
    {limit_cap,"limit-cap",Kind::correction},
    {potential_bands,"potential-bands",Kind::correction},
    {potential_half,"potential-half",Kind::correction},
    {weapon_inherit_map,"weapon-inherit-map",Kind::correction},
    {aura_slash_power,"aura-slash-power",Kind::correction},
    {boss_dummy_half,"boss-dummy-half",Kind::difficulty},
    {boss_dummy_none,"boss-dummy-none",Kind::difficulty},
    {upgrade_cap_break,"upgrade-cap-break",Kind::difficulty}};
consteval unsigned mask_of(Kind kind) {
    unsigned mask=0;
    for(const auto& entry:catalog)if(entry.kind==kind)mask|=entry.fix;
    return mask;
}
inline constexpr unsigned corrections=mask_of(Kind::correction);
inline constexpr unsigned difficulty=mask_of(Kind::difficulty);
inline constexpr unsigned all_fixes=corrections|difficulty;
inline constexpr unsigned default_fixes=corrections;
inline constexpr unsigned version=1;
// The presets offered by the menu and the settings window, by ui key.
struct Preset {std::string_view key;unsigned fixes;};
inline constexpr Preset presets[]={{"rules_defaults",default_fixes},{"rules_original",0},{"rules_all",all_fixes}};
// "esp-level" -> ui key "rule_esp_level", as profile.py derives the required keys.
inline std::string ui_key(std::string_view id) {
    std::string key="rule_";
    for(char c:id)key+=c=='-'?'_':c;
    return key;
}

// Comma-separated rule ids; the empty string selects the original rules.
inline unsigned parse(std::string_view text) {
    unsigned fixes=0;
    if(text.empty())return fixes;
    for(size_t start=0;;) {
        const auto comma=text.find(',',start);
        const auto id=text.substr(start,comma==std::string_view::npos?std::string_view::npos:comma-start);
        const auto* entry=std::find_if(std::begin(catalog),std::end(catalog),[&](const Entry& e){return e.id==id;});
        if(entry==std::end(catalog))throw std::runtime_error("SRW64_RULE_FIXES: unknown rule '"+std::string(id)+"'");
        fixes|=entry->fix;
        if(comma==std::string_view::npos)return fixes;
        start=comma+1;
    }
}
inline std::string names(unsigned fixes) {
    std::string result;
    for(const auto& e:catalog)if(fixes&e.fix){if(!result.empty())result+=',';result+=e.id;}
    return result;
}
inline unsigned launch_fixes() {
    static const unsigned value=[]{const char* p=std::getenv("SRW64_RULE_FIXES");return p?parse(p):0u;}();
    return value;
}
// The current choice. The menu (window thread) writes it and guest code reads it
// on every calculation, so a switch takes effect from the next one onwards.
inline std::atomic<unsigned>& current_fixes(){static std::atomic<unsigned> value{~0u};return value;}
inline unsigned active_fixes() {
    const unsigned value=current_fixes().load(std::memory_order_relaxed);
    return value==~0u?launch_fixes():value;
}
// The in-game probe evaluates several rule sets in one run; everything else uses
// the current choice. Guest code runs on one thread, so a plain override suffices.
inline constexpr unsigned no_override=~0u;
inline unsigned& override_fixes(){static unsigned value=no_override;return value;}
inline bool enabled(Fix fix) {
    const unsigned active=override_fixes()==no_override?active_fixes():override_fixes();
    return (active&fix)!=0;
}
class Override {
public:
    explicit Override(unsigned fixes):previous(override_fixes()){override_fixes()=fixes;}
    ~Override(){override_fixes()=previous;}
    Override(const Override&)=delete;
    Override& operator=(const Override&)=delete;
private:
    unsigned previous;
};

inline std::filesystem::path& directory(){static std::filesystem::path value;return value;}
inline nlohmann::json enabled_ids(unsigned fixes) {
    nlohmann::json ids=nlohmann::json::array();
    for(const auto& e:catalog)if(fixes&e.fix)ids.push_back(e.id);
    return ids;
}
// Host start-up: an unknown id fails here rather than on the first battle.
inline void configure(const std::filesystem::path& output) {
    const unsigned fixes=launch_fixes();
    directory()=output;
    current_fixes().store(fixes,std::memory_order_relaxed);
    std::ofstream(output/"rule-fixes.json")<<nlohmann::json({{"schema","srw64.rule-fixes-applied.v1"},
        {"rules_version",version},{"enabled",enabled_ids(fixes)}}).dump(2)<<'\n';
    std::fprintf(stderr,"SRW64_RULE_FIXES %s\n",fixes?names(fixes).c_str():"none");
}

// Written atomically so a crash mid-write cannot leave a half file the launcher
// would reject. SRW64_RULE_SETTINGS names the launcher's own rules.json; without
// it (bounded probe runs) a change stays in this run only.
inline void persist(unsigned fixes) {
    const char* path=std::getenv("SRW64_RULE_SETTINGS");
    if(!path || !*path)return;
    const std::filesystem::path destination(path), temporary=destination.string()+".tmp";
    {
        std::ofstream out(temporary);
        out<<nlohmann::json({{"schema","srw64.rule-settings.v1"},{"rules_version",version},
            {"fixes",enabled_ids(fixes)}}).dump(2)<<'\n';
        out.flush();
        if(!out)throw std::runtime_error("Cannot write rule settings");
    }
    std::filesystem::rename(temporary,destination);
}
// Menu thread. Guest code picks the new set up at its next calculation; a battle
// already being resolved keeps the hit rate it was given.
inline void set_fixes(unsigned fixes) {
    if(fixes&~all_fixes)throw std::runtime_error("unknown rule bit");
    if(current_fixes().exchange(fixes,std::memory_order_relaxed)==fixes)return;
    persist(fixes);
    if(!directory().empty())
        std::ofstream(directory()/"rule-fixes-events.jsonl",std::ios::app)
            <<nlohmann::json({{"schema","srw64.rule-fixes-change.v1"},{"vi",srw64_current_vi()},
                {"rules_version",version},{"enabled",enabled_ids(fixes)}}).dump()<<'\n';
    std::fprintf(stderr,"SRW64_RULE_FIXES %s\n",fixes?names(fixes).c_str():"none");
}

// Hit/evade bonus by special-skill level (pilot +0x06, shared by NT, 強化人間,
// 聖戦士 and 超能力). This is the ROM's NT table read by 801E1EDC. Its hit and evade
// rows are identical, as are the two rows of the unreferenced table at 802180B4,
// so the row argument the callers pass does not matter.
inline constexpr uint32_t skill_bonus_table=0x80218080;
inline uint32_t level_bonus(const uint8_t* ram,uint32_t pilot) {
    const uint32_t level=read(ram,pilot+0x06,1);
    return level<10?read(ram,skill_bonus_table+level,1):0;
}

// 底力 (pilot flag 0x04), read by 801E1D64: table 80217F90[level*10 + band], the
// band taken from current/maximum HP (unit +0x04/+0x06). The original gives band 1
// from 90% HP up and band 9 below 20%, so column 0 is never read, a full-HP unit
// already counts as damaged, and the top value starts one band early. The
// corrected bands run from 0 (90% and up) to 9 (below 10%).
inline constexpr uint32_t potential_table=0x80217F90;
inline uint32_t potential_band(float ratio,bool corrected) {
    static constexpr float edges[]={90,80,70,60,50,40,30,20,10};
    const uint32_t first=corrected?0:1, count=corrected?9:8;
    for(uint32_t i=0;i<count;++i)if(edges[i]<=ratio)return first+i;
    return 9;
}
inline uint32_t potential_bonus(const uint8_t* ram,uint32_t pilot,uint32_t unit,bool corrected) {
    if(!(read(ram,pilot+0x36,1)&0x04))return 0;
    // Same single-precision steps as 801E1D64: (hp / max) * 100.
    const float hp=float(int32_t(read(ram,unit+0x04,2))), max=float(int32_t(read(ram,unit+0x06,2)));
    const float fraction=hp/max;
    const float ratio=fraction*100.0f;
    return read(ram,potential_table+read(ram,pilot+0x06,1)*10+potential_band(ratio,corrected),1);
}

inline constexpr uint32_t pilot_evade=0x26,pilot_hit=0x28,unit_mobility=0x10,unit_limit=0x14;
// The pilot status page (801E7174/801E71C8) draws "stat+mobility" in the warning
// colour once the sum exceeds the unit's limit, but 801F4384 and 80204254 add both
// summands unclamped. For one original call, lower them so they add up to the
// limit; reaction stays outside the cap, as on that page.
class StatCap {
public:
    StatCap(uint8_t* ram,uint32_t pilot,uint32_t unit,uint32_t stat,bool apply=true):ram(ram),pilot(pilot),unit(unit),stat(stat) {
        if(!apply || !valid(pilot+stat,2) || !valid(unit,unit_limit+2))return;
        old_stat=uint16_t(read(ram,pilot+stat,2));
        old_mobility=uint16_t(read(ram,unit+unit_mobility,2));
        const uint16_t limit=uint16_t(read(ram,unit+unit_limit,2));
        if(uint32_t(old_stat)+old_mobility<=limit)return;
        const uint16_t capped=std::min(old_stat,limit);
        write16(ram,pilot+stat,capped);
        write16(ram,unit+unit_mobility,uint16_t(limit-capped));
        active=true;
    }
    ~StatCap() {
        if(!active)return;
        write16(ram,pilot+stat,old_stat);
        write16(ram,unit+unit_mobility,old_mobility);
    }
    StatCap(const StatCap&)=delete;
    StatCap& operator=(const StatCap&)=delete;
private:
    uint8_t* ram;
    uint32_t pilot,unit,stat;
    uint16_t old_stat{},old_mobility{};
    bool active{};
};


// Deployment record (28 bytes) read by 8020ABB4: +0x14 side (3 means the player
// side, 4 the third party), +0x16 behaviour, +0x18 the dummy count it writes into
// the new pilot record when behaviour bit 14 is set and the side is not the
// player's. 42 original records carry it, with 2, 3, 5 or 7 dummies.
inline constexpr uint32_t record_side=0x14,record_behaviour=0x16,record_dummies=0x18,record_dummy_bit=0x4000;
inline uint32_t record_faction(uint32_t raw){return raw==3?0u:raw==4?2u:raw;}
// Halving keeps one dummy, so the mechanic still shows up; "none" wins over it.
inline uint32_t scaled_dummies(uint32_t count,bool half,bool none) {
    if(none)return 0;
    if(!half || count<=1)return count;
    return std::max(1u,count/2);
}
// Rewrites the record for one original call and restores it afterwards; the block
// at 80199400 is the game's own per-scene copy, so nothing outlives the scene.
class DummyScale {
public:
    DummyScale(uint8_t* ram,uint32_t record):ram(ram),record(record) {
        const bool half=enabled(boss_dummy_half), none=enabled(boss_dummy_none);
        if((!half && !none) || !valid(record,0x1C))return;
        if(!(read(ram,record+record_behaviour,2)&record_dummy_bit))return;
        if(record_faction(read(ram,record+record_side,2))==0)return;
        old_count=uint16_t(read(ram,record+record_dummies,2));
        const uint16_t scaled=uint16_t(scaled_dummies(old_count,half,none));
        if(scaled==old_count)return;
        write16(ram,record+record_dummies,scaled);
        active=true;
    }
    ~DummyScale(){if(active)write16(ram,record+record_dummies,old_count);}
    DummyScale(const DummyScale&)=delete;
    DummyScale& operator=(const DummyScale&)=delete;
private:
    uint8_t* ram;
    uint32_t record;
    uint16_t old_count{};
    bool active{};
};

// Weapon upgrade inheritance on a machine swap (docs/upgrade-inheritance.md).
// 800AA8F4 moves a weapon's upgrade level to the successor only when the ROM table
// D_800CB5E8 lists that weapon; three pairs are missing although both machines
// carry the weapon under the same name, with every value but attack power equal,
// so those levels drop to 0. The three source ids exist on their predecessor only
// and the three targets on their successor only, so nothing else can match.
inline constexpr uint32_t unit_size=0x54,unit_weapon_count=0x2C,unit_weapon_list=0x30;
inline constexpr uint32_t weapon_size=0x24,weapon_number=0x02,weapon_level=0x16;
// One saved weapon as 800AA868 stores it: number, level, flags.
inline constexpr uint32_t saved_size=4,saved_number=0,saved_level=2;
struct WeaponPair {uint16_t from,to;};
inline constexpr WeaponPair inherit_gaps[]={{1070,1057},   // レイズナー → ニューレイズナー 火炎放射器
                                            {1072,1059},   // レイズナー → ニューレイズナー グレネードランチャー
                                            {470,1291}};   // アルトロン → アルトロンカスタム ドラゴンファイヤー
// The level the old machine had for this weapon; -1 when it did not carry it.
inline int saved_weapon_level(const uint8_t* ram,uint32_t saved,int count,uint16_t number) {
    for(int i=0;i<count;++i) {
        const uint32_t entry=saved+uint32_t(i)*saved_size;
        if(uint16_t(read(ram,entry+saved_number,2))==number)return int(read(ram,entry+saved_level,1));
    }
    return -1;
}
// Same shape as the original's write: the first weapon of the new machine whose
// number matches, level only. Power is left to 800A5254, which every caller of
// 800AA8F4 reaches through 800AAB5C right afterwards.
inline bool write_weapon_level(uint8_t* ram,uint32_t unit,uint16_t number,uint8_t level) {
    const uint32_t count=read(ram,unit+unit_weapon_count,1),list=read(ram,unit+unit_weapon_list,4);
    if(!count || !valid(list,count*weapon_size))return false;
    for(uint32_t i=0;i<count;++i) {
        const uint32_t weapon=list+i*weapon_size;
        if(uint16_t(read(ram,weapon+weapon_number,2))!=number)continue;
        write8(ram,weapon+weapon_level,level);
        return true;
    }
    return false;
}
// Runs after the original call, on the same arguments: new machine, the saved
// weapons of the predecessor, and their count.
inline void inherit_missing_weapons(uint8_t* ram,uint32_t unit,uint32_t saved,int count) {
    if(!enabled(weapon_inherit_map) || count<=0)return;
    if(!valid(unit,unit_size) || !valid(saved,uint32_t(count)*saved_size))return;
    for(const auto& pair:inherit_gaps) {
        const int level=saved_weapon_level(ram,saved,count,pair.from);
        if(level<0)continue;
        write_weapon_level(ram,unit,pair.to,uint8_t(level));
    }
}

// 聖戦士 was meant to raise the power of the Hyper Aura attacks, but no routine
// ever reads the skill for power: 801F5628 (battle damage) and 80203418 (AI damage
// estimate) take weapon +0x06 as is. The unreferenced row at 802180C8 (0,20,...,150,
// right after the hit/evade rows of that table) times 10 gives +200..+1500, and
// +1500 at L9 is the value the guidebook-era sources quote. It applies to the
// weapons gated by condition 15 (聖戦士 L3: ハイパーオーラ斬り, ハイパーオーラキャノン,
// ハイパーオーラショットアーム, ツインオーラアタック), in weapon-power units (2400 = 2400).
inline constexpr uint32_t aura_attack_table=0x802180C8, weapon_power=0x06, weapon_condition=0x0F;
inline constexpr uint8_t hyper_aura_condition=15;
inline uint32_t aura_slash_bonus(const uint8_t* ram,uint32_t pilot,uint32_t weapon) {
    if(!valid(pilot+0x36,1) || !valid(weapon+weapon_condition,1))return 0;
    if(!(read(ram,pilot+0x36,1)&0x20) || read(ram,weapon+weapon_condition,1)!=hyper_aura_condition)return 0;
    const uint32_t level=read(ram,pilot+0x06,1);
    return level<10?read(ram,aura_attack_table+level,1)*10:0;
}
// Raises the weapon's power for one original damage call and restores it.
class WeaponPower {
public:
    WeaponPower(uint8_t* ram,uint32_t weapon,uint32_t bonus):ram(ram),weapon(weapon) {
        if(!bonus || !valid(weapon+weapon_power,2))return;
        old=uint16_t(read(ram,weapon+weapon_power,2));
        write16(ram,weapon+weapon_power,uint16_t(std::min<uint32_t>(0xFFFF,old+bonus)));
        active=true;
    }
    ~WeaponPower(){if(active)write16(ram,weapon+weapon_power,old);}
    WeaponPower(const WeaponPower&)=delete;
    WeaponPower& operator=(const WeaponPower&)=delete;
private:
    uint8_t* ram;
    uint32_t weapon;
    uint16_t old{};
    bool active{};
};

// Combat participant table read by 801F4384: +0x04 unit, +0x0C pilot.
inline constexpr uint32_t participants=0x8018B6E8,participant_size=0x5C;
struct Combatant {uint32_t pilot,unit;};
inline Combatant participant(const uint8_t* ram,uint32_t slot) {
    const uint32_t base=participants+(slot&0xFF)*participant_size;
    return {read(ram,base+0x0C,4),read(ram,base+0x04,4)};
}
}
