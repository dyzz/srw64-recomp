#pragma once
// The intermission menu (801CDFB0 builds it, 801CE19C steps it; docs/native/native-intermission-menu.md)
// with a row of ours before its last: 返回标题画面 (quick_load.hpp, asked first), then
// 次のマップへ. Nine items and the two-item menu after a （前） scene alike, made in the
// original's data and flow:
// - the labels: after the build ours is made with the text engine's own calls where
//   次のマップへ (text 0xFD5) was, which moves a row down, and the nine-item menu's chapter
//   line (y 193: its labels and the title's text body) with it;
// - the cursor: 801C4A20 is given one row more (generate_cpu.py: its last index 8 -> 9, 1 -> 2),
//   and the nine-item menu reads its tenth row as the ninth for the next screen (cursor + 1),
//   so 次のマップへ still leads to screen 9; A on our row is taken before the step;
// - the frames and panels: menu_widen.cpp (one row taller, the chapter box 16 lower).
// The native page (intermission_page.cpp) lists the same rows on the same cursor.
#include "guest_memory.hpp"
#include "title_row.hpp"
#include <cstdint>

namespace srw64::intermission_row {
constexpr uint32_t cursor=0x801DD548,mode=0x801DECD8,built=0x801DEC50,pressed=0x80178A08,transition=0x8015E9C5;
constexpr uint32_t mode_full=0,mode_two=10;   // mode 1 is the のりかえ window
constexpr uint16_t text_next=0xFD5,button_a=0x8000;
constexpr int label_x=34,first_y=41,row=16,chapter_y=193;
constexpr uint32_t bodies=0x800FBAB0,body_size=0x218,body_count=2;   // the text engine's bodies: +2 status, +4 x, +8 y
// Our row: the original's last index, which its last row moves past.
inline unsigned title_index(bool two){return two?1:8;}
inline unsigned rows(bool two){return two?3:10;}

// Game thread, after the original build: 次のマップへ (and the chapter line) a row down;
// returns where ours goes, the place 次のマップへ had.
inline int make_room(uint8_t* ram) {
    const bool two=guest::read(ram,mode,4)==mode_two;
    const int y=first_y+row*int(title_index(two));
    title_row::move_labels(ram,text_next,label_x,y,row);
    if(!two) {
        title_row::move_labels(ram,-1,-1,chapter_y,row);
        // The chapter's title is a text body (80083F24, 8008CD8C at (82, 193)), not a label.
        for(unsigned n=0;n<body_count;++n) {
            const uint32_t body=bodies+n*body_size;
            const unsigned status=guest::read(ram,body+2,1);
            if(status>=1 && status<=3 && int32_t(guest::read(ram,body+8,4))==chapter_y)guest::write32(ram,body+8,uint32_t(chapter_y+row));
        }
    }
    return y;
}
// Game thread, before the step: A on our row, taken (true) for the title question. The
// caller puts the buttons back after the step.
inline bool take_a(uint8_t* ram) {
    if(!guest::read(ram,built,4) || int8_t(guest::read(ram,transition,1))!=-1)return false;
    const uint32_t m=guest::read(ram,mode,4);
    if(m!=mode_full && m!=mode_two)return false;
    const uint16_t buttons=uint16_t(guest::read(ram,pressed,2));
    if(!(buttons&button_a) || guest::read(ram,cursor,2)!=title_index(m==mode_two))return false;
    guest::write16(ram,pressed,uint16_t(buttons&~button_a));
    title_row::choose();
    return true;
}
}
