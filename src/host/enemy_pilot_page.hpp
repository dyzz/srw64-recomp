#pragma once
// The pilot page on the map (801E6C5C, the tactical overlay): for an enemy the original
// prints ????? in all six spirit slots and ---/--- for SP, although the enemy's record
// holds its learned spirits (+0x0A count, +0x0B ids) and SP (+0x16 / +0x18) as ours do.
// After the original has drawn the page, an enemy's slots are drawn again from its record:
// a learned spirit by name, greyed when SP is short (as the original does for ours), an
// empty slot left empty, and SP as the original prints ours (user, 2026-10-17).
#include "guest_memory.hpp"
#include "recomp.h"
#include "funcs.h"
#include <algorithm>

namespace srw64::enemy_pilot_page {
// The page's text handles (each a text engine slot; 801E6C5C frees the whole array at
// 802271DD.. before it draws): SP, then the six spirits. The spirits' places are (x, y)
// byte pairs; SP stands at (0xF4, 0x46). The roster (side x 0x258 + slot x 0x14) leads to
// the unit, whose +0x38 are its pilots. SP costs per spirit id at 80217F70; the spirit
// names are text 0x3C9 + id; the original's SP format is "%3d/%3d".
inline constexpr uint32_t handle_sp=0x802271E2,handle_spirits=0x802271ED,places=0x8021820C,
    roster=0x8015E10C,costs=0x80217F70,sp_format=0x8021ECA0,text_buffer=0x801614F0;
inline constexpr uint32_t text_spirits=0x3C9;

inline void call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),
                 uint32_t a0=0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0,uint32_t stack0=0,uint32_t* result=nullptr) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);
    c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);c.r7=int32_t(a3);
    guest::write32(ram,uint32_t(c.r29)+0x10,stack0);
    function(ram,&c);
    if(result)*result=uint32_t(c.r2);
}
// A new handle in place of the one at `at` (freed first).
inline int8_t renew(uint8_t* ram,recomp_context* ctx,uint32_t at) {
    if(const int8_t old=int8_t(guest::read(ram,at,1));old>=0)call(ram,ctx,resident_func_8008D460,uint32_t(int32_t(old)));
    uint32_t handle=0;call(ram,ctx,resident_func_80083F70,0,0,0,0,0,&handle);
    guest::write8(ram,at,uint8_t(handle));
    return int8_t(handle);
}
// After 801E6C5C(crew, side, slot) has drawn the page.
inline void reveal(uint8_t* ram,recomp_context* ctx,unsigned crew,unsigned side,unsigned slot) {
    using guest::read;
    if(side==0 || side>2 || slot>=30 || crew>=7)return;
    const uint32_t unit=read(ram,roster+side*0x258+slot*0x14,4);
    if(!guest::valid(unit,0x54))return;
    const uint32_t pilot=read(ram,unit+0x38+crew*4,4);
    if(!guest::valid(pilot,0x4C))return;
    const unsigned sp=read(ram,pilot+0x16,2),count=std::min(6u,read(ram,pilot+0x0A,1));
    for(unsigned n=0;n<6;++n) {
        const uint32_t at=handle_spirits+n;
        const unsigned x=read(ram,places+2*n,1),y=read(ram,places+2*n+1,1);
        if(n>=count) {   // not learned: nothing
            if(const int8_t old=int8_t(read(ram,at,1));old>=0)call(ram,ctx,resident_func_8008D460,uint32_t(int32_t(old)));
            guest::write8(ram,at,0xFF);
            continue;
        }
        const unsigned id=read(ram,pilot+0x0B+n,1);
        const bool short_of_sp=id<32 && sp<read(ram,costs+id,1);
        const int8_t handle=renew(ram,ctx,at);
        if(handle>=0)call(ram,ctx,resident_func_8008D0E8,uint32_t(int32_t(handle)),text_spirits+id,x,y,short_of_sp);
    }
    call(ram,ctx,resident_func_800B4950,text_buffer,sp_format,sp,read(ram,pilot+0x18,2));
    if(const int8_t handle=renew(ram,ctx,handle_sp);handle>=0)call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),0xF4,0x46,0);
}
}
