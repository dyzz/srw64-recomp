#include "link_battler.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace srw64::link;

static std::vector<uint8_t> ram(0x800000);
static void put(uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=uint8_t(v>>((n-i-1)*8));}
static uint32_t get(uint32_t p,unsigned n){return read(ram.data(),p,n);}
static void pilot(unsigned slot,unsigned flags,uint16_t number,uint16_t experience) {
    const uint32_t record=pilots+slot*pilot_size;
    put(record,flags,1);put(record+pilot_number,number,2);put(record+pilot_experience,experience,2);
}
static void machine(unsigned slot,uint16_t number){put(units+slot*unit_size,1,1);put(units+slot*unit_size+unit_number,number,2);}
static void variable(unsigned index,unsigned value) {
    const uint32_t at=campaign_vars+2*(index/8);
    const unsigned shift=2*(index%8);
    put(at,(get(at,2)&~(3u<<shift))|(value<<shift),2);
}
static bool bit(const Block& block,unsigned n){return block[owned_pilots+n/8]>>(n%8)&1;}
static unsigned experience(const Block& block,unsigned n){return block[pilot_experience_at+2*n]|(block[pilot_experience_at+2*n+1]<<8);}

int main() {
    // A new game: every campaign variable is 3.
    for(unsigned i=0;i<200;++i)variable(i,3);
    assert(srw64::link::variable(ram.data(),48)==3 && srw64::link::variable(ram.data(),50)==3);
    variable(49,0);
    assert(srw64::link::variable(ram.data(),49)==0 && srw64::link::variable(ram.data(),48)==3);
    // Variables 48..50 share the u16 at +12, two bits each from bit 0.
    assert(get(campaign_vars+12,2)==0xFFF3);

    assert(scene_mask(108)==0 && scene_mask(123)==0);
    assert(scene_mask(109)==1 && scene_mask(110)==1 && scene_mask(113)==4 && scene_mask(114)==4);
    assert(scene_mask(115)==3 && scene_mask(118)==5 && scene_mask(119)==6 && scene_mask(122)==7);

    // ゴーショーグン joined (var49 = 0); ザンボット3's machine is in the team; the next
    // scene is 合流 (F91 + ゴーショーグン).
    machine(7,206);put(next_scene,115,1);
    auto state=status(ram.data());
    assert(!state.joined[0] && state.joined[1] && state.joined[2]);
    assert(state.scheduled[0] && state.scheduled[1] && !state.scheduled[2]);
    put(next_scene,40,1);
    state=status(ram.data());
    assert(!state.scheduled[0] && !state.scheduled[1] && !state.scheduled[2]);
    // A sold F91 with ビギナ・ギナ still in the team counts as joined too.
    machine(8,289);assert(status(ram.data()).joined[0]);put(units+8*unit_size,0,1);
    assert(!status(ram.data()).joined[0]);

    // Pilots: アムロ under his second record (262 answers bit 19, 36), ドモン, a
    // record with the 0x80 flag, and シーブック, whose bit only a chosen F91 sets.
    pilot(0,1,262,0x1234);pilot(1,1,4,0x0456);pilot(2,0x81,7,0x0999);pilot(3,1,41,0x0777);
    Block block=build(ram.data(),0);
    assert(std::memcmp(block.data()+head_magic_at,"ROBOT_TAISENN_GB",16)==0);
    assert(std::memcmp(block.data()+tail_magic_at,"LINK_BATTLER_V00",16)==0);
    assert(bit(block,19) && experience(block,19)==0x1234);
    assert(bit(block,2) && experience(block,2)==0x0456);
    assert(!bit(block,3) && experience(block,3)==0);   // flagged record skipped
    assert(!bit(block,23) && !bit(block,80) && !bit(block,55) && !bit(block,59));
    unsigned count=0;
    for(unsigned n=0;n<lb_pilot_count;++n)count+=bit(block,n);
    assert(count==2);
    // Choosing all three: F91 is free, ゴーショーグン and ザンボット3 have joined.
    block=build(ram.data(),7);
    assert(bit(block,23) && !bit(block,55) && !bit(block,59) && !bit(block,80));
    assert(experience(block,23)==0);
    // Only the pilot bits: the machine bits (+0x2D on) stay clear.
    for(uint32_t at=owned_pilots+12;at<pilot_experience_at;++at)assert(block[at]==0);

    // The cartridge: read the block into a guest buffer, write part of it back.
    const uint32_t buffer=0x801DDAA0;
    prepare(ram.data(),1);
    transfer(ram.data(),false,block_address,buffer,block_size);
    for(uint32_t i=0;i<block_size;++i)assert(get(buffer+i,1)==cartridge().block[i]);
    put(buffer+0x10,0x5A,1);
    transfer(ram.data(),true,block_address,buffer,block_size);
    assert(cartridge().block[0x10]==0x5A);
    // Outside the block reads are zero and writes are dropped.
    put(buffer,0xAB,1);
    transfer(ram.data(),false,block_address+block_size-1,buffer,2);
    assert(get(buffer,1)==cartridge().block[block_size-1] && get(buffer+1,1)==0);
    transfer(ram.data(),true,0x9FFF,buffer,1);
    assert(cartridge().block[0]=='R');
    std::puts("link battler: ok");
}
