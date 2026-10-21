#pragma once
// Combination attacks (合体技, docs/gameplay/combination-attacks.md): what the ROM's tables
// in the tactical overlay (load_000AB160) say about each one, read for the screens that
// list them while the partners are not around.
//
// A weapon with 0x02 in its ROM flag byte (+0xF; instance +0x22) is a combination
// attack. On the map 8020178C runs each time a weapon list opens and, for every such weapon
// of the unit, looks its number up in the 25-entry table D_80218A6C and calls that entry's
// check (D_80219444, one wrapper per attack, all but one ending in 80200530): the result,
// a power, clears the weapon's lock bit (+0x22 0x04) and lands in its power (+0x06); 0
// sets the lock. The intermission's lists (801C7254) never show a combination attack at
// all, the map's status page (801E6410) and attack list (801F8F1C) skip a locked one.
//
// 80200530(handle, unit, weapon, rows, index): the rows (14 bytes: s16 id, u8 by_pilot,
// u8 partners, s16 partner[4], s16 unit; -1 ends) name who takes part. The first row's
// by_pilot says how the whole table reads: 0 matches a row by the unit's number, 1 by
// the lead pilot's number with the unit's number equal to the row's +0xC. Each partner is
// the first unit of the same side on the field with that unit (or pilot, in that unit);
// all of them stand in one connected group (eight neighbours, 801E966C/801E95DC); each
// has the weapon's EN (+0xD) and morale (+0xE), and passes 801E6214 when the weapon
// names a skill (+0xF >= 2). Then D_80218AA0[index] (five (unit, weapon) pairs and a
// float) gives every participant's own component weapon: the power is the mean of those
// weapons' current powers times the float (trunc). A missing pair or weapon gives 0.
// The Mazinger/Getter/Dunbine wrappers also need one row of a pilot table (rows of 10:
// u8 count, u8 pad, s16 pilot[4]; 0 ends) wholly among the participants' lead pilots
// (80200404); 石破ラブラブ天驚拳 (80200B1C) needs the first 石破天驚拳 (19) in the weapon
// pool unlocked as well.
#include "guest_memory.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace srw64::combo_attacks {
constexpr uint32_t overlay_vram=0x801C2600,overlay_rom=0xAB160;
constexpr uint32_t rom_of(uint32_t vram){return vram-overlay_vram+overlay_rom;}
constexpr unsigned count=25;
constexpr uint32_t weapon_table=rom_of(0x80218A6C),check_table=rom_of(0x80219444),power_table=rom_of(0x80218AA0);
constexpr unsigned power_row=0x18,row_size=14,pilot_row=10;
constexpr uint8_t flag_combo=0x02,flag_locked=0x04;
constexpr uint16_t weapon_shippa=19;
// Each check wrapper's participant rows and, where it has one, its pilot table (the
// wrappers only differ in these two constants).
struct Wrapper{uint32_t function,rows,pilots;};
constexpr Wrapper wrappers[count]={
    {0x80200AB4,0x80218CF8,0},{0x80200B1C,0x80218D24,0},{0x80200BF4,0x80218D50,0},{0x80200C5C,0x80218DA4,0},{0x80200CC4,0x80218DFC,0},{0x80200D2C,0x80218E34,0},
    {0x80200D94,0x80218DD0,0},{0x80200DFC,0x80218EDC,0x80219190},{0x80200E88,0x80218F08,0x80219190},{0x80200F14,0x80218F34,0x80219190},{0x80200FA0,0x80218F50,0x80219190},
    {0x8020102C,0x80218F6C,0x80219200},{0x802010B8,0x80218F98,0x80219200},{0x80201144,0x80218FB4,0x80219200},{0x802011D0,0x80218FD0,0x8021925C},{0x8020125C,0x80219008,0x802192CC},
    {0x802012E8,0x80219050,0x8021933C},{0x80201374,0x80219088,0x802193AC},{0x80201400,0x802190B4,0x8021923C},{0x8020148C,0x80219164,0x8021923C},{0x80201518,0x802190E0,0x8021941C},
    {0x802015A4,0x8021910C,0x8021941C},{0x80201630,0x80219138,0x8021941C},{0x802016BC,0x80218E6C,0},{0x80201724,0x80218EA4,0}};

inline unsigned rom_u8(std::span<const uint8_t> rom,uint32_t at){return at<rom.size()?rom[at]:0;}
inline unsigned rom_u16(std::span<const uint8_t> rom,uint32_t at){return rom_u8(rom,at)<<8|rom_u8(rom,at+1);}
inline int rom_s16(std::span<const uint8_t> rom,uint32_t at){return int16_t(rom_u16(rom,at));}
inline uint32_t rom_u32(std::span<const uint8_t> rom,uint32_t at){return uint32_t(rom_u16(rom,at))<<16|rom_u16(rom,at+2);}
inline float rom_f32(std::span<const uint8_t> rom,uint32_t at){const uint32_t bits=rom_u32(rom,at);float f;static_assert(sizeof f==4);std::copy_n(reinterpret_cast<const char*>(&bits),4,reinterpret_cast<char*>(&f));return f;}

