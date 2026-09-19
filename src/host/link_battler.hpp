#pragma once
#include "guest_memory.hpp"
#include <array>
#include <cstdint>
#include <cstring>

// Link Battler link without a Transfer Pak (docs/gameplay/link-battler.md).
//
// The intermission's リンク screen reads a 4 KB block from the Game Boy cartridge
// through a small resident driver (80090F44..80091428), and the original code above
// it does everything else: 801D9F80 opens a series when the block has its pilot or
// machine bit and the player has none of its machines, moving the next scene to one
// of the link stages 109..122 (the story's next scene is kept in 8010F5F2); 801D975C
// lists the pilots both games have for the level exchange. The native host has no
// pak, so the driver is replaced by a virtual cartridge holding a block made here:
// the player's own pilots with their own experience (the exchange changes nothing)
// plus the pilot bit of each series the player chose to link.
namespace srw64::link {
using guest::read;
using guest::write8;

inline constexpr uint32_t next_scene=0x8010F5F0, story_scene=0x8010F5F2;   // u8 scene indices
inline constexpr uint32_t campaign_vars=0x8015E818;   // 200 two-bit script variables, 8 per u16
// The player's machines (800A6E68 pool 0) and pilots, as 801D9F80 and 801D975C scan them.
inline constexpr uint32_t units=0x8016A210, unit_size=0x54, unit_slots=140, unit_number=0x02;
inline constexpr uint32_t pilots=0x80172F40, pilot_size=0x4C, pilot_slots=100, pilot_number=0x02, pilot_experience=0x12;

// The block: 0x1000 bytes at linear SRAM address 0xA000 (80091284 maps it to RAM bank
// 5). 801DAEAC only checks the two magic strings; 801D98EC sums the first 0x9C9 bytes
// into +0x9C9 before a write-back.
inline constexpr uint32_t block_size=0x1000, block_address=0xA000;
inline constexpr uint32_t owned_pilots=0x020;        // 92 bits, bit i = lb_pilots[i]
inline constexpr uint32_t pilot_experience_at=0x138; // u16 little-endian per pilot bit
inline constexpr uint32_t head_magic_at=0x000, tail_magic_at=0x9B9;
inline constexpr char head_magic[]="ROBOT_TAISENN_GB", tail_magic[]="LINK_BATTLER_V00";
using Block=std::array<uint8_t,block_size>;

// D_801DCB90: the SRW64 pilot behind each Link Battler pilot bit, -1 unused. 801D975C
// also matches 36 アムロ to record 262 and 96 ミリアルド to 286 ゼクス.
inline constexpr unsigned lb_pilot_count=92;
inline constexpr int16_t lb_pilots[lb_pilot_count]={
    -1,3,4,7,8,9,10,11,12,13,-1,25,26,27,28,29,30,31,-1,36,38,39,40,41,42,43,45,50,51,52,53,54,284,56,57,58,
    59,89,91,92,93,95,96,269,107,108,112,113,114,115,116,120,121,123,124,133,138,140,145,152,154,156,165,166,
    171,181,182,183,184,185,186,195,196,197,199,201,204,205,206,-1,230,232,233,234,235,236,237,238,266,246,-1,272};

// The three series, bit n = 1 << n as in D_801DCABC. 801D9F80 skips a series once the
// player has one of its machines, and otherwise opens it for either of its pilot bits
// (D_801DCD0C..D_801DCD13) or its machine bits, which the block leaves clear. The link
// stage sets the variable to 0 when it ends (initial value 3).
struct Series {uint16_t variable;std::array<int16_t,2> machines;std::array<uint8_t,2> pilot_bits;};
inline constexpr unsigned series_count=3;
inline constexpr Series series[series_count]={
    {48,{42,289},{23,80}},   // ガンダムF91: F91, ビギナ・ギナ; シーブック, セシリー
    {49,{184,-1},{55,55}},   // ゴーショーグン; 真吾
    {50,{206,-1},{59,59}},   // ザンボット3; 勝平
};
// Scenes 109..122 in pairs (ground, space); the pair's series mask as D_801DCABC lists it.
inline constexpr uint8_t link_first=109, link_last=122;
inline constexpr uint8_t scene_masks[7]={1,2,4,3,5,6,7};
inline unsigned scene_mask(unsigned scene) {
    return scene>=link_first && scene<=link_last?scene_masks[(scene-link_first)/2]:0;
}

inline unsigned variable(const uint8_t* ram,unsigned index) {
    return (read(ram,campaign_vars+2*(index/8),2)>>(2*(index%8)))&3;
}
inline bool owns_machine(const uint8_t* ram,int16_t number) {
    for(uint32_t slot=0;slot<unit_slots;++slot) {
        const uint32_t unit=units+slot*unit_size;
        if(read(ram,unit,1) && int16_t(read(ram,unit+unit_number,2))==number)return true;
    }
    return false;
}
// The first player pilot record 801D975C would pair with a Link Battler pilot, or 0.
inline uint32_t pilot_for(const uint8_t* ram,int16_t number) {
    for(uint32_t slot=0;slot<pilot_slots;++slot) {
        const uint32_t pilot=pilots+slot*pilot_size;
        const unsigned flags=read(ram,pilot,1);
        if(!flags || (flags&0x80))continue;
        const int16_t id=int16_t(read(ram,pilot+pilot_number,2));
        if(id==number || (number==36 && id==262) || (number==96 && id==286))return pilot;
    }
    return 0;
}

struct Status {
    std::array<bool,series_count> joined{};     // its link stage was played, or the player has its machine
    std::array<bool,series_count> scheduled{};  // the next scene is already a link stage bringing it
};
inline Status status(const uint8_t* ram) {
    Status result;
    const unsigned mask=scene_mask(read(ram,next_scene,1));
    for(unsigned n=0;n<series_count;++n) {
        const auto& entry=series[n];
        result.joined[n]=variable(ram,entry.variable)==0 || owns_machine(ram,entry.machines[0]) ||
            (entry.machines[1]>=0 && owns_machine(ram,entry.machines[1]));
        result.scheduled[n]=mask>>n&1;
    }
    return result;
}
inline bool key_bit(unsigned bit) {
    for(const auto& entry:series)for(auto key:entry.pilot_bits)if(key==bit)return true;
    return false;
}
// `selection`: series bits to link. Joined series are dropped, so a series whose
// machines were sold after it joined does not come a second time.
inline Block build(const uint8_t* ram,unsigned selection) {
    Block block{};
    std::memcpy(block.data()+head_magic_at,head_magic,16);
    std::memcpy(block.data()+tail_magic_at,tail_magic,16);
    auto set=[&](unsigned bit){block[owned_pilots+bit/8]|=uint8_t(1u<<(bit%8));};
    for(unsigned bit=0;bit<lb_pilot_count;++bit) {
        if(lb_pilots[bit]<0 || key_bit(bit))continue;
        const uint32_t pilot=pilot_for(ram,lb_pilots[bit]);
        if(!pilot)continue;
        set(bit);
        const uint32_t experience=read(ram,pilot+pilot_experience,2);
        block[pilot_experience_at+2*bit]=uint8_t(experience);
        block[pilot_experience_at+2*bit+1]=uint8_t(experience>>8);
    }
    const auto state=status(ram);
    for(unsigned n=0;n<series_count;++n)if((selection>>n&1) && !state.joined[n])set(series[n].pilot_bits[0]);
    return block;
}

// The virtual cartridge. `prepare` runs when the リンク screen opens; the screen then
// reads the block (801D9544) and may write it back (801D966C) several times.
struct Cartridge {Block block{};bool ready{};};
inline Cartridge& cartridge(){static Cartridge value;return value;}
inline void prepare(const uint8_t* ram,unsigned selection){cartridge()={build(ram,selection),true};}
// 80091284(a0 write, a1 linear address, a2 buffer, a3 length).
inline void transfer(uint8_t* ram,bool write,uint32_t address,uint32_t buffer,uint32_t length) {
    auto& cart=cartridge();
    if(!cart.ready)prepare(ram,0);
    for(uint32_t i=0;i<length;++i) {
        const uint32_t at=address+i-block_address;
        if(write){if(at<block_size)cart.block[at]=uint8_t(read(ram,buffer+i,1));}
        else if(guest::valid(buffer+i,1))write8(ram,buffer+i,at<block_size?cart.block[at]:0);
    }
}
}
