#pragma once
// New characters (docs/design/unit-mod-kit.md §5.4): the character -> record maps the game
// reads at copies in free RDRAM (generate_cpu.py PILOT_MAP_READERS). The ROM's 360 entries as
// the resident copy has them, then the new characters: its own number for the ability and
// spirit records (their rows are in the table window, mod_units.hpp), and for the battle
// quotes the voice slot of the character it speaks as; -1 for every other number.
#include "guest_memory.hpp"
#include "librecomp/game.hpp"
#include "mod_data.hpp"
#include "table_window.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace srw64::mod_pilots {
namespace tw=table_window;

// Game thread, at each frame boundary: rewrites a map only when it changed (a package came or
// went), or when something else wrote over it.
inline void fill(uint8_t* ram) {
    if(!ram)return;
    const auto copies=mod_data::pilot_copies();
    const auto voices=mod_data::voices();
    const auto original=[&](uint32_t map,unsigned p){return uint16_t(guest::read(ram,map+p*2,2));};
    const auto build=[&](uint32_t map,bool voice) {
        std::vector<uint16_t> out(tw::pilot_map_slots,0xFFFF);
        for(unsigned p=0;p<tw::rom_pilot_maps;++p)out[p]=original(map,p);
        // A new number no package gives (a save made with one, loaded without it, until
        // mod_saves.hpp turns it back) reads character 0's records, never a row before a table.
        if(map==tw::pilot_map_ram)for(unsigned p=tw::first_new_pilot;p<=tw::last_new_pilot;++p)out[p]=original(map,0);
        for(const auto& [n,source]:copies) {
            if(n>=out.size())continue;
            if(!voice){out[n]=uint16_t(n);continue;}
            const auto found=voices.find(n);
            out[n]=original(map,found!=voices.end()?found->second:source);
        }
        return out;
    };
    const std::array<std::pair<uint32_t,std::vector<uint16_t>>,3> maps={{
        {tw::pilot_stats_copy,build(tw::pilot_map_ram,false)},
        {tw::pilot_voice_copy,build(tw::pilot_map_ram,true)},
        {tw::pilot_spirit_copy,build(tw::spirit_map_ram,false)}}};
    for(const auto& [copy,values]:maps)
        for(unsigned p=0;p<values.size();++p)
            if(uint16_t(guest::read(ram,copy+p*2,2))!=values[p])guest::write16(ram,copy+p*2,values[p]);
    // The love pairs: the ROM's 47 rows, the packages' (holder, partner, none), then empty rows.
    std::vector<uint8_t> love(tw::love_rows*tw::love_row,0xFF);
    const auto rom=recomp::get_rom();
    for(uint32_t i=0;i<tw::love_rom_rows*tw::love_row && tw::love_rom+i<rom.size();++i)love[i]=rom[tw::love_rom+i];
    unsigned row=tw::love_rom_rows;
    for(const auto& [holder,partner]:mod_data::loves()) {
        if(row>=tw::love_rows)break;
        uint8_t* r=&love[row++*tw::love_row];
        r[0]=0;r[1]=0;r[2]=uint8_t(holder>>8);r[3]=uint8_t(holder);r[4]=uint8_t(partner>>8);r[5]=uint8_t(partner);
    }
    for(uint32_t i=0;i<love.size();++i)if(guest::read(ram,tw::love_copy+i,1)!=love[i])guest::write8(ram,tw::love_copy+i,love[i]);
}

// Combination attacks (8020178C, which works out which are usable): while it runs, a new
// character that counts as an original one carries that one's number in its pilot record, so
// the pilot-keyed rows and the pilot combination tables take it; put back after.
using Swaps=std::vector<std::pair<uint32_t,uint16_t>>;
inline Swaps combo_aliases(uint8_t* ram) {
    Swaps out;
    const auto as=mod_data::combo_as();
    if(as.empty())return out;
    for(uint32_t i=0;i<100;++i) {
        const uint32_t p=0x80172F40+i*0x4C;
        if(!guest::read(ram,p,1))continue;
        const unsigned number=guest::read(ram,p+2,2);
        if(const auto found=as.find(number);found!=as.end()){out.push_back({p+2,uint16_t(number)});guest::write16(ram,p+2,uint16_t(found->second));}
    }
    return out;
}
inline void restore(uint8_t* ram,const Swaps& swaps){for(const auto& [at,v]:swaps)guest::write16(ram,at,v);}
// The number a character counts as for combination attacks (the host's own pages).
inline unsigned combo_number(unsigned pilot) {
    const auto as=mod_data::combo_as();
    const auto found=as.find(pilot);
    return found==as.end()?pilot:found->second;
}
}
