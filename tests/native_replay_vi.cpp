#include "../tools/recomp/native-host/replay_vi.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    constexpr uint32_t words[]={0x0C02D82C,0x00822021,0x0C02D840,0x24040002,0x0C02D840,
        0x24040004,0x0C02D840,0x24040080,0x0C02D840,0x24040020};
    std::vector<uint8_t> fixture(0x8BFDC);
    for(size_t i=0;i<10;++i) for(unsigned j=0;j<4;++j) fixture[0x8BFB4+i*4+j]=words[i]>>(24-j*8);
    assert(srw64_replay_vi_status(fixture.data(),fixture.size(),0x311E)==0x3106);
    // Exercise a table with the filter on and a different AA mode. Only the
    // requested special features change; the other mode bits are preserved.
    assert(srw64_replay_vi_status(fixture.data(),fixture.size(),0x1521A)==0x5206);
    bool rejected=false;
    try {srw64_replay_vi_status(fixture.data(),20,0x311E);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);
    fixture[0x8BFC3]^=1; // Alter the gamma-off argument: reconstruction must stop.
    rejected=false;
    try {srw64_replay_vi_status(fixture.data(),fixture.size(),0x311E);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> real((std::istreambuf_iterator<char>(file)),{});
        assert(real.size()==0x800000);
        const size_t mode=0x178A10+80*real[0x10F5B2];
        const auto* p=real.data()+mode+4;
        const uint32_t status=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
        assert(status==0x311E && srw64_replay_vi_status(real.data(),real.size(),status)==0x3106);
    }
    std::cout<<"Replay VI: effective status, retained mode bits, changed-code rejection and capture passed.\n";
}
