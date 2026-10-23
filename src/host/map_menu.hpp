#pragma once
// The tactical map's blank-tile menu (state 0x16: フェイズ終了, 部隊表, ..., 中断) with a
// row of ours before the original's last: 返回标题画面 (quick_load.hpp, asked first), then 中断.
// The menu is the resident window table D_800C8988's entry 0: frame scene 1198, dark panel
// 8009CDF0, labels from the list at 800C7608 (records 520-526 at (8, 8 + 16 k)), all three
// its own. 8009D684(entry, x, y) makes the labels whenever the window is built (801D13C0,
// and again on the way back from a sub-screen, which clears them); for entry 0 ours is made
// there too, with the text engine's own calls, and 中断's moved a row down. The frame and
// the panel are always drawn a row taller (menu_widen.cpp).
// 801D13C0 sets the row count at 80172ED4 to seven; it is raised to eight, so the original's
// cursor reaches the eighth row by itself, and A goes through 801D17AC(index) to a
// seven-entry table: index 6 is taken here and index 7 goes to the original as 6.
#include "guest_memory.hpp"
#include "title_row.hpp"
#include <atomic>
#include <cstdint>

namespace srw64::map_menu {
constexpr uint32_t count=0x80172ED4;
constexpr unsigned original_rows=7,row_pitch=16;
constexpr unsigned our_row=original_rows-1;   // the original's last row (中断) comes after ours
constexpr uint16_t record_suspend=526;
constexpr int label_x=8,label_y=8;            // where entry 0's list puts its labels

// Game thread, after the original's 801D13C0: one row more for the cursor to reach.
inline void opened(uint8_t* ram) {
    if(guest::read(ram,count,1)!=original_rows)return;
    guest::write8(ram,count,uint8_t(original_rows+1));
}
// Game thread, in place of 801D17AC(index): our row is the question, the frontend's; the
// menu stays open under it, so a No leaves the player where they were. The row after it
// is the original's last.
inline bool choose(uint64_t& index) {
    if(index==our_row+1){index=our_row;return false;}
    if(index!=our_row)return false;
    title_row::choose();
    return true;
}
}