struct Row{int16_t id;bool by_pilot;std::vector<int16_t> partners;int16_t unit;};
struct Attack {
    unsigned index;uint16_t weapon;
    std::vector<Row> rows;                              // who takes part, one row per lead
    std::vector<std::vector<int16_t>> pilots;           // pilot pairings, any one row; empty: none needed
    std::array<std::pair<int16_t,int16_t>,5> parts;     // (unit, its component weapon); -1 unused
    float multiplier;
    uint16_t needs_unlocked;                            // a weapon number whose first pool instance must be unlocked, 0 none
};

inline std::optional<Attack> find(std::span<const uint8_t> rom,uint16_t weapon) {
    for(unsigned i=0;i<count;++i) {
        if(rom_u16(rom,weapon_table+i*2)!=weapon)continue;
        const uint32_t function=rom_u32(rom,check_table+i*4);
        const auto wrapper=std::find_if(std::begin(wrappers),std::end(wrappers),[&](const Wrapper& w){return w.function==function;});
        if(wrapper==std::end(wrappers))return std::nullopt;
        Attack a{i,weapon,{},{},{},rom_f32(rom,power_table+i*power_row+0x14),uint16_t(function==0x80200B1C?weapon_shippa:0)};
        for(unsigned n=0;n<5;++n)a.parts[n]={int16_t(rom_s16(rom,power_table+i*power_row+n*4)),int16_t(rom_s16(rom,power_table+i*power_row+n*4+2))};
        for(uint32_t at=rom_of(wrapper->rows);rom_s16(rom,at)!=-1 && at+row_size<=rom.size();at+=row_size) {
            Row r{int16_t(rom_s16(rom,at)),rom_u8(rom,at+2)!=0,{},int16_t(rom_s16(rom,at+0xC))};
            for(unsigned n=0;n<std::min(4u,rom_u8(rom,at+3));++n)r.partners.push_back(int16_t(rom_s16(rom,at+4+n*2)));
            a.rows.push_back(std::move(r));
        }
        if(wrapper->pilots)for(uint32_t at=rom_of(wrapper->pilots);rom_u8(rom,at) && at+pilot_row<=rom.size();at+=pilot_row) {
            std::vector<int16_t> ids;
            for(unsigned n=0;n<std::min(4u,rom_u8(rom,at));++n)ids.push_back(int16_t(rom_s16(rom,at+2+n*2)));
            a.pilots.push_back(std::move(ids));
        }
        return a;
    }
    return std::nullopt;
}
// The row this unit leads: by its number, or (a pilot table) by its lead pilot's number
// while the unit is the table's one unit. None: this form never uses the attack.
inline const Row* lead_row(const Attack& a,int unit_number,int pilot_number) {
    if(a.rows.empty())return nullptr;
    const bool by_pilot=a.rows.front().by_pilot;
    if(by_pilot && a.rows.front().unit!=unit_number)return nullptr;
    for(const auto& r:a.rows)if(r.id==(by_pilot?pilot_number:unit_number))return &r;
    return nullptr;
}
// The component weapon the power table gives a unit, if any.
inline std::optional<uint16_t> component(const Attack& a,int unit_number) {
    for(const auto& [unit,weapon]:a.parts)if(unit==unit_number)return uint16_t(weapon);
    return std::nullopt;
}
// 80200404: some pilot row lies wholly among the lead pilots present.
inline bool pilots_met(const Attack& a,const std::vector<int>& lead_pilots) {
    if(a.pilots.empty())return true;
    for(const auto& row:a.pilots)
        if(std::all_of(row.begin(),row.end(),[&](int16_t id){return std::find(lead_pilots.begin(),lead_pilots.end(),id)!=lead_pilots.end();}))return true;
    return false;
}
// The weapon pool's first in-use instance of a weapon (80200B1C), 0 when none.
inline uint32_t pool_instance(const uint8_t* ram,uint16_t weapon) {
    constexpr uint32_t pool=0x80178F80,size=0x24;constexpr unsigned entries=700;
    for(unsigned i=0;i<entries;++i) {
        const uint32_t at=pool+i*size;
        if(guest::read(ram,at,1) && guest::read(ram,at+2,2)==weapon)return at;
    }
    return 0;
}
// The instance of weapon `number` in a unit's array, 0 when the unit has none.
inline uint32_t unit_weapon(const uint8_t* ram,uint32_t unit,uint16_t number) {
    const uint32_t n=guest::read(ram,unit+0x2C,1),list=guest::read(ram,unit+0x30,4);
    for(uint32_t i=0;i<n;++i)if(guest::read(ram,list+i*0x24+2,2)==number)return list+i*0x24;
    return 0;
}
}
