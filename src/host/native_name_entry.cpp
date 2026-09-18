#include "native_name_entry.hpp"
#include "native_dialogue.hpp"
#include "game_adapter/name_codec.hpp"
#include "game_hooks.hpp"
#include "funcs.h"
#include "json/json.hpp"
#include <atomic>
#include <fstream>
#include <mutex>
#include <map>

uint64_t srw64_current_vi();
namespace srw64::names {
namespace {
Codec codec;
bool enabled{},ready{},closed{},cancelled{};
std::atomic_bool owning{};
std::atomic_bool window_owning{};
uint16_t held{};
uint32_t overlay{};
uint64_t serial{};
std::mutex mutex;
Request current;
std::array<std::array<std::array<std::string,2>,4>,2> portrait_paths;
std::map<uint64_t,bool> covers;
uint64_t presented_workload{};
bool presented_cover{};
bool review_confirm{};
std::ofstream log;
uint16_t half(const uint8_t* ram,uint32_t p) {uint16_t value;std::memcpy(&value,ram+(p^2),2);return value;}
void half(uint8_t* ram,uint32_t p,uint16_t value) {std::memcpy(ram+(p^2),&value,2);}
void record(const char* kind) {
    nlohmann::json values=nlohmann::json::array();
    for(const auto& value:current.values)values.push_back(dialogue::utf8(value));
    log<<nlohmann::json({{"schema","srw64.native-name-event.v1"},{"kind",kind},
        {"vi",srw64_current_vi()},{"serial",current.serial},{"person",current.person},
        {"values",values},{"error",current.error}}).dump()<<'\n';log.flush();
}
void begin(uint8_t* ram,unsigned person) {
    if(!enabled || !ready || overlay!=0x1090A0)return;
    std::lock_guard lock(mutex);current={};owning=false;closed=false;
    if(person==Selection)return;
    current.serial=++serial;current.person=person;
    current.route=half(ram,0x1C70FA)&3;
    for(unsigned p=0;p<2;++p)current.portraits[p]=portrait_paths[p][current.route];
    if(person==Review) {
        const uint32_t bank[2][3]={{0x10F5F8,0x10F618,0x10F638},{0x10F608,0x10F628,0x10F644}};
        for(unsigned p=0;p<2;++p)for(unsigned f=0;f<3;++f) {
            std::vector<uint16_t> codes;
            for(unsigned i=0;i<Codec::limits[f];++i) {
                auto code=half(ram,bank[p][f]+2*i);if(code==0xFFFF)break;codes.push_back(code);
            }
            current.names[p][f]=codec.decode(codes);
        }
    } else for(unsigned f=0;f<3;++f) {
        std::vector<uint16_t> codes;
        for(unsigned i=0;i<Codec::limits[f];++i)codes.push_back(half(ram,0x1C71E0+2*(Codec::offsets[f]+i)));
        while(!codes.empty() && codes.back()==0)codes.pop_back();
        current.values[f]=codec.decode(codes);
        if(current.values[f].empty()){current={};return;}
    }
    current.visible=true;owning=true;
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned person) {
    if(!enabled || !ready || overlay!=0x1090A0)return false;
    std::lock_guard lock(mutex);
    if(closed)return true;
    auto call=*ctx;resident_func_80099B30(ram,&call);
    if(int8_t(call.r2)!=-1)return true; // Let the original fade finish first.
    if(!current.visible)return false;
    if(!current.active) {
        current.active=true;++current.revision;record("open");
    }
    if(!current.pending)return true;
    current.pending=false;
    if(person==Review) {
        if(review_confirm) {ram[0x1C70F4^3]=2;record("started");}
        else {const uint32_t state=1;std::memcpy(ram+0x1C6FB0,&state,4);record("edit");}
    } else if(!cancelled) {
        std::array<std::vector<uint16_t>,3> codes;
        for(unsigned f=0;f<3;++f)if(auto error=codec.encode(current.values[f],f,codes[f]);!error.empty()) {
            current.error=error;++current.revision;record("rejected");return true;
        }
        // The original validator writes fields before checking reserved names.
        // Snapshot the complete bank so a rejection cannot leak partial edits.
        std::array<uint8_t,0xA0> before;
        std::memcpy(before.data(),ram+0x10F5F8,before.size());
        std::array<uint16_t,21> editor{},previous{};
        for(unsigned i=0;i<21;++i)previous[i]=half(ram,0x1C71E0+2*i);
        for(unsigned f=0;f<3;++f)std::copy(codes[f].begin(),codes[f].end(),editor.begin()+Codec::offsets[f]);
        for(unsigned i=0;i<21;++i)half(ram,0x1C71E0+2*i,editor[i]);
        call=*ctx;call.r4=person;load_001090A0_func_801C474C(ram,&call);
        if(call.r2) {
            std::memcpy(ram+0x10F5F8,before.data(),before.size());
            for(unsigned i=0;i<21;++i)half(ram,0x1C71E0+2*i,previous[i]);
            current.error="name_invalid";++current.revision;record("rejected");return true;
        }
        for(unsigned i=0;i<21;++i) {
            const auto id=Codec::first_id+editor[i];
            half(ram,0x1C7070+2*i,id);
            half(ram,(person?0x1C6FB8:0x1C70B8)+2*i,id);
        }
        record("committed");
    } else record("cancelled");
    if(person!=Review) {
        const uint32_t state=cancelled?0:person+2;
        std::memcpy(ram+0x1C6FB0,&state,4);
    }
    // Keep the page and input ownership through the guest fade/next init.
    current.active=false;closed=true;++current.revision;
    call=*ctx;call.r4=5;call.r5=1;call.r6=2;resident_func_80099814(ram,&call);
    return true;
}
}
void configure(const std::filesystem::path& directory) {
    const char* path=std::getenv("SRW64_DIALOGUE_DATA");
    if(!path || (std::getenv("SRW64_NATIVE_NAME_ENTRY") && std::string(std::getenv("SRW64_NATIVE_NAME_ENTRY"))=="0"))return;
    std::ifstream source(path);nlohmann::json data;source>>data;
    // Legacy patched ROMs keep their existing name adapter and original UI.
    if(data.at("schema")!="srw64.native-dialogue-data.v2")return;
    for(const auto& [key,value]:data.at("glyphs").items())codec.add(std::stoul(key),dialogue::utf16(value.get<std::string>()));
    if(data.contains("name_entry_assets")) {
        const auto& art=data.at("name_entry_assets");
        for(unsigned p=0;p<2;++p)for(unsigned route=0;route<4;++route) {
            const auto& row=art.at("portraits").at(std::to_string(art.at("route_faces").at(p).at(route).get<unsigned>()));
            portrait_paths[p][route]={row.at("original").get<std::string>(),row.value("hd",row.at("original").get<std::string>())};
        }
    }
    enabled=true;log.open(directory/"name-entry-events.jsonl");
    srw64_game_hooks.name_begin=begin;srw64_game_hooks.name_step=step;
}
void initialize_rom(const uint8_t* rom,size_t size) {if(enabled){codec.initialize_rom(rom,size);ready=true;}}
void overlay_loaded(uint32_t rom,uint32_t ram,uint32_t size) {
    // The following intro loads at 801C4500, overlapping this overlay rather
    // than reusing its start address. Any code overlap ends name-page ownership.
    if(uint64_t(ram)>=0x801C6FB0ULL || uint64_t(ram)+size<=0x801C2600ULL)return;
    std::lock_guard lock(mutex);overlay=ram==0x801C2600?rom:0;current={};closed=false;owning=false;
}
bool descriptor(uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size) {return ready && codec.descriptor(table,id,offset,size);}
bool read(uint32_t rom,uint8_t* ram,uint32_t destination,uint32_t size) {return ready && codec.read(rom,ram,destination,size);}
bool owns_input() {return owning || window_owning;}
void window_claim_input(bool value) {window_owning=value;}
uint16_t input(uint16_t buttons) {held &= buttons;if(owns_input())held|=buttons;return buttons & ~held;}
Request request() {std::lock_guard lock(mutex);return current;}
std::string validate(const std::u16string& value,unsigned field) {std::vector<uint16_t> codes;return codec.encode(value,field,codes);}
void submit(uint64_t id,const std::array<std::u16string,3>& values,bool cancel) {
    std::lock_guard lock(mutex);
    if(!current.active || current.person==Review || current.serial!=id || current.pending)return;
    current.values=values;current.pending=true;cancelled=cancel;current.error.clear();
}
void review(uint64_t id,bool confirm) {
    std::lock_guard lock(mutex);
    if(!current.active || current.person!=Review || current.serial!=id || current.pending)return;
    current.pending=true;review_confirm=confirm;
}
void queue_cover(uint64_t workload,bool visible) {
    std::lock_guard lock(mutex);covers[workload]=visible;
    while(covers.size()>128)covers.erase(covers.begin());
}
bool frame_cover(uint64_t workload) {
    std::lock_guard lock(mutex);auto found=covers.find(workload);return found!=covers.end() && found->second;
}
void cover_presented(uint64_t workload,bool visible) {
    std::lock_guard lock(mutex);
    if(workload>=presented_workload){presented_workload=workload;presented_cover=visible;}
}
bool cover_in_flight() {std::lock_guard lock(mutex);return presented_cover;}
}
