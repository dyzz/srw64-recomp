#pragma once
// The pilot page on the map (801E6C5C, the tactical overlay): for an enemy the original
// prints ????? in all six spirit slots and ---/--- for SP, although the enemy's record
// holds its learned spirits (+0x0A count, +0x0B ids) and SP (+0x16 / +0x18) as ours do.
// After the original has drawn the page, an enemy's slots are drawn again from its record:
// as ours read (user, 2026-10-17): a learned spirit by name, greyed when SP is short, one it
// learns at a later level ?????, a slot its learning table leaves empty ---, and SP as the
// original prints ours.
//
// And every enemy counts as fought from the start (user, 2026-10-17): the original shows an
// enemy's numbers (the info window, the pre-battle page) and lets A open its 能力 menu
// (801CBDE8) only once its lead pilot's +0 has 0x40, which the battle sets (801D4E6C).
#include "guest_memory.hpp"
#include "recomp.h"
#include "funcs.h"
#include "librecomp/game.hpp"
#include <algorithm>

namespace srw64::enemy_pilot_page {
// The page's text handles (each a text engine slot; 801E6C5C frees the whole array at
// 802271DD.. before it draws): SP, then the six spirits. The spirits' places are (x, y)
// byte pairs; SP stands at (0xF4, 0x46). The roster (side x 0x258 + slot x 0x14) leads to
// the unit, whose +0x38 are its pilots. SP costs per spirit id at 80217F70; the spirit
// names are text 0x3C9 + id; the original's SP format is "%3d/%3d".
inline constexpr uint32_t handle_sp=0x802271E2,handle_spirits=0x802271ED,places=0x8021820C,
    roster=0x8015E10C,costs=0x80217F70,sp_format=0x8021ECA0,text_buffer=0x801614F0;
inline constexpr uint32_t text_spirits=0x3C9,unknown_format=0x8021ECB0,none_format=0x8021EBEC;   // "?????", "---"
// The learning tables in ROM (library.cpp): actor -> s16 record (800CA6F4, resident at ROM
// 0x550E4), a record six (level, command) pairs at 0x7CFB0; a level 0 or past 99 is no entry.
inline constexpr uint32_t spirits_map_rom=0x800CA6F4-0x80075610,spirits_rom=0x7CFB0;
inline constexpr unsigned mapped_actors=360;
// How many of a pilot's six slots its learning table fills.
inline unsigned table_size(unsigned actor) {
    const auto rom=recomp::get_rom();
    const auto u8=[&](uint32_t at)->unsigned{return at<rom.size()?rom[at]:0;};
    if(actor>=mapped_actors)return 0;
    const int record=int16_t(uint16_t(u8(spirits_map_rom+actor*2)<<8|u8(spirits_map_rom+actor*2+1)));
    if(record<0)return 0;
    unsigned filled=0;
    for(unsigned n=0;n<6;++n) {
        const uint32_t at=spirits_rom+uint32_t(record)*12+n*2;
        if(const unsigned level=u8(at);level && level<100 && u8(at+1)<30)++filled;
    }
    return filled;
}

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
// Game thread, each frame on the map or in a battle: every pilot of every enemy and
// third-party unit on the field is marked as fought (+0 |= 0x40), as a battle would.
inline constexpr uint32_t game_mode=0x8015DA02;
inline void mark_fought(uint8_t* ram) {
    using guest::read;
    const unsigned m=read(ram,game_mode,1);
    if(!(m==2 || m==3 || m==0xB || m==0x11 || m==0x16 || m==0x22))return;
    for(unsigned side=1;side<=2;++side)for(unsigned slot=0;slot<30;++slot) {
        const uint32_t entry=roster-0xC+side*0x258+slot*0x14;
        if(read(ram,entry,1)!=1)continue;   // not on the field
        const uint32_t unit=read(ram,entry+0xC,4);
        if(!guest::valid(unit,0x54))continue;
        for(unsigned crew=0;crew<std::min(7u,read(ram,unit+0x34,1));++crew) {
            const uint32_t pilot=read(ram,unit+0x38+crew*4,4);
            if(guest::valid(pilot,0x4C))if(const unsigned flags=read(ram,pilot,1);!(flags&0x40))guest::write8(ram,pilot,uint8_t(flags|0x40));
        }
    }
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
    const unsigned filled=std::max(count,table_size(read(ram,pilot+0x02,2)));
    for(unsigned n=0;n<6;++n) {
        const uint32_t at=handle_spirits+n;
        const unsigned x=read(ram,places+2*n,1),y=read(ram,places+2*n+1,1);
        if(n>=count) {   // learned later: ?????, never: ---
            call(ram,ctx,resident_func_800B4950,text_buffer,n<filled?unknown_format:none_format);
            if(const int8_t handle=renew(ram,ctx,at);handle>=0)call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),x,y,0);
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
