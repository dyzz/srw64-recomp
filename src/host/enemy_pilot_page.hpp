#pragma once
// The pilot page on the map (801E6C5C, the tactical overlay). After the original has drawn
// it, the spirit slots are drawn again for every pilot, ours and the enemy's (user,
// 2026-10-17): a learned spirit by name, greyed when SP is short (the original's rule); one
// the pilot learns at a later level by name in dark grey with that level beside it, small
// and yellow as "Lv12" (ui_text::mark_small); a slot its learning table leaves empty ---. For an enemy
// the original prints ????? in all six slots and ---/--- for SP, though its record holds
// both as ours does (+0x0A count, +0x0B ids, +0x16 / +0x18 SP): SP as ours is printed.
//
// And every enemy counts as fought from the start (user, 2026-10-17): the original shows an
// enemy's numbers (the info window, the pre-battle page) and lets A open its 能力 menu
// (801CBDE8) only once its lead pilot's +0 has 0x40, which the battle sets (801D4E6C).
#include "guest_memory.hpp"
#include "recomp.h"
#include "funcs.h"
#include "spirit_learning.hpp"
#include "ui_text.hpp"
#include "native_dialogue.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace srw64::enemy_pilot_page {
// The page's text handles (each a text engine slot; 801E6C5C frees the whole array at
// 802271DD.. before it draws): SP, then the six spirits. The spirits' places are (x, y)
// byte pairs; SP stands at (0xF4, 0x46). The roster (side x 0x258 + slot x 0x14) leads to
// the unit, whose +0x38 are its pilots. SP costs per spirit id at 80217F70; the spirit
// names are text 0x3C9 + id; the original's SP format is "%3d/%3d".
inline constexpr uint32_t handle_sp=0x802271E2,handle_spirits=0x802271ED,places=0x8021820C,
    roster=0x8015E10C,costs=0x80217F70,sp_format=0x8021ECA0,text_buffer=0x801614F0;
inline constexpr uint32_t text_spirits=0x3C9,unknown_format=0x8021ECB0,none_format=0x8021EBEC;   // "?????", "---"
// The level beside a later spirit: the page's own handle array (802271DD.., 40 bytes, freed
// whole each time a page of the tactical overlay is drawn, 801E61C4 / 801E642C) has its
// last few unused: four of them hold the levels of the second row's slots, two the first's.
inline constexpr uint32_t handle_levels=0x802271FD;
// Palettes of the text engine (ui_text.cpp kPaletteHandles): 0 white, 1 the grey of a
// spirit short of SP, 2 the dark grey of resource 4, 3 the yellow the levels take when the
// ROM's font draws them.
inline constexpr unsigned palette_dark=2,palette_level=3;
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
// About how wide a spirit's name is drawn, in N64 pixels: the native font's 12.5 a full-width
// glyph and about half that a Latin one, the ROM's 14 and 8.
inline unsigned name_width(const std::string& text,bool native) {
    unsigned wide=0,narrow=0;
    for(size_t i=0;i<text.size();) {
        const uint8_t c=uint8_t(text[i]);
        const size_t n=c<0x80?1:c<0xE0?2:c<0xF0?3:4;
        (c<0x80?narrow:wide)++;i+=n;
    }
    return native?unsigned(wide*12.5+narrow*6.5):wide*14+narrow*8;
}
// Prints `text` (ASCII) into the engine's sprintf buffer and draws it with a new handle.
inline int8_t print(uint8_t* ram,recomp_context* ctx,uint32_t at,const std::string& text,unsigned x,unsigned y,unsigned palette) {
    for(size_t i=0;i<=text.size();++i)guest::write8(ram,text_buffer+0x100+uint32_t(i),i<text.size()?uint8_t(text[i]):0);
    call(ram,ctx,resident_func_800B4950,text_buffer,text_buffer+0x100);
    const int8_t handle=renew(ram,ctx,at);
    if(handle>=0)call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),x,y,palette);
    return handle;
}
// After 801E6C5C(crew, side, slot) has drawn the page.
inline void reveal(uint8_t* ram,recomp_context* ctx,unsigned crew,unsigned side,unsigned slot) {
    using guest::read;
    if(side>2 || slot>=30 || crew>=7)return;
    const uint32_t unit=read(ram,roster+side*0x258+slot*0x14,4);
    if(!guest::valid(unit,0x54))return;
    const uint32_t pilot=read(ram,unit+0x38+crew*4,4);
    if(!guest::valid(pilot,0x4C))return;
    const unsigned sp=read(ram,pilot+0x16,2),count=std::min(6u,read(ram,pilot+0x0A,1));
    const auto table=spirit_learning::table(read(ram,pilot+0x02,2));
    const bool native=ui_text::native_numbers();
    ui_text::clear_small();
    for(unsigned n=0;n<6;++n) {
        const uint32_t at=handle_spirits+n;
        const unsigned x=read(ram,places+2*n,1),y=read(ram,places+2*n+1,1);
        if(n<count) {
            if(side==0)continue;   // ours: the original's own
            const unsigned id=read(ram,pilot+0x0B+n,1);
            const int8_t handle=renew(ram,ctx,at);
            if(handle>=0)call(ram,ctx,resident_func_8008D0E8,uint32_t(int32_t(handle)),text_spirits+id,x,y,id<32 && sp<read(ram,costs+id,1));
            continue;
        }
        if(n>=table.size()) {   // nothing in the table: ---
            call(ram,ctx,resident_func_800B4950,text_buffer,none_format);
            if(const int8_t handle=renew(ram,ctx,at);handle>=0)call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),x,y,0);
            continue;
        }
        // Learned later: the name in dark grey, its level at the slot's right, small (native
        // text) or in the ROM's font (Japanese original mode). Slots are 56 apart.
        const auto& entry=table[n];
        if(const int8_t handle=renew(ram,ctx,at);handle>=0)
            call(ram,ctx,resident_func_8008D0E8,uint32_t(int32_t(handle)),text_spirits+entry.id,x,y,palette_dark);
        // Right after the name, and never past the slot (the name narrows to what is left).
        // The engine prints the digits; the native text shows them small as "Lv12". The
        // ROM's font (Japanese original mode) has no letters and no room for more.
        const std::string level=std::to_string(entry.level),shown="Lv"+level;
        const unsigned digits=native?unsigned(shown.size())*5:unsigned(level.size())*8,room=56-digits-2;
        const unsigned lx=x+std::min(room,name_width(dialogue::ui_text(ram,uint16_t(text_spirits+entry.id)),native)+2);
        if(n>=2)if(const int8_t handle=print(ram,ctx,handle_levels+(n-2),level,lx,y,palette_level);handle>=0 && native)
            ui_text::mark_small(unsigned(handle),int(lx),int(y),level,shown);
        if(n<2)if(const int8_t handle=print(ram,ctx,handle_levels+4+n,level,lx,y,palette_level);handle>=0 && native)
            ui_text::mark_small(unsigned(handle),int(lx),int(y),level,shown);
    }
    if(side==0)return;
    call(ram,ctx,resident_func_800B4950,text_buffer,sp_format,sp,read(ram,pilot+0x18,2));
    if(const int8_t handle=renew(ram,ctx,handle_sp);handle>=0)call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),0xF4,0x46,0);
}
}
