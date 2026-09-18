#pragma once
#include "script_trace.hpp"
#include <cstring>

// A deliberately fixed, opt-in experiment for original scene 0. This is not a
// general script patch API. The launcher pins ROM/profile and isolates SRAM.
namespace srw64::script_move_probe {
using script_trace::read;
constexpr uint32_t base=0x8019B400, command=base+0xC8, parameter=command+4;
enum class Mode { off, baseline, target17 };
inline Mode mode() {
    const char* p=std::getenv("SRW64_SCRIPT_MOVE_PROBE");
    if(p && std::string_view(p)=="baseline")return Mode::baseline;
    if(p && std::string_view(p)=="target17")return Mode::target17;
    return Mode::off;
}
inline float floating(const uint8_t* ram,uint32_t address) {
    uint32_t bits=read(ram,address,4);float value;std::memcpy(&value,&bits,4);return value;
}
inline void write_parameter(uint8_t* ram,uint16_t value) {
    ram[(parameter&0x1FFFFFFF)^3]=value>>8;
    ram[((parameter&0x1FFFFFFF)+1)^3]=value&255;
}
inline bool identity(const uint8_t* ram,uint32_t engine,uint32_t owner) {
    if(read(ram,0x8010F5F0,1)!=0 || read(ram,engine+0x99E,2)!=0x3DD3 ||
       read(ram,engine+4,2)!=0xC1 || read(ram,owner+0x24,2)!=0 ||
       read(ram,owner+0x1C,4)!=command)return false;
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<256;++i)hash=(hash^read(ram,base+i,1))*16777619u;
    return hash==0x476B3D02u && read(ram,command,2)==0x3D3C &&
           read(ram,command+2,2)==27 && read(ram,parameter,2)==0x1912;
}
inline void emit(const uint8_t* ram,const char* phase,uint32_t owner,Mode selected) {
    flockfile(stderr);
    std::fprintf(stderr,"SRW64_MOVE_PROBE {\"schema\":\"srw64.move-probe.v1\",\"vi\":%llu,\"phase\":\"%s\",\"mode\":\"%s\",\"pc\":%u,\"state\":%u,\"parameter\":%u,\"units\":[",
        (unsigned long long)srw64_current_vi(),phase,selected==Mode::target17?"target17":"baseline",
        read(ram,owner+0x1C,4),read(ram,owner+0x24,2),read(ram,parameter,2));
    bool first=true;
    for(unsigned side=0;side<3;++side)for(unsigned slot=0;slot<30;++slot) {
        uint32_t entry=0x8015E100+side*0x258+slot*0x14;
        if(read(ram,entry,1)!=1)continue;
        uint32_t unit=read(ram,entry+12,4),pilot=read(ram,unit+0x38,4);
        unsigned sprite=0x42+side*30+slot;
        std::fprintf(stderr,"%s{\"side\":%u,\"slot\":%u,\"unit\":%u,\"actor\":%u,\"flags\":%u,\"x\":%u,\"y\":%u,\"sprite\":%u,\"sprite_x\":%.9g,\"sprite_y\":%.9g}",
            first?"":",",side,slot,unit,read(ram,pilot+2,2),read(ram,entry+1,1),
            read(ram,entry+4,1),read(ram,entry+5,1),sprite,
            floating(ram,0x800FFA74+sprite*196),floating(ram,0x800FFA78+sprite*196));
        first=false;
    }
    std::fputs("]}\n",stderr);funlockfile(stderr);
}
struct Probe {
    Mode selected;
    bool attempted=false,active=false,opening_recorded=false;
    uint32_t active_owner=0;
    explicit Probe(Mode value):selected(value){}
    void before(uint8_t* ram,uint32_t engine,uint32_t owner) {
        if(selected==Mode::off || attempted || read(ram,owner+0x1C,4)!=command ||
           read(ram,owner+0x24,2)!=0)return;
        attempted=true;
        if(!identity(ram,engine,owner)){emit(ram,"identity-rejected",owner,selected);return;}
        active_owner=owner;active=true;
        emit(ram,"before",owner,selected);
        if(selected==Mode::target17)write_parameter(ram,0x1911);
        emit(ram,"armed",owner,selected);
    }
    void after(uint8_t* ram,uint32_t owner) {
        if(selected==Mode::off || owner!=active_owner)return;
        auto pc=read(ram,owner+0x1C,4);
        if(active) {
            bool complete=pc==command+6 && read(ram,owner+0x24,2)==0;
            emit(ram,complete?"complete":"poll",owner,selected);
            if(complete) {
                active=false;
                if(selected==Mode::target17) {
                    if(read(ram,parameter,2)==0x1911) {
                        write_parameter(ram,0x1912);emit(ram,"restored",owner,selected);
                    } else emit(ram,"restore-conflict",owner,selected);
                }
            }
        }
        if(attempted && !active && !opening_recorded && pc>=base+254 && pc<=base+256) {
            opening_recorded=true;emit(ram,"opening-end",owner,selected);
        }
    }
};
inline Probe& instance(){static Probe p(mode());return p;}
}
