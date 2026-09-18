#pragma once
#include <cstdint>

// Guest RDRAM access for host-side hooks. The host keeps RDRAM word-swapped, so
// byte p of the guest's view lives at (p & 0x1FFFFFFF) ^ 3. Reads outside KSEG0
// RDRAM return 0; writes assume the caller has checked valid().
namespace srw64::guest {
inline bool valid(uint32_t p, uint32_t size) {
    return (p & 0xE0000000u)==0x80000000u && (p & 0x1FFFFFFFu)<=0x800000u-size;
}
inline uint32_t read(const uint8_t* ram,uint32_t p,unsigned size) {
    if(!valid(p,size))return 0;
    uint32_t value=0;
    for(unsigned i=0;i<size;++i)value=(value<<8)|ram[((p&0x1FFFFFFFu)+i)^3];
    return value;
}
inline void write8(uint8_t* ram,uint32_t p,uint8_t v){ram[(p&0x1FFFFFFF)^3]=v;}
inline void write16(uint8_t* ram,uint32_t p,uint16_t v){write8(ram,p,v>>8);write8(ram,p+1,v&255);}
inline void write32(uint8_t* ram,uint32_t p,uint32_t v){write16(ram,p,v>>16);write16(ram,p+2,v&0xFFFF);}
}
