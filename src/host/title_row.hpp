#pragma once
// The 返回标题画面 row the host adds to two original menus, the map's blank-tile menu
// (map_menu.hpp) and the intermission menu (intermission_row.hpp): a text engine label
// the menu makes with its own, and a choice the frontend answers with the title question
// (quick_load.hpp).
#include "guest_memory.hpp"
#include <atomic>
#include <cstdint>

namespace srw64::title_row {
// The label's record number: no ROM record has it; the text layer shows the UI string
// title_return_open for it (dialogue::label_text).
constexpr uint16_t record=0xF001;
// タイトルに戻る in the ROM font, for the Japanese glyphs the engine itself draws.
constexpr uint16_t glyphs[]={0x84,0x76,0x88,0x9D,0x3A,0x4E9,0x4D};
constexpr uint32_t labels=0x8015CB00,label_size=0x34,label_count=60;   // the text engine's single-line labels: +0 record, +2 kind, +4 x, +8 y, +0xC glyphs

// Game thread: the live labels on line y (and at x, unless x < 0; of record `of` unless it
// is < 0) moved down by dy.
inline void move_labels(uint8_t* ram,int of,int x,int y,int dy) {
    for(unsigned n=0;n<label_count;++n) {
        const uint32_t slot=labels+n*label_size;
        const unsigned kind=guest::read(ram,slot+2,1);
        if(kind<1 || kind>3 || int32_t(guest::read(ram,slot+8,4))!=y)continue;
        if((of>=0 && guest::read(ram,slot,2)!=unsigned(of)) || (x>=0 && int32_t(guest::read(ram,slot+4,4))!=x))continue;
        guest::write32(ram,slot+8,uint32_t(y+dy));
    }
}

inline std::atomic_bool chosen{};
// Game thread: the row was chosen; the frontend asks.
inline void choose(){chosen=true;}
inline bool take_chosen(){return chosen.exchange(false);}
}
