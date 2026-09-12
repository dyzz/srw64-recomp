#include "script_move_probe.hpp"
#include <cassert>
#include <fstream>
#include <vector>
uint64_t srw64_current_vi(){return 42;}
int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream rom(argv[1],std::ios::binary);assert(rom);
    std::vector<uint8_t> ram(0x800000);
    auto put=[&](uint32_t p,uint32_t v,unsigned n){
        for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=v>>((n-i-1)*8);
    };
    using namespace srw64::script_move_probe;
    rom.seekg(0x19BF10);
    for(unsigned i=0;i<256;++i){char c;assert(rom.get(c));put(base+i,uint8_t(c),1);}
    constexpr uint32_t engine=0x8015F950,owner=engine+0x948;
    put(engine+4,0xC1,2);put(engine+0x99E,0x3DD3,2);put(owner+0x1C,command,4);
    auto original=ram;
    Probe disabled(Mode::off);disabled.before(ram.data(),engine,owner);assert(ram==original);
    Probe baseline(Mode::baseline);baseline.before(ram.data(),engine,owner);
    assert(baseline.active && ram==original);
    for(uint32_t address:{base,command,parameter,engine+0x99E,0x8010F5F0u}) {
        ram=original;ram[(address&0x1FFFFFFF)^3]^=1;auto bad=ram;
        Probe rejected(Mode::target17);rejected.before(ram.data(),engine,owner);
        assert(!rejected.active && ram==bad);
    }
    ram=original;Probe changed(Mode::target17);changed.before(ram.data(),engine,owner);
    assert(changed.active && read(ram.data(),parameter,2)==0x1911);
    auto expected=original;expected[((parameter&0x1FFFFFFF)+1)^3]=0x11;assert(ram==expected);
    put(owner+0x1C,command+2,4);put(owner+0x24,2,2);changed.after(ram.data(),owner);
    assert(changed.active && read(ram.data(),parameter,2)==0x1911);
    put(owner+0x1C,command+6,4);put(owner+0x24,0,2);changed.after(ram.data(),owner);
    assert(!changed.active && read(ram.data(),parameter,2)==0x1912);
    put(owner+0x1C,command,4);assert(ram==original);
    changed.before(ram.data(),engine,owner);assert(ram==original); // one shot
    Probe conflict(Mode::target17);conflict.before(ram.data(),engine,owner);
    put(parameter,0x1910,2);put(owner+0x1C,command+6,4);auto external=ram;
    conflict.after(ram.data(),owner);assert(ram==external); // never overwrite another mutation
}
