#pragma once
// The tactical map's blank-tile menu (state 0x16: フェイズ終了, 部隊表, ..., 中断) with a
// row of ours before the original's last: 返回标题画面 (quick_load.hpp, asked first), then 中断.
// The original opens it in 801D13C0 (seven rows, a 16-pixel pitch from the window's y + 8,
// the cursor bar placed by 801CA6C0), moves the cursor in its sub-state 0 against the
// count at 80172ED4, and dispatches A through 801D17AC(index) to a seven-entry table.
// The count is raised to eight once the menu is open, so the original's cursor reaches
// the eighth row by itself; index 6 is taken here and index 7 goes to the original as 6.
// 中断's label (a text engine slot) is moved a row down and ours drawn in its place.
#include "guest_memory.hpp"
#include "ui_text.hpp"
#include "json/json.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace srw64::map_menu {
constexpr uint32_t map_state=0x80172EB0,sub_state=0x80172EB2,count=0x80172ED4,cursor=0x80172ED6;
constexpr uint32_t window_x=0x80101DAC,window_y=0x80101DB0;   // floats 801D13C0 stores
constexpr unsigned original_rows=7,row_pitch=16,first_row_y=8;
constexpr unsigned our_row=original_rows-1;   // the original's last row (中断) comes after ours
constexpr uint32_t labels=0x8015CB00,label_size=0x34,label_count=60;   // the text engine's single-line labels: +4 x, +8 y
inline std::atomic_bool chosen{};
inline std::atomic<float> at_x{},at_y{};
inline float read_f32(const uint8_t* ram,uint32_t p){const uint32_t bits=guest::read(ram,p,4);float f;std::memcpy(&f,&bits,4);return f;}

// Game thread, after the original's 801D13C0: one row more for the cursor to reach.
inline void opened(uint8_t* ram) {
    if(guest::read(ram,count,1)!=original_rows)return;
    guest::write8(ram,count,uint8_t(original_rows+1));
    at_x=read_f32(ram,window_x);at_y=read_f32(ram,window_y);
}
// Game thread, in place of 801D17AC(index): our row is the question, the frontend's; the
// menu stays open under it, so a No leaves the player where they were. The row after it
// is the original's last.
inline bool choose(uint64_t& index) {
    if(index==our_row+1){index=our_row;return false;}
    if(index!=our_row)return false;
    chosen=true;
    return true;
}
inline bool take_chosen(){return chosen.exchange(false);}
// The menu is open with its eighth row (the frame, panel and text are drawn a row taller).
inline std::atomic_bool is_tall{};
inline bool tall(){return is_tall.load();}
// The row's text, at the original labels' x in the seventh row.
constexpr int label_x=8,label_y=8,label_width=68;   // the labels sit at 128, 56 + 16 k for a window at (120, 64): 80101DB0 holds y - 16
// Game thread, every frame boundary: 中断's label a row down and ours in its place while the
// menu is open with it.
inline void frame(uint8_t* ram) {
    const unsigned mode=guest::read(ram,0x8015DA02,1);
    const bool tactical=mode==3 || mode==0xB || mode==0x11 || mode==0x16 || mode==0x22;
    const bool open=tactical && guest::read(ram,map_state,1)==0x16 && guest::read(ram,count,1)==original_rows+1;
    if(open) {
        const int x=int(at_x.load())+label_x,y=int(at_y.load())+label_y+int(row_pitch*our_row);
        for(unsigned n=0;n<label_count;++n) {
            const uint32_t slot=labels+n*label_size;
            const unsigned kind=guest::read(ram,slot+2,1);
            if(kind>=1 && kind<=3 && int32_t(guest::read(ram,slot+4,4))==x && int32_t(guest::read(ram,slot+8,4))==y)
                guest::write32(ram,slot+8,uint32_t(y+int(row_pitch)));
        }
    }
    if(open==is_tall.load())return;
    is_tall=open;
    if(open)ui_text::set_extra("map-menu-title",int(at_x.load())+label_x,int(at_y.load())+label_y+int(row_pitch*our_row),"title_return_open",label_width);
    else ui_text::clear_extra("map-menu-title");
}
// Any thread: the row to draw, in the original's 320x240 coordinates.
inline nlohmann::json state(const uint8_t* ram) {
    if(!ram)return {{"visible",false}};
    // The map's RAM keeps its values after the map is left (the title after a return), so
    // the game must be on the tactical map too (8015DA02: 3 / 0xB / 0x11 / 0x16 / 0x22).
    const unsigned mode=guest::read(ram,0x8015DA02,1);
    const bool tactical=mode==3 || mode==0xB || mode==0x11 || mode==0x16 || mode==0x22;
    const unsigned state=guest::read(ram,map_state,1),sub=guest::read(ram,sub_state,1),rows=guest::read(ram,count,1);
    const bool visible=tactical && state==0x16 && sub==0 && rows==original_rows+1;
    return {{"visible",visible},{"x",at_x.load()},{"y",at_y.load()+first_row_y+row_pitch*our_row},
            {"selected",visible && guest::read(ram,cursor,1)==our_row}};
}
}
