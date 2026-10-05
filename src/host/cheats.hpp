#pragma once
#include "guest_memory.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The settings' cheats page (docs/gameplay/cheats.md). Five switches the game thread
// holds every frame, and setting a pilot's level, which waits for the インターミッション
// menu. The values come from the original's own limits. What they write is the game's
// own state and is saved with it; the switches themselves are not.
namespace srw64::cheats {

enum Switch : unsigned {max_funds=1,max_parts=2,keep_en=4,keep_sp=8,morale_150=16};
struct Entry {Switch bit;const char* id;};
inline constexpr Entry catalog[]={{max_funds,"funds"},{max_parts,"parts"},{keep_en,"en"},{keep_sp,"sp"},{morale_150,"morale"}};

unsigned active();
void set_active(unsigned switches);

// A player pilot as the インターミッション menu last listed it (801C549C's rule).
struct Pilot {unsigned index{},number{},level{};std::string name;};
// The pilots while the menu is up; empty anywhere else, where levels cannot change.
std::vector<Pilot> pilots();
// Asks for pilot `index` (in the player table) at `level`, 1-99; applied on the menu.
void request_level(unsigned index,unsigned level);

// Installs the game-thread hooks; SRW64_CHEATS (comma-separated ids) turns
// switches on for a run, for checks.
void configure(const std::filesystem::path& directory);

// Funds: a u32 every adder leaves unbounded; every display prints it as %8d.
inline constexpr uint32_t funds=0x8010F5F4,funds_max=99999999;
// 強化パーツ: 18 kinds, a byte held and a byte fitted each (800A8BC8 counts 0x12).
// Drops stop at 9 held (801F88C0) and the parts page has room for one digit.
inline constexpr uint32_t parts=0x8015E990;
inline constexpr unsigned part_kinds=18,parts_max=9;
// The player's machines: bank 0 of 8016A210, 140 of 0x54; +0 non-zero is in use
// (800A6FA8). +8 EN, +0xA its maximum (800A55D0 refills one from the other).
inline constexpr uint32_t units=0x8016A210,unit_size=0x54;
inline constexpr unsigned unit_count=140;
// The player's pilots: 100 records of 0x4C (800A84F8). +0 in use, 0x80 set for one
// the menus leave out (801C549C); +4 bits 0xC0 mark a pilot whose morale the game
// never resets (801F12D0); +5 level, +0x12 experience, +0x16 SP, +0x18 its maximum,
// +0x20 morale, capped at 150 by every raise (801FAD58, 801E1460).
inline constexpr uint32_t pilots_table=0x80172F40,pilot_size=0x4C;
inline constexpr unsigned pilot_count=100,morale_max=150,level_max=99,experience_per_level=500;
inline constexpr uint16_t text_pilot_names=0x111E;
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
// 800AC220's way of setting a level: the level and the experience it stands for.
// The caller then recomputes the record (800A7F8C).
inline void write_level(uint8_t* ram,uint32_t pilot,unsigned level) {
    using namespace guest;
    level=std::clamp(level,1u,level_max);
    write8(ram,pilot+5,uint8_t(level));
    write16(ram,pilot+0x12,uint16_t((level-1)*experience_per_level));
}
}
