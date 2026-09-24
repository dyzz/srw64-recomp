#pragma once
#include "guest_memory.hpp"
#include "rule_fixes.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

// Carrying 強化パーツ across a machine swap (the difficulty rule parts-carry-over;
// docs/gameplay/upgrade-inheritance.md 8.5).
//
// Freeing a machine unequips everything on it: 800AA3C4 → 800A9DCC(.., 0, 1) →
// 800A9D60 writes -1 over the slots, clears the machine's equipped count and takes
// one off the inventory's equipped count, leaving the owned count alone. So the
// parts are not lost, they wait in the inventory, but the successor starts bare and
// nothing can be re-equipped until the next intermission — a machine that swaps
// mid-stage and deploys on its own fights that stage without them. ダンクーガ's
// in-place upgrade (800ACB74) strips its own parts the same way.
//
// With the rule on, the parts on the pilot's machine are noted before 800AAD28 runs
// and equipped on the successor afterwards, in slot order, as many as it has slots:
// the five EW machines and the three 真ゲッター forms have one slot where their
// predecessor had two, and whatever does not fit simply stays in the inventory.
namespace srw64::parts_carry {
using guest::read;
using guest::valid;
using guest::write8;

// Machine instances (140 x 0x54) and their part slots, as parts_page.cpp reads them.
inline constexpr uint32_t units=0x8016A210, unit_slots=140, unit_size=0x54;
inline constexpr uint32_t unit_number=0x02, unit_slot_count=0x21, unit_equipped=0x22, unit_parts=0x23;
// Pilot records (100 x 0x4C): +0x37 set when +0x38 points at the machine, as 800AA464 reads them.
inline constexpr uint32_t pilots=0x80172F40, pilot_slots=100, pilot_size=0x4C;
inline constexpr uint32_t pilot_number=0x02, pilot_has_unit=0x37, pilot_unit=0x38;
// One u16 per part: high byte owned, low byte equipped (801CBB18 draws both).
inline constexpr uint32_t inventory=0x8015E990, part_count=18;
inline constexpr unsigned max_slots=8;   // the largest +0x21 in the machine table is 3

inline bool player_unit(uint32_t unit) {
    return unit>=units && unit<units+unit_slots*unit_size && (unit-units)%unit_size==0;
}
inline uint32_t unit_at(uint32_t slot){return units+slot*unit_size;}

// What one machine carries. `equipped` is its count at capture time: the original
// clears it when it strips the parts, so a run where it did not (800AAD28 returns
// early because the pilot already has this machine) is told apart from one where it did.
struct Carry {
    uint32_t source{};
    uint16_t source_number{}, successor{};
    uint8_t equipped{}, count{};
    std::array<uint8_t,max_slots> parts{};
    bool empty() const {return count==0;}
};

// The machine this pilot is in, or 0. 999 means the registration has no pilot.
inline uint32_t pilot_machine(const uint8_t* ram,int16_t actor) {
    if(actor==999)return 0;
    for(uint32_t slot=0;slot<pilot_slots;++slot) {
        const uint32_t pilot=pilots+slot*pilot_size;
        if(!read(ram,pilot,1) || int16_t(read(ram,pilot+pilot_number,2))!=actor)continue;
        if(!read(ram,pilot+pilot_has_unit,1))return 0;
        const uint32_t unit=read(ram,pilot+pilot_unit,4);
        return player_unit(unit)?unit:0;
    }
    return 0;
}

// Before 800AAD28: a0 the pilot, a1 the machine being registered.
inline Carry capture(const uint8_t* ram,int16_t actor,int16_t successor) {
    Carry carry;
    if(!rules::enabled(rules::parts_carry_over))return carry;
    const uint32_t unit=pilot_machine(ram,actor);
    if(!unit || !read(ram,unit,1))return carry;
    carry.source=unit;
    carry.source_number=uint16_t(read(ram,unit+unit_number,2));
    carry.successor=uint16_t(successor);
    carry.equipped=uint8_t(read(ram,unit+unit_equipped,1));
    const unsigned slots=std::min<unsigned>(read(ram,unit+unit_slot_count,1),max_slots);
    for(unsigned i=0;i<slots;++i) {
        const int8_t part=int8_t(read(ram,unit+unit_parts+i,1));
        if(part>=0)carry.parts[carry.count++]=uint8_t(part);
    }
    return carry;
}

// The first in-use instance of this machine, the way the original finds one.
inline uint32_t instance_of(const uint8_t* ram,uint16_t number) {
    for(uint32_t slot=0;slot<unit_slots;++slot) {
        const uint32_t unit=unit_at(slot);
        if(read(ram,unit,1) && uint16_t(read(ram,unit+unit_number,2))==number)return unit;
    }
    return 0;
}
// True when the original left the captured machine untouched, so its parts are
// still on it and re-equipping them would count them twice.
inline bool still_equipped(const uint8_t* ram,const Carry& carry) {
    return read(ram,carry.source,1) && uint16_t(read(ram,carry.source+unit_number,2))==carry.source_number &&
           read(ram,carry.source+unit_equipped,1)==carry.equipped;
}

struct Carried {uint16_t unit{};std::vector<uint8_t> parts, left{};};

// After 800AAD28: fill the successor's free slots, in the order the old machine
// had them. Takes one part back out of the inventory for each, the reverse of what
// 800A9D60 put there.
inline std::optional<Carried> apply(uint8_t* ram,const Carry& carry) {
    if(carry.empty() || !rules::enabled(rules::parts_carry_over))return std::nullopt;
    if(still_equipped(ram,carry))return std::nullopt;
    const uint32_t unit=instance_of(ram,carry.successor);
    if(!unit)return std::nullopt;
    const unsigned slots=std::min<unsigned>(read(ram,unit+unit_slot_count,1),max_slots);
    Carried carried{carry.successor};
    for(unsigned i=0;i<carry.count;++i) {
        const uint8_t part=carry.parts[i];
        if(part>=part_count){carried.left.push_back(part);continue;}
        const uint32_t record=inventory+uint32_t(part)*2;
        const unsigned owned=read(ram,record,1), equipped=read(ram,record+1,1);
        unsigned free_slot=slots;
        for(unsigned s=0;s<slots;++s)if(int8_t(read(ram,unit+unit_parts+s,1))<0){free_slot=s;break;}
        if(free_slot==slots || equipped>=owned){carried.left.push_back(part);continue;}
        write8(ram,unit+unit_parts+free_slot,part);
        write8(ram,unit+unit_equipped,uint8_t(read(ram,unit+unit_equipped,1)+1));
        write8(ram,record+1,uint8_t(equipped+1));
        carried.parts.push_back(part);
    }
    if(carried.parts.empty())return std::nullopt;
    return carried;
}

inline std::filesystem::path& directory(){static std::filesystem::path value;return value;}
inline void configure(const std::filesystem::path& output){directory()=output;}
inline void record(const Carry& carry,const Carried& carried) {
    if(directory().empty())return;
    std::ofstream(directory()/"parts-carry-events.jsonl",std::ios::app)
        <<nlohmann::json({{"schema","srw64.parts-carry.v1"},{"vi",srw64_current_vi()},
            {"from",carry.source_number},{"to",carried.unit},
            {"carried",carried.parts},{"left_in_inventory",carried.left}}).dump()<<'\n';
}
}
