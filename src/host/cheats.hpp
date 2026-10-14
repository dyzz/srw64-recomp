#pragma once
#include "guest_memory.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The settings' cheats page (docs/gameplay/cheats.md). Six switches the game thread
// holds every frame, and setting a pilot's level, which waits for the インターミッション
// menu. The values come from the original's own limits. What they write is the game's
// own state and is saved with it; the switches themselves are not.
namespace srw64::cheats {

enum Switch : unsigned {max_funds=1,max_parts=2,keep_en=4,keep_sp=8,morale_150=16,four_slots=32,level_curve=64,level_party=128};
struct Entry {Switch bit;const char* id;};
inline constexpr Entry catalog[]={{max_funds,"funds"},{max_parts,"parts"},{four_slots,"slots"},{keep_en,"en"},{keep_sp,"sp"},{morale_150,"morale"},
    {level_party,"party"},{level_curve,"curve"}};
// The base level row offers three choices, not switches: the original, the party's (no
// cap) or the chapter curve. At most one of the two bits is set.
inline constexpr unsigned level_modes=level_curve|level_party;
inline constexpr bool toggle(Switch bit){return !(bit&level_modes);}

unsigned active();
void set_active(unsigned switches);

// A player pilot as the インターミッション menu last listed it (801C549C's rule).
struct Pilot {unsigned index{},number{},level{};std::string name;};
// The pilots while the menu is up; empty anywhere else, where levels cannot change.
std::vector<Pilot> pilots();
// Asks for pilot `index` (in the player table) at `level`, 1-99; applied on the menu.
void request_level(unsigned index,unsigned level);

// The four-slot switch's write, now rather than at the next frame: a machine built
// this frame (a swap) still has its ROM count when the parts-carry rule fills it.
void hold_slots(uint8_t* ram);
// The chosen base level, right after the original computes its own (800A4BE0).
void hold_base_level(uint8_t* ram);

// Installs the game-thread hooks; SRW64_CHEATS (comma-separated ids) turns
// switches on for a run, for checks.
void configure(const std::filesystem::path& directory);

// Funds: a u32 every adder leaves unbounded; every display prints it as %8d.
inline constexpr uint32_t funds=0x8010F5F4,funds_max=99999999;
// 強化パーツ: 18 kinds, a byte held and a byte fitted each (800A8BC8 counts 0x12).
// 9 is only where drops stop (801F88C0); a save keeps four bits of each count
// (80091A88), so 15 is the most a save written here still reads back as.
inline constexpr uint32_t parts=0x8015E990;
inline constexpr unsigned part_kinds=18,parts_max=15;
// The player's machines: bank 0 of 8016A210, 140 of 0x54; +0 non-zero is in use
// (800A6FA8). +8 EN, +0xA its maximum (800A55D0 refills one from the other).
inline constexpr uint32_t units=0x8016A210,unit_size=0x54;
inline constexpr unsigned unit_count=140;
// Part slots: +0x21 the count, copied from the ROM machine record's +0x19 whenever an
// instance is built (800A8CE8, also on every load); +0x22 the parts fitted, +0x23..+0x26
// the parts (s8, -1 empty). Four is all the record has room for.
inline constexpr uint32_t unit_number=0x02,unit_slot_count=0x21,unit_fitted=0x22,unit_parts=0x23;
inline constexpr uint32_t machine_records=0x71B80,machine_record_size=0x24,machine_record_slots=0x19;
inline constexpr unsigned machine_count=363,slots_max=4;
// The player's pilots: 100 records of 0x4C (800A84F8). +0 in use, 0x80 set for one
// the menus leave out (801C549C); +4 bits 0xC0 mark a pilot whose morale the game
// never resets (801F12D0); +5 level, +0x12 experience, +0x16 SP, +0x18 its maximum,
// +0x20 morale, capped at 150 by every raise (801FAD58, 801E1460).
inline constexpr uint32_t pilots_table=0x80172F40,pilot_size=0x4C;
inline constexpr unsigned pilot_count=100,morale_max=150,level_max=99,experience_per_level=500;
inline constexpr uint16_t text_pilot_names=0x111E;
// The base level (800A4BE0, on entering every map): the mean of the top 15 player
// pilots, between 1 and min(99, chapters cleared + 40, 95). New enemies start at it
// plus their record's offset (8020ABB4), new recruits at it plus the script's (800AAD28).
// level_party keeps the mean without those caps; level_curve follows the chapters cleared
// instead: 3 + 0.9 a chapter, at most 50, about what a party of 10-15 sharing every kill
// reaches (docs/gameplay/cheats.md §6).
inline constexpr uint32_t base_level=0x8010F5F3,chapters_cleared=0x8010F5EF;
inline constexpr unsigned curve_start=3,curve_tenths=9,curve_max=50,party_counted=15;
inline constexpr unsigned curve_level(unsigned cleared){return std::min(curve_max,curve_start+cleared*curve_tenths/10);}
// 800A4BE0's mean without its caps: pilots in use and in the party (+0 without 0x80)
// with a machine (+0x37, +0x38) the party has (+0xC bit 0x80, or one of 0xAB-0xB3).
inline unsigned party_level(const uint8_t* ram) {
    using namespace guest;
    std::vector<unsigned> levels;
    for(unsigned i=0;i<pilot_count;++i) {
        const uint32_t pilot=pilots_table+i*pilot_size;
        const unsigned state=read(ram,pilot,1);
        if(!state || (state&0x80) || !read(ram,pilot+0x37,1))continue;
        const uint32_t unit=read(ram,pilot+0x38,4);
        if(!valid(unit,0x54))continue;
        const unsigned number=read(ram,unit+unit_number,2);
        if(!(read(ram,unit+0xC,1)&0x80) && !(number>=0xAB && number<0xAB+9))continue;
        levels.push_back(read(ram,pilot+5,1));
    }
    if(levels.empty())return 1;
    std::sort(levels.rbegin(),levels.rend());
    levels.resize(std::min<size_t>(levels.size(),party_counted));
    unsigned sum=0;for(unsigned l:levels)sum+=l;
    return std::clamp(sum/unsigned(levels.size()),1u,level_max);
}
// The intermission's fade: -1 once a screen is idle.
inline constexpr uint32_t transition=0x8015E9C5;
// The menu counts as open while its step ran this recently.
inline constexpr uint64_t menu_grace_vis=20;

// What one frame writes for the switches `on` (tests/native_cheats.cpp).
inline void hold(uint8_t* ram,unsigned on) {
    using namespace guest;
    if(on&max_funds)write32(ram,funds,funds_max);
    if(on&max_parts)for(unsigned i=0;i<part_kinds;++i) {
        // The held count only; never below the fitted count beside it.
        const uint32_t entry=parts+i*2;
        write8(ram,entry,uint8_t(std::max<unsigned>(parts_max,read(ram,entry+1,1))));
    }
    if(on&level_curve)write8(ram,base_level,uint8_t(curve_level(read(ram,chapters_cleared,1))));
    else if(on&level_party)write8(ram,base_level,uint8_t(party_level(ram)));
    if(on&four_slots)for(unsigned i=0;i<unit_count;++i) {
        const uint32_t unit=units+i*unit_size;
        if(read(ram,unit,1))write8(ram,unit+unit_slot_count,slots_max);
    }
    // Written whatever the current value: a cancelled move gives EN back and the
    // recovery animations add in steps, either of which can pass the maximum.
    if(on&keep_en)for(unsigned i=0;i<unit_count;++i) {
        const uint32_t unit=units+i*unit_size;
        if(read(ram,unit,1))write16(ram,unit+8,uint16_t(read(ram,unit+0xA,2)));
    }
    if(on&(keep_sp|morale_150))for(unsigned i=0;i<pilot_count;++i) {
        const uint32_t pilot=pilots_table+i*pilot_size;
        if(!read(ram,pilot,1))continue;
        if(on&keep_sp)write16(ram,pilot+0x16,uint16_t(read(ram,pilot+0x18,2)));
        if((on&morale_150) && !(read(ram,pilot+4,1)&0xC0))write16(ram,pilot+0x20,morale_max);
    }
}
// With the four-slot switch off, a machine still showing more slots than its ROM
// record (`own`), or holding parts above it, gets back to that count: a load rebuilds
// +0x21 from ROM but puts back all four slots (8009282C). The parts above it go back to the
// inventory as the parts screen's はずす does (801D4C94): the slot to -1, one off the
// machine's and the inventory's fitted count. Left there, they would still count
// (800A5254 reads all four slots) but no screen would show them, and freeing the
// machine (800A9D60 stops at +0x21) would wipe the slots and leave the fitted count
// one too high for good. True when it changed the machine; the caller then copies
// its slots to the other forms (800A5924), so a part shared with them is returned once.
inline bool release_slots(uint8_t* ram,uint32_t unit,unsigned own) {
    using namespace guest;
    own=std::min(own,slots_max);
    if(!read(ram,unit,1))return false;
    bool above=read(ram,unit+unit_slot_count,1)>own;
    for(unsigned s=own;s<slots_max;++s)above|=int8_t(read(ram,unit+unit_parts+s,1))>=0;
    if(!above)return false;
    for(unsigned s=own;s<slots_max;++s) {
        const int8_t part=int8_t(read(ram,unit+unit_parts+s,1));
        if(part<0)continue;
        write8(ram,unit+unit_parts+s,0xFF);
        if(const unsigned fitted=read(ram,unit+unit_fitted,1))write8(ram,unit+unit_fitted,uint8_t(fitted-1));
        if(unsigned(part)>=part_kinds)continue;
        const uint32_t entry=parts+unsigned(part)*2;
        if(const unsigned fitted=read(ram,entry+1,1))write8(ram,entry+1,uint8_t(fitted-1));
    }
    write8(ram,unit+unit_slot_count,uint8_t(own));
    return true;
}
// 800AC220's way of setting a level: the level and the experience it stands for.
// The caller then recomputes the record (800A7F8C).
inline void write_level(uint8_t* ram,uint32_t pilot,unsigned level) {
    using namespace guest;
    level=std::clamp(level,1u,level_max);
    write8(ram,pilot+5,uint8_t(level));
    write16(ram,pilot+0x12,uint16_t((level-1)*experience_per_level));
}
}
