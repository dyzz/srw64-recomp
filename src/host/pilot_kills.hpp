#pragma once
// A pilot's kills (record +0x14, u16) on the two pilot pages the original draws: the map's
// (801E6C5C) and, with the original intermission screens chosen, the intermission's
// (801D2480 → 801C936C). The original shows kills only in the map's 部隊表, as
// 撃墜数表示: "%d" then text 0x464 機 (801EC3A0). Both pages leave the left of their top
// box empty (the intermission's LRZ hint and the map's A:サブパイロット start further
// right), so they go there (user, 2026-10-19). Natively drawn text shows the reading
// language's label and the number (ui_text::mark_label); the Japanese original screen, in
// the ROM's font, which has no such label, shows the number and 機 as the 部隊表 does.
// Only for our pilots: an enemy's +0x14 counts its dummies (battle-formulas.md §7).
#include "guest_memory.hpp"
#include "recomp.h"
#include "funcs.h"
#include "ui_text.hpp"
#include <string>

namespace srw64::pilot_kills {
inline constexpr uint32_t kills=0x14,text_buffer=0x801614F0;
inline constexpr uint16_t text_kills_unit=0x464;   // 機
inline void call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),
                 uint32_t a0=0,uint32_t a1=0,uint32_t a2=0,uint32_t a3=0,uint32_t stack0=0,uint32_t* result=nullptr) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);
    c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);c.r7=int32_t(a3);
    guest::write32(ram,uint32_t(c.r29)+0x10,stack0);
    function(ram,&c);
    if(result)*result=uint32_t(c.r2);
}
// The intermission's original pages keep their handles in D_801DD568 (97 bytes), counted
// by D_801DD148; one appended there is freed with the page's own.
inline int8_t intermission_handle(uint8_t* ram,recomp_context* ctx) {
    constexpr uint32_t handles=0x801DD568,count=0x801DD148;constexpr unsigned room=0x61;
    const unsigned n=guest::read(ram,count,2);
    if(n>=room)return -1;
    uint32_t handle=0;call(ram,ctx,resident_func_80083F70,0,0,0,0,0,&handle);
    guest::write8(ram,handles+n,uint8_t(handle));guest::write16(ram,count,uint16_t(n+1));
    return int8_t(handle);
}
// Draws the pilot's kills from (x, y), the label squeezed into `width`. `handle()` gives a
// new text-engine handle (or -1), which its caller keeps so the page frees it.
template<class NewHandle>
inline void draw(uint8_t* ram,recomp_context* ctx,uint32_t pilot,unsigned x,unsigned y,int width,NewHandle handle) {
    if(!guest::valid(pilot,0x4C))return;
    const std::string digits=std::to_string(guest::read(ram,pilot+kills,2));
    for(size_t i=0;i<=digits.size();++i)guest::write8(ram,text_buffer+0x100+uint32_t(i),i<digits.size()?uint8_t(digits[i]):0);
    call(ram,ctx,resident_func_800B4950,text_buffer,text_buffer+0x100);
    const int8_t number=handle();
    if(number<0)return;
    call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(number)),x,y,0);
    if(ui_text::native_numbers()){ui_text::mark_label(unsigned(number),int(x),int(y),digits,"ability_kills",width);return;}
    // The ROM's digits are 8 wide.
    if(const int8_t unit=handle();unit>=0)
        call(ram,ctx,resident_func_8008D0E8,uint32_t(int32_t(unit)),text_kills_unit,x+unsigned(digits.size())*8,y,0);
}
}
