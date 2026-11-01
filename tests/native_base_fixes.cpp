#include "base_fixes.hpp"
#include <cassert>
#include <vector>
int main() {
    using namespace srw64::base_fixes;
    // BUG05: only the player's own 100 pilot records keep a Wing pilot's kills;
    // the same field on sides 1 and 2 is the dummy count.
    assert(player_pilot(pilot_table));
    assert(player_pilot(pilot_table + 99 * 0x4C));
    assert(!player_pilot(pilot_table + pilot_side_stride));
    assert(!player_pilot(pilot_table + 2 * pilot_side_stride));
    assert(!player_pilot(pilot_table - 1));
    assert(!player_pilot(0));

    using namespace srw64::guest;
    std::vector<uint8_t> ram(0x800000);
    auto* p=ram.data();
    constexpr uint32_t owner=0x802F0000,pc=0x8019C000;
    constexpr uint16_t tail[]={0x3D6B,165,216,0,0x3D5A,201,0,270,273,
        0x3D5A,7,0,8,500,0x3D4B,56,0xFFFF};
    write32(p,owner+0x1C,pc+2);
    for(unsigned i=0;i<std::size(tail);++i)write16(p,pc+i*2,tail[i]);
    write8(p,daitarn_scene,55);
    for(unsigned hero=0;hero<4;++hero) {
        write16(p,daitarn_engine+0x99E,uint16_t(0x3DD1+hero));
        assert(daitarn_join_event(p,owner)==(hero<2));
    }
    write16(p,daitarn_engine+0x99E,0x3DD1);
    auto baseline=ram;
    // No other scene or modified script may receive the join.
    for(unsigned scene=0;scene<144;++scene) {
        write8(p,daitarn_scene,uint8_t(scene));
        assert(daitarn_join_event(p,owner)==(scene==55));
    }
    ram=baseline;p=ram.data();
    write16(p,pc+8,0x3D38);assert(!daitarn_join_event(p,owner));
    ram=baseline;p=ram.data();
    write32(p,owner+0x1C,0);assert(!daitarn_join_event(p,owner));
    assert(!daitarn_join_event(p,0));
    ram=baseline;p=ram.data();

    unsigned registrations=0,hides=0;
    auto reg=[&] {
        ++registrations;
        write8(p,pilot_table,1);write16(p,pilot_table+2,165);
        write8(p,daitarn_units,1);write16(p,daitarn_units+2,216);
    };
    auto hide=[&] {++hides;write8(p,pilot_table,0x81);write8(p,daitarn_units+0xC,0xC0);};
    assert(!repair_daitarn(p,{},reg,hide));assert(registrations==0);
    assert(repair_daitarn(p,{true,false},reg,hide));assert(registrations==1 && hides==0);
    // Repeated calls must not touch levels, weapon upgrades, parts or flags.
    write8(p,pilot_table+5,42);write8(p,daitarn_units+0x23,7);
    auto joined=ram;
    assert(!repair_daitarn(p,{true,true},reg,hide));
    assert(ram==joined && registrations==1 && hides==0);
    for(unsigned form=216;form<=218;++form) {
        ram=baseline;p=ram.data();write8(p,daitarn_units,1);write16(p,daitarn_units+2,uint16_t(form));
        assert(!daitarn_absent(p));assert(!repair_daitarn(p,{true,false},reg,hide));
    }
    ram=baseline;p=ram.data();write8(p,pilot_table,0x81);write16(p,pilot_table+2,165);
    assert(!daitarn_absent(p)); // a hidden pilot alone is also preserved

    ram=baseline;p=ram.data();
    write8(p,0x8015E9C5,0xFF); // idle intermission
    write16(p,daitarn_vars+2,uint16_t(1u<<4)); // var10 = OZ
    constexpr unsigned after[]={56,57,59,60,61,91,92,93,94,95,96,97,98,99,100,101,102,
        103,104,105,106,123,124,134,137,138,139};
    for(unsigned scene=0;scene<144;++scene) {
        bool expected=false;for(auto n:after)expected|=n==scene;
        write8(p,daitarn_scene,uint8_t(scene));
        const auto decision=daitarn_old_save(p);
        assert(decision.eligible==expected);
        assert(decision.hidden==(scene==103 || scene==104 || scene==134));
    }
    write8(p,daitarn_scene,56);
    for(unsigned hero=0;hero<4;++hero) {
        write16(p,daitarn_engine+0x99E,uint16_t(0x3DD1+hero));
        assert(daitarn_old_save(p).eligible==(hero<2));
    }
    write16(p,daitarn_engine+0x99E,0x3DD2);
    write16(p,daitarn_vars+2,0);assert(!daitarn_old_save(p).eligible); // IA/CP
    write16(p,daitarn_vars+2,uint16_t(1u<<4));
    write8(p,0x8015E9C5,0);assert(!daitarn_old_save(p).eligible); // transition not idle
    write8(p,0x8015E9C5,0xFF);
    for(unsigned link=109;link<=122;++link) {
        write8(p,daitarn_scene,uint8_t(link));
        write8(p,0x8010F5F2,55);assert(!daitarn_old_save(p).eligible);
        write8(p,0x8010F5F2,56);assert(daitarn_old_save(p).eligible && !daitarn_old_save(p).hidden);
        write8(p,0x8010F5F2,103);assert(daitarn_old_save(p).eligible && daitarn_old_save(p).hidden);
    }
    ram=baseline;p=ram.data();
    assert(repair_daitarn(p,{true,true},reg,hide));assert(hides==1);
    assert(read(p,pilot_table,1)==0x81 && read(p,daitarn_units+0xC,1)==0xC0);
    return 0;
}
