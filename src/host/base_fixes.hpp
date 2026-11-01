#pragma once
#include "guest_memory.hpp"
#include <array>
#include <cstdint>

// Corrections that are always on, for original defects where the game contradicts
// its own data and no playable behaviour is lost (docs/gameplay/base-fixes.md). Unlike the
// optional rules in rule_fixes.hpp these have no switch: the hook wrappers in
// game_hooks.cpp apply them on every run.
namespace srw64::base_fixes {

// Pilot records: 100 per side of 0x4C bytes, the player's first (800A32EC, 800A84F8).
inline constexpr uint32_t pilot_table=0x80172F40,pilot_side_stride=0x1DB0;
// BUG05. The kill backup for the five W pilots (801614E0) is raised with every
// player kill by 800A4FBC and kept in the save, and 800A5054 copies it into a new
// pilot record so a pilot who left keeps his kills. It never looks at the side,
// and for sides 1 and 2 that same field (+0x14) is the dummy count read by
// 801F6E3C, so a hostile 五飛 gets one dummy per kill he scored for the player —
// his own deployment records ask for none. Restore only into the player table;
// both callers (800A84F8, 80210758) fill records that are meant to keep kills.
inline bool player_pilot(uint32_t pilot){return pilot-pilot_table<pilot_side_stride;}

// BUG06/FIX02. S55's ending only unhides Banjo/Daitarn (3D6B), but the real
// protagonists have never registered them. S69 correctly uses 3D5A 165,0,216,500
// for real protagonists and 3D6B for supers. Keep registration in the original
// routine: it also creates the transforming machine's other forms.
inline constexpr uint32_t daitarn_engine=0x8015F950,daitarn_scene=0x8010F5F0;
inline constexpr uint32_t daitarn_units=0x8016A210,daitarn_vars=0x8015E818;
inline constexpr uint16_t banjo=165,daitarn=216;
inline bool daitarn_real(const uint8_t* ram) {
    const auto route=guest::read(ram,daitarn_engine+0x99E,2);
    return route==0x3DD1 || route==0x3DD2;
}
inline bool daitarn_absent(const uint8_t* ram) {
    // Count hidden pilots and inactive transformation forms as existing too.
    // Never re-register an existing/partially populated pair: preserve its
    // experience, upgrades, parts and party restriction, even in modified saves.
    for(unsigned i=0;i<100;++i) {
        const uint32_t p=pilot_table+i*0x4C;
        if(guest::read(ram,p,1) && guest::read(ram,p+2,2)==banjo)return false;
    }
    for(unsigned i=0;i<140;++i) {
        const uint32_t u=daitarn_units+i*0x54;
        const auto number=guest::read(ram,u+2,2);
        if(guest::read(ram,u,1) && number>=216 && number<=218)return false;
    }
    return true;
}
inline bool daitarn_join_event(const uint8_t* ram,uint32_t owner) {
    if(guest::read(ram,daitarn_scene,1)!=55 || !daitarn_real(ram) || !guest::valid(owner,0x28))return false;
    const auto pc=guest::read(ram,owner+0x1C,4);
    // The original ending tail at ROM 001C5B24. Match the actual script, not
    // just a borrowed scene number or any use of 3D6B on the same pair.
    constexpr std::array<uint16_t,17> tail={0x3D6B,165,216,0,0x3D5A,201,0,270,273,
        0x3D5A,7,0,8,500,0x3D4B,56,0xFFFF};
    if(pc<2 || !guest::valid(pc-2,uint32_t(tail.size()*2)))return false;
    for(unsigned i=0;i<tail.size();++i)if(guest::read(ram,pc-2+i*2,2)!=tail[i])return false;
    return true;
}
inline unsigned daitarn_variable(const uint8_t* ram,unsigned index) {
    return (guest::read(ram,daitarn_vars+2*(index/8),2)>>(2*(index%8)))&3;
}
// The next scenes reachable after S55 on OZ, with the 123/124 return guarded by
// var10==1. No early scene, IA/CP scene, or arbitrary numerical range qualifies.
inline bool daitarn_after_join(unsigned scene) {
    switch(scene) {
    case 56:case 57:case 59:case 60:case 61:
    case 91:case 92:case 93:case 94:case 95:case 96:case 97:case 98:case 99:
    case 100:case 101:case 102:case 103:case 104:case 105:case 106:
    case 123:case 124:case 134:case 137:case 138:case 139:return true;
    default:return false;
    }
}
struct DaitarnRepair {bool eligible{},hidden{};};
inline DaitarnRepair daitarn_old_save(const uint8_t* ram) {
    if(!daitarn_real(ram) || daitarn_variable(ram,10)!=1 ||
       int8_t(guest::read(ram,0x8015E9C5,1))!=-1)return {};
    unsigned next=guest::read(ram,daitarn_scene,1);
    if(next>=109 && next<=122)next=guest::read(ram,0x8010F5F2,1); // scheduled Link battle
    if(!daitarn_after_join(next))return {};
    // 3D73 1 hides Banjo's space party on the earth branch. Preserve this until
    // S104's opening runs 3D74 (reunion), including the intermission before it.
    return {true,next==103 || next==134 || next==104};
}
template<class Register,class Hide>
inline bool repair_daitarn(uint8_t* ram,DaitarnRepair repair,Register&& register_pair,Hide&& hide_pair) {
    if(!repair.eligible || !daitarn_absent(ram))return false;
    register_pair();
    if(repair.hidden)hide_pair();
    return true;
}
}
