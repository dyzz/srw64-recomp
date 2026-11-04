#pragma once
#include "guest_memory.hpp"

// Whether the game still runs the インターミッション: mode 4 after a clear, 0x12-0x15 after a
// load from the title (quick_load.cpp title_screen). The native pages that stand over its
// screens close when it does not, as when 返回标题画面 (F10, the Z+START reset) leaves from
// one of them: that keeps the intermission's screen numbers, so the pages' own test (a new
// screen asked for) never fires.
namespace srw64::intermission_mode {
inline constexpr uint32_t game_mode=0x8015DA02;
inline bool running(const uint8_t* ram) {
    const unsigned m=guest::read(ram,game_mode,1);
    return m==4 || (m>=0x12 && m<=0x15);
}
}
