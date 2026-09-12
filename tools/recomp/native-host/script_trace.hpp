#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

uint64_t srw64_current_vi();

namespace srw64::script_trace {
inline bool enabled() {
    static const bool value=[] {const char* p=std::getenv("SRW64_SCRIPT_TRACE");return p && std::string_view(p)=="1";}();
    return value;
}
// RDRAM is word-swapped in the host. These reads never mutate guest memory.
inline bool valid(uint32_t p, uint32_t size) {
    return (p & 0xE0000000u)==0x80000000u && (p & 0x1FFFFFFFu)<=0x800000u-size;
}
inline uint32_t read(const uint8_t* ram,uint32_t p,unsigned size) {
    if(!valid(p,size))return 0;
    uint32_t value=0;
    for(unsigned i=0;i<size;++i)value=(value<<8)|ram[((p&0x1FFFFFFFu)+i)^3];
    return value;
}
struct Snapshot {
    uint32_t pc{},handler{};
    uint16_t state{},acc{},route{},choice{},event_type{},engine_phase{},turn{};
    uint8_t scene{},side{};
    std::array<uint8_t,3> units{};
    std::array<uint16_t,8> words{};
};
inline Snapshot snapshot(const uint8_t* ram,uint32_t engine,uint32_t owner) {
    Snapshot s;
    s.pc=read(ram,owner+0x1C,4);s.handler=read(ram,owner+0x30,4);
    s.state=read(ram,owner+0x24,2);s.engine_phase=read(ram,engine+4,2);
    s.acc=read(ram,engine+0x99C,2);s.route=read(ram,engine+0x99E,2);
    s.choice=read(ram,engine+0x994,2);s.event_type=read(ram,engine+0x944,2);
    s.scene=read(ram,0x8010F5F0,1);s.turn=read(ram,0x8010F5EA,2);s.side=read(ram,0x8010F5E8,1);
    for(unsigned i=0;i<3;++i)s.units[i]=read(ram,engine+0x9B0+i,1);
    for(unsigned i=0;i<s.words.size();++i)s.words[i]=read(ram,s.pc+i*2,2);
    return s;
}
inline void print(const Snapshot& s) {
    std::fprintf(stderr,"{\"pc\":%u,\"handler\":%u,\"state\":%u,\"phase\":%u,\"scene\":%u,\"turn\":%u,\"side\":%u,\"event_type\":%u,\"acc\":%u,\"route\":%u,\"choice\":%u,\"units\":[%u,%u,%u],\"words\":[",
        s.pc,s.handler,s.state,s.engine_phase,s.scene,s.turn,s.side,s.event_type,s.acc,s.route,s.choice,s.units[0],s.units[1],s.units[2]);
    for(unsigned i=0;i<s.words.size();++i)std::fprintf(stderr,"%s%u",i?",":"",s.words[i]);
    std::fputs("]}",stderr);
}
inline void record(uint32_t engine,uint32_t owner,const Snapshot& before,const Snapshot& after) {
    // A poll can both scan conditions and execute an immediate command. Preserve
    // both raw boundaries; this is not a claim to trace each internal condition.
    if(before.state!=0 && before.pc==after.pc && before.state==after.state &&
       before.handler==after.handler && before.engine_phase==after.engine_phase)return;
    // Keep GPU/audio diagnostics from splitting a JSON record on stderr.
    flockfile(stderr);
    std::fprintf(stderr,"SRW64_SCRIPT_TRACE {\"schema\":\"srw64.script-poll.v2\",\"vi\":%llu,\"engine\":%u,\"owner\":%u,\"before\":",
        (unsigned long long)srw64_current_vi(),engine,owner);
    print(before);std::fputs(",\"after\":",stderr);print(after);std::fputs("}\n",stderr);
    funlockfile(stderr);
}
}
