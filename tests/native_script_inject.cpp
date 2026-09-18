#include "script_inject.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>
static uint64_t current_vi=100;
uint64_t srw64_current_vi(){return current_vi;}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    setenv("SRW64_SCRIPT_INJECT","1",1);
    using namespace srw64::script_inject;
    std::vector<uint8_t> ram(0x800000);
    auto put=[&](uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=v>>((n-i-1)*8);};
    // Parser: malformed, wrong magic, odd hex, missing terminator, bad type.
    std::string error;
    assert(!parse("SRWJ1 1",error) && error=="malformed");
    assert(!parse("SRWC1 1 10 000C00000000000000003D380001FFFF",error) && error=="magic");
    assert(!parse("SRWJ1 1 10 000C00000000000000003D380001FFF",error) && error=="words");
    assert(!parse("SRWJ1 1 10 000C00000000000000003D380001",error) && error=="terminator");
    assert(!parse("SRWJ1 1 10 000F00000000000000003D380001FFFF",error) && error=="event-type");
    auto ok=parse("SRWJ1 1 10 000C00000000000000003D380001FFFF",error);
    assert(ok && ok->words.size()==8 && ok->words[5]==0x3D38 && ok->words[7]==0xFFFF);
    // Nothing happens without a queued request; an idle map is required to apply.
    auto original=ram;
    service(ram.data());observe(ram.data());assert(ram==original);
    std::ofstream(dir/"script-inject.txt")<<"SRWJ1 1 10 000C00000000000000003D380001FFFF\n";
    poll_file(dir);
    poll_file(dir); // same sequence: ignored, no duplicate queue
    assert(state().pending && state().pending->sequence==1);
    service(ram.data());assert(ram==original); // engine phase is 0, not C0: deferred
    put(engine+4,0xC0,2);put(engine+0x97C,0x80,2);put(engine+0x9AA,0,2);put(0x8015DA02,3,1);put(engine+0x98E,7,2);
    service(ram.data());assert(read(ram.data(),engine+0x97C,2)==0x80); // phase byte 0 (between stages) is not the player phase
    put(0x8010F5E8,1,1);
    original=ram;
    put(scratch+0x100,1,1);service(ram.data());assert(read(ram.data(),engine+0x97C,2)==0x80); // scratch in use: deferred
    put(scratch+0x100,0,1);original=ram;
    service(ram.data());
    assert(state().active && read(ram.data(),engine+0x97C,2)==0x2000 && read(ram.data(),owner+0x1C,4)==scratch+10);
    assert(read(ram.data(),scratch,2)==0x000C && read(ram.data(),scratch+10,2)==0x3D38 && read(ram.data(),scratch+14,2)==0xFFFF);
    assert(read(ram.data(),owner+8,1)==0xFF && read(ram.data(),owner+0x24,2)==0);
    // Progress inside the range keeps the injection; completion restores the
    // registered-event counter and zeroes the scratch words.
    current_vi=200;put(owner+0x1C,scratch+14,4);observe(ram.data());assert(state().active);
    put(owner+0x1C,0,4);put(owner+0x24,0x80,2);put(engine+0x97C,0x80,2);put(engine+0x98E,6,2);
    observe(ram.data());
    assert(!state().active && read(ram.data(),engine+0x98E,2)==7);
    for(uint32_t i=0;i<16;i+=2)assert(read(ram.data(),scratch+i,2)==0);
    // A second request with a lower sequence is ignored; a busy engine rejects.
    std::ofstream(dir/"script-inject.txt")<<"SRWJ1 1 10 000C00000000000000003D380001FFFF\n";
    poll_file(dir);assert(!state().pending);
    std::ofstream(dir/"script-inject.txt")<<"SRWJ1 2 10 000C00000000000000003D380001FFFF\n";
    poll_file(dir);assert(state().pending && state().pending->sequence==2);
    std::ofstream(dir/"script-inject.txt")<<"SRWJ1 3 10 000C00000000000000003D380001FFFF\n";
    poll_file(dir);assert(state().pending->sequence==2); // busy: sequence 3 rejected and logged
    std::ifstream log(dir/"script-inject-events.jsonl");std::string line;unsigned rejected=0,complete=0;
    while(std::getline(log,line)){if(line.find("\"rejected\"")!=std::string::npos)++rejected;if(line.find("\"complete\"")!=std::string::npos)++complete;}
    assert(rejected==1 && complete==1);
    return 0;
}
