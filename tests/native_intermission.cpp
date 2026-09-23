// Component checks of the native インターミッション menu: no game, no window.
#include "intermission_menu.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace srw64::intermission_page;
int main() {
    // D_801DC6D4 as in the ROM: thirteen （前） scenes and アクシズの攻防（中）.
    const uint8_t list[]={0x26,0x30,0x44,0x49,0x4F,0x51,0x53,0x56,0x58,0x5D,0x5F,0x60,0x68,0x84};
    for(auto scene:list)assert(restricted_scene(list,sizeof list,scene));
    for(unsigned scene:{0u,1u,0x25u,0x27u,0x57u,0x83u,0x85u,0xFFu})assert(!restricted_scene(list,sizeof list,uint8_t(scene)));
    assert(!restricted_scene(list,0,0x26));

    uint16_t held=0;
    // The page owns the pad: nothing gets through, except the soft reset pair together.
    assert(filter_input(0x8000,held,true)==0);
    assert(filter_input(0x1000,held,true)==0);
    assert(filter_input(0x3000,held,true)==0x3000);
    assert(filter_input(0xB000,held,true)==0x3000);
    // Closed with A still down: A stays hidden until released, other buttons work.
    held=0;filter_input(0x8000,held,true);
    assert(filter_input(0x8000,held,false)==0);
    assert(filter_input(0x8400,held,false)==0x0400);
    assert(filter_input(0,held,false)==0);
    assert(filter_input(0x8000,held,false)==0x8000);
    uint32_t value=0;
    assert(parse_funds("funds:0",value) && value==0);
    assert(parse_funds("funds:14500",value) && value==14500);
    assert(parse_funds("funds:99999999",value) && value==99999999);
    assert(!parse_funds("funds:",value) && !parse_funds("funds:123456789",value) && !parse_funds("funds:1a",value) && !parse_funds("move:1",value));
    std::puts("native intermission checks passed");
}
