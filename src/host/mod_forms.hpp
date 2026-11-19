#pragma once
// Form families of new units (docs/design/unit-mod-kit.md §4.6): the original's family list
// D_800CAC94 (unit numbers, -1) and its record pointers D_800CAEF0 are read at copies in free
// RDRAM (generate_cpu.py FORMS_*_READERS), the ROM's 60 families followed by the packages'.
// A record is 8 bytes: +0 how many other forms (1-2), +2 and +4 those forms (-1 none), +6 the
// family's base form. 800A87FC finds a unit's index; 800A7330 builds the other forms'
// instances sharing the first's weapons and crew; the map's 変形 changes between them.
#include "guest_memory.hpp"
#include "mod_data.hpp"
#include <cstdint>
#include <vector>

namespace srw64::mod_forms {
using guest::read;
using guest::write8;
using guest::write16;
using guest::write32;

inline constexpr uint32_t list_ram=0x800CAC94, pointers_ram=0x800CAEF0;
inline constexpr uint32_t list_copy=0x807D4000, pointers_copy=0x807D4100, records_copy=0x807D4300;   // generate_cpu.py FORMS_*_COPY
inline constexpr unsigned capacity=128, record_size=8;

// One record of a family member: the others in the family's order, the base form.
inline std::vector<uint8_t> record(const std::vector<unsigned>& family,unsigned member) {
    std::vector<uint8_t> out(record_size,0xFF);
    unsigned k=0;
    for(const unsigned other:family) {
        if(other==member || k==2)continue;
        out[2+k*2]=uint8_t(other>>8);out[3+k*2]=uint8_t(other);++k;
    }
    out[0]=uint8_t(k);out[1]=0;
    out[6]=uint8_t(family[0]>>8);out[7]=uint8_t(family[0]);
    return out;
}

// Game thread, at each frame boundary: the copies as they should be, written only where they differ.
inline void fill(uint8_t* ram) {
    std::vector<uint16_t> list;std::vector<uint32_t> pointers;
    for(unsigned i=0;i<capacity-1;++i) {
        const uint16_t unit=uint16_t(read(ram,list_ram+i*2,2));
        if(unit==0xFFFF)break;
        list.push_back(unit);pointers.push_back(read(ram,pointers_ram+i*4,4));
    }
    unsigned slot=0;
    for(const auto& [base,family]:mod_data::forms())
        for(const unsigned member:family) {
            if(list.size()>=capacity-1)break;
            const uint32_t at=records_copy+slot++*record_size;
            const auto bytes=record(family,member);
            for(unsigned b=0;b<record_size;++b)if(read(ram,at+b,1)!=bytes[b])write8(ram,at+b,bytes[b]);
            list.push_back(uint16_t(member));pointers.push_back(at);
        }
    list.push_back(0xFFFF);
    for(size_t i=0;i<list.size();++i)if(read(ram,list_copy+uint32_t(i)*2,2)!=list[i])write16(ram,list_copy+uint32_t(i)*2,list[i]);
    for(size_t i=0;i<pointers.size();++i)if(read(ram,pointers_copy+uint32_t(i)*4,4)!=pointers[i])write32(ram,pointers_copy+uint32_t(i)*4,pointers[i]);
}
}
