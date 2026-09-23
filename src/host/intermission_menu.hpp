#pragma once
#include <cstdint>
#include <string>

// The parts of the native インターミッション menu that need no game: which menu the
// cleared scene gets, and what the game may read from the pad while the page is up.
namespace srw64::intermission_page {
// D_801DC6D4: after these scenes the menu is only データセーブ / 次のマップへ.
inline bool restricted_scene(const uint8_t* list,unsigned count,uint8_t scene) {
    for(unsigned n=0;n<count;++n)if(list[n]==scene)return true;
    return false;
}
// Funds (D_8010F5F4) as the intermission's %8d shows them; the native pages let the
// player type a new figure straight into that field. Returns false for bad text.
inline bool parse_funds(const std::string& action,uint32_t& value) {
    if(action.size()<7 || action.compare(0,6,"funds:")!=0 || action.size()>6+8)return false;
    value=0;
    for(size_t i=6;i<action.size();++i){if(action[i]<'0' || action[i]>'9')return false;value=value*10+unsigned(action[i]-'0');}
    return true;
}
inline constexpr uint32_t funds_address=0x8010F5F4;
// The screen dispatcher (801D8EA0) resets the game while both of these are held.
inline constexpr uint16_t soft_reset=0x3000;
// Buttons held when the page closed stay hidden until released. While it owns the
// pad only the soft reset pair gets through.
inline uint16_t filter_input(uint16_t buttons,uint16_t& held,bool owns) {
    held&=buttons;
    if(!owns)return buttons&~held;
    held|=buttons;
    return (buttons&soft_reset)==soft_reset?soft_reset:0;
}
}
