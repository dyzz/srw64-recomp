#pragma once
#include "upgrade_rules.hpp"
#include <optional>

// Refund of upgrade funds when the story removes a machine (the difficulty rule
// upgrade-refund; docs/gameplay/upgrade-inheritance.md 8.3, docs/gameplay/rule-fixes.md).
//
// Story removals delete the machine instance, and with it every upgrade the
// player paid for: 3D5A modes 3000/4000, a registration that deletes the old
// machine (800AA464, e.g. ミデア → アウドムラ), and the ゴッドマーズ merge (800AB808).
// With the rule on, what the deleted levels cost at the current prices goes back
// into the funds just before 800AA3C4 frees the instance. Not refunded:
//  - the instance a registration inherits from (800AAD28 step 4): its levels
//    live on in the successor;
//  - levels the story gave for free with 3D6C;
//  - sales, which the sale screen prices itself, and removals on the map
//    (800A7DEC), since both call 800AA3C4 outside the scopes below;
//  - instances outside the player's pool.
namespace srw64::refund {
using guest::read;
using guest::valid;
using guest::write32;
using upgrades::unit_size, upgrades::units, upgrades::unit_slots, upgrades::unit_number, upgrades::unit_levels;
using upgrades::unit_weapon_count, upgrades::unit_weapon_list, upgrades::weapon_size, upgrades::weapon_level;
using upgrades::weapon_type, upgrades::unit_cap;

inline constexpr uint32_t funds=0x8010F5F4;
// 800AA814's table: 30 (successor, predecessor) s16 pairs.
inline constexpr uint32_t predecessors=0x800CA3A0, predecessor_count=30;

// The highest level each machine gets for free from 3D6C in the original event
// scripts (38 commands on 26 machines; tests/test_upgrade_refund.py checks the
// list against the ROM). 3D6C writes that level, capped by the machine's own cap,
// into the five stats and every weapon, so an instance that received it has no
// upgradeable item below it.
struct Grant {uint16_t unit;uint8_t levels;};
inline constexpr Grant story_grants[]={
    {35,3},{54,1},{55,1},{58,1},{59,1},{74,3},{75,3},{79,3},{80,6},{115,3},{119,3},{123,3},{124,3},
    {126,3},{128,3},{132,3},{157,3},{188,3},{264,5},{271,3},{272,3},{274,4},{295,8},{327,4},{328,3},{339,3}};
inline uint8_t story_grant(uint16_t unit) {
    for(const auto& grant:story_grants)if(grant.unit==unit)return grant.levels;
    return 0;
}

// Pool 0 of the unit allocator (800A6E68): the player's 140 machines. Other
// pools start past it, and the story removals only ever scan this one.
inline bool player_unit(uint32_t unit) {
    return unit>=units && unit<units+unit_slots*unit_size && (unit-units)%unit_size==0;
}

struct Cost {
    uint32_t stats{}, weapons{};
    uint8_t free_levels{};
    uint32_t total() const {return stats+weapons;}
};
// Going from level n to n+1 costs prices[n], as the upgrade screen charges it.
inline uint32_t curve_cost(const upgrades::Curve& curve,unsigned from,unsigned to) {
    uint32_t sum=0;
    for(unsigned n=from;n<to && n<upgrades::levels;++n)sum+=curve.prices[n];
    return sum;
}
// What the machine's levels above the story's free ones cost. The grant counts as
// free only when no upgradeable item is below it: a single lower item means the
// grant never happened on this instance (another route, or levels carried over
// from a predecessor and bought there), and then nothing is free. Weapons of
// type 0 cannot be upgraded and cost nothing.
inline Cost assess(const uint8_t* ram,uint32_t unit,const upgrades::Tables& tables) {
    Cost cost;
    const uint32_t count=read(ram,unit+unit_weapon_count,1), list=read(ram,unit+unit_weapon_list,4);
    const bool armed=count && valid(list,count*weapon_size);
    auto type_of=[&](uint32_t i){return unsigned(read(ram,list+i*weapon_size+weapon_type,1));};
    auto level_of=[&](uint32_t i){return unsigned(read(ram,list+i*weapon_size+weapon_level,1));};
    unsigned lowest=upgrades::levels;
    for(unsigned stat=0;stat<upgrades::stat_count;++stat)lowest=std::min(lowest,unsigned(read(ram,unit+unit_levels+stat,1)));
    if(armed)for(uint32_t i=0;i<count;++i)
        if(type_of(i)>=1 && type_of(i)<=upgrades::weapon_type_count)lowest=std::min(lowest,level_of(i));
    const unsigned grant=std::min<unsigned>(story_grant(uint16_t(read(ram,unit+unit_number,2))),read(ram,unit+unit_cap,1));
    cost.free_levels=uint8_t(lowest>=grant?grant:0);
    for(unsigned stat=0;stat<upgrades::stat_count;++stat)
        cost.stats+=curve_cost(tables.stats[stat],cost.free_levels,read(ram,unit+unit_levels+stat,1));
    if(armed)for(uint32_t i=0;i<count;++i)
        if(type_of(i)>=1 && type_of(i)<=upgrades::weapon_type_count)
            cost.weapons+=curve_cost(tables.weapons[type_of(i)-1],cost.free_levels,level_of(i));
    return cost;
}

// The first in-use instance of the successor's predecessor, as 800AAD28 finds
// it before saving its levels; 0 when the successor has none.
inline uint32_t predecessor_instance(const uint8_t* ram,int16_t successor) {
    for(uint32_t i=0;i<predecessor_count;++i) {
        const uint32_t pair=predecessors+i*4;
        if(int16_t(read(ram,pair,2))!=successor)continue;
        const int16_t predecessor=int16_t(read(ram,pair+2,2));
        for(uint32_t slot=0;slot<unit_slots;++slot) {
            const uint32_t unit=units+slot*unit_size;
            if(read(ram,unit,1) && int16_t(read(ram,unit+unit_number,2))==predecessor)return unit;
        }
        return 0;   // 800AA814 stops at the first matching pair
    }
    return 0;
}

// Which story routine is running. Guest code runs on one thread, so scoped state
// is enough, as for rules::Override.
enum class Source {none,removal,merge};
struct Context {Source source=Source::none;uint32_t inherited{};};
inline Context& context(){static Context value;return value;}
inline const char* name(Source source){return source==Source::merge?"merge":source==Source::removal?"removal":"none";}
// 800AA464 (story removal and a registration's old machine) or 800AB808 (merge).
class Removal {
public:
    explicit Removal(Source source):previous(context().source){context().source=source;}
    ~Removal(){context().source=previous;}
    Removal(const Removal&)=delete;
    Removal& operator=(const Removal&)=delete;
private:
    Source previous;
};
// 800AAD28 with a1 the new machine: its predecessor's levels move to it.
class Registration {
public:
    Registration(const uint8_t* ram,int16_t successor):previous(context().inherited) {
        context().inherited=predecessor_instance(ram,successor);
    }
    ~Registration(){context().inherited=previous;}
    Registration(const Registration&)=delete;
    Registration& operator=(const Registration&)=delete;
private:
    uint32_t previous;
};

// Machine names are text table 0 from id 527 (the data catalog's unit_base):
// the glyph codes of one, without its 8-byte header and FFFF terminator.
inline constexpr uint16_t unit_name_base=527;
inline std::vector<uint16_t> unit_name(uint16_t unit) {
    std::vector<uint16_t> codes;
    if(!upgrades::state().rom || unit>=upgrades::unit_record_count)return codes;
    const uint32_t offset=upgrades::rom_read(upgrades::text_table+4+uint32_t(unit_name_base+unit)*8,4);
    for(uint32_t at=upgrades::text_table+offset+8;codes.size()<32;at+=2) {
        const uint16_t code=uint16_t(upgrades::rom_read(at,2));
        if(code==0xFFFF)break;
        codes.push_back(code);
    }
    return codes;
}

struct Paid {
    uint16_t unit{};
    Cost cost;
    uint32_t funds_before{}, funds_after{};
    Source source{};
};
inline std::filesystem::path& directory(){static std::filesystem::path value;return value;}
inline void configure(const std::filesystem::path& output){directory()=output;context()={};}
// 800AA3C4 wrapper, before the original frees the instance in a0. Every story
// deletion is logged while the rule is on, including those that pay nothing.
inline std::optional<Paid> before_delete(uint8_t* ram,uint32_t unit) {
    const auto& scope=context();
    if(!rules::enabled(rules::upgrade_refund) || scope.source==Source::none)return std::nullopt;
    if(!player_unit(unit) || !read(ram,unit,1))return std::nullopt;
    Paid paid{uint16_t(read(ram,unit+unit_number,2)),{},read(ram,funds,4),0,scope.source};
    const bool inherited=unit==scope.inherited;
    if(!inherited)paid.cost=assess(ram,unit,upgrades::state().active);
    paid.funds_after=uint32_t(std::min<uint64_t>(uint64_t(paid.funds_before)+paid.cost.total(),0xFFFFFFFFu));
    if(paid.funds_after!=paid.funds_before)write32(ram,funds,paid.funds_after);
    if(!directory().empty())
        std::ofstream(directory()/"upgrade-refund-events.jsonl",std::ios::app)
            <<nlohmann::json({{"schema","srw64.upgrade-refund.v1"},{"vi",srw64_current_vi()},{"unit",paid.unit},
                {"source",name(paid.source)},{"inherited",inherited},{"free_levels",paid.cost.free_levels},
                {"stats",paid.cost.stats},{"weapons",paid.cost.weapons},{"refund",paid.cost.total()},
                {"funds_before",paid.funds_before},{"funds_after",paid.funds_after}}).dump()<<'\n';
    if(!paid.cost.total())return std::nullopt;
    return paid;
}
}
