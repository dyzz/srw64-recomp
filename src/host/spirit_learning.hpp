#pragma once
// A pilot's spirit learning table in ROM, as the Library reads it (library.cpp): the actor
// (pilot record +0x02) leads through the s16 map at 800CA6F4 (ROM 0x550E4) to a record of
// six (level, command) pairs at ROM 0x7CFB0; a level of 0 or past 99, or a command past 29,
// is no entry. A pilot learns them in this order (+0x0A counts those learned, +0x0B.. ids).
#include "librecomp/game.hpp"
#include "upgrade_rules.hpp"
#include <cstdint>
#include <vector>

namespace srw64::spirit_learning {
struct Entry { unsigned level, id; };
inline constexpr uint32_t map_rom=0x800CA6F4-0x80075610,records_rom=0x7CFB0,record_size=12;
inline constexpr unsigned mapped_actors=360;
inline std::vector<Entry> table(unsigned actor) {
    std::vector<Entry> out;
    const auto rom=recomp::get_rom();
    // As the game reads them, a number MOD's (mod_data.hpp) over the ROM's.
    const auto u8=[&](uint32_t at)->unsigned{return upgrades::game_byte({rom.data(),rom.size()},at);};
    if(actor>=mapped_actors)return out;
    const int record=int16_t(uint16_t(u8(map_rom+actor*2)<<8|u8(map_rom+actor*2+1)));
    if(record<0)return out;
    for(unsigned n=0;n<6;++n) {
        const uint32_t at=records_rom+uint32_t(record)*record_size+n*2;
        if(const unsigned level=u8(at),id=u8(at+1);level && level<100 && id<30)out.push_back({level,id});
    }
    return out;
}
}
