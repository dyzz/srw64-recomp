#include "script_trace.hpp"
#include <cassert>
#include <vector>
uint64_t srw64_current_vi() {return 42;}
int main() {
    std::vector<uint8_t> ram(0x800000);
    auto put=[&](uint32_t p,uint32_t v,unsigned n) {
        for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=v>>((n-i-1)*8);
    };
    constexpr uint32_t engine=0x8015F950,owner=engine+0x948,pc=0x8019B40A;
    put(owner+0x1C,pc,4);put(owner+0x30,0x800A0360,4);put(owner+0x24,2,2);
    put(engine+4,0xC1,2);put(engine+0x99E,0x3DD4,2);
    put(0x8010F5EA,0x123,2);put(0x8010F5F0,1,1);put(0x8010F5E8,2,1);
    put(engine+0x9B0,3,1);put(engine+0x9B1,11,1);put(engine+0x9B2,2,1);
    put(pc,0x3D3C,2);put(pc+2,42,2);put(pc+4,0x1508,2);
    auto original=ram;
    auto s=srw64::script_trace::snapshot(ram.data(),engine,owner);
    assert(s.pc==pc && s.handler==0x800A0360 && s.state==2);
    assert(s.engine_phase==0xC1 && s.turn==0x123 && s.route==0x3DD4);
    assert(s.units[0]==3 && s.units[1]==11 && s.units[2]==2);
    assert(s.words[0]==0x3D3C && s.words[1]==42 && s.words[2]==0x1508);
    assert(srw64::script_trace::read(ram.data(),0,4)==0);
    assert(srw64::script_trace::read(ram.data(),0x807FFFFE,4)==0);
    auto end=s;end.state=0;end.pc+=6;
    srw64::script_trace::record(engine,owner,s,end);
    assert(ram==original);
}
