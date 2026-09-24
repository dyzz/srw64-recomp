#include "native_name_entry.hpp"
#include "native_dialogue.hpp"
#include "game_adapter/name_codec.hpp"
#include "game_hooks.hpp"
#include "presentation_settings.hpp"
#include "mini_stage.hpp"
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
unsigned cursor_moves{};
const uint8_t* rom_image{};
size_t rom_bytes{};
std::ofstream log;
uint16_t half(const uint8_t* ram,uint32_t p) {uint16_t value;std::memcpy(&value,ram+(p^2),2);return value;}
void half(uint8_t* ram,uint32_t p,uint16_t value) {std::memcpy(ram+(p^2),&value,2);}
void record(const char* kind) {
    nlohmann::json values=nlohmann::json::array();
    for(const auto& value:current.values)values.push_back(dialogue::utf8(value));
    log<<nlohmann::json({{"schema","srw64.native-name-event.v1"},{"kind",kind},
        {"vi",srw64_current_vi()},{"serial",current.serial},{"person",current.person},
        {"route",current.route},{"values",values},{"error",current.error}}).dump()<<'\n';log.flush();
}
// Default names on the selection page. 801C3744 builds them from overlay tables:
// given names at 801C6C00 and family names at 801C6C70, 7 codes per route, the
// partner 0x38 bytes after the protagonist, padded with code 0xCA (the blank the
// original also writes, 0x1549). A code is the name grid's character: text id
// 0x147F + code, an 8-byte header and one font glyph.
constexpr uint16_t grid_base=0x147F, grid_blank=0xCA;
uint16_t grid_glyph(uint16_t code) {
    auto word=[](uint32_t p){return (uint32_t(rom_image[p])<<24)|(rom_image[p+1]<<16)|(rom_image[p+2]<<8)|rom_image[p+3];};
    const uint32_t entry=Codec::table_base+4+uint32_t(grid_base+code)*8;
    if(!rom_image || entry+4>rom_bytes)return 0xFFFF;
    const uint32_t at=Codec::table_base+word(entry);
    return at+10>rom_bytes?0xFFFF:uint16_t((rom_image[at+8]<<8)|rom_image[at+9]);
}
std::u16string default_name(const uint8_t* ram,uint32_t table) {
    std::vector<uint16_t> codes;
    for(unsigned i=0;i<7;++i)codes.push_back(half(ram,table+2*i));
    while(!codes.empty() && codes.back()==grid_blank)codes.pop_back();
    for(auto& code:codes)code=grid_glyph(code);
    return codec.decode(codes);
}
void begin_selection(uint8_t* ram) {
    current.serial=++serial;current.person=Selection;current.route=half(ram,0x1C70FA)&3;
    for(unsigned route=0;route<4;++route)for(unsigned p=0;p<2;++p) {
        auto& choice=current.choices[route];
        choice.portraits[p]=portrait_paths[p][route];
        for(unsigned f=0;f<2;++f) {
            choice.names[p][f]=default_name(ram,(f?0x1C6C70:0x1C6C00)+(p?0x38:0)+route*14);
            // Unknown glyphs: keep the original page rather than show blanks.
            if(choice.names[p][f].empty()){current={};return;}
        }
    }
    current.visible=true;owning=true;
}
void begin(uint8_t* ram,unsigned person) {
    if(!enabled || !ready || overlay!=0x1090A0)return;
    std::lock_guard lock(mutex);current={};owning=false;closed=false;cursor_moves=0;
    if(!settings::native_name_entry_ui())return;   // the original pages, chosen in the settings window
    if(person==Selection){begin_selection(ram);return;}
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
    if(person==Selection && cursor_moves) {
        cursor_moves=0;call=*ctx;call.r4=0xB9;resident_func_8007E8A8(ram,&call);
    }
    if(mini_stage::quick_start()) {
        // Debug mini-stage entry still uses the native adapter's normal default
        // name validation and writes. No legacy name-grid input script needed.
        if(person==Selection)current.route=0;
        cancelled=false;review_confirm=true;current.pending=true;
        record("mini-stage-default");
    }
    if(!current.pending)return true;
    current.pending=false;
    if(person==Selection) {
        // The original's はい (801C52DC): a route other than the last confirmed one
        // resets both name banks and loads that route's defaults (801C3744).
        const uint16_t route=uint16_t(current.route);
        call=*ctx;call.r4=0xB7;resident_func_8007E8A8(ram,&call);
        half(ram,0x1C70FA,route);
        if(route!=half(ram,0x1C70B0)) {
            for(unsigned i=0;i<30;++i){half(ram,0x1C6FB8+2*i,0x1549);half(ram,0x1C70B8+2*i,0x1549);}
            call=*ctx;load_001090A0_func_801C3744(ram,&call);
        }
        const uint32_t state=1;   // 801C6FB0: 0 selection, 1 protagonist, 2 partner, 3 review
        std::memcpy(ram+0x1C6FB0,&state,4);
        half(ram,0x1C70B0,route);
        record("selected");
    } else if(person==Review) {
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
    if(person==Player || person==Partner) {
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
void initialize_rom(const uint8_t* rom,size_t size) {
    if(!enabled)return;
    codec.initialize_rom(rom,size);rom_image=rom;rom_bytes=size;ready=true;
}
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
Request request() {std::lock_guard lock(mutex);auto result=current;if(mini_stage::quick_start())result.visible=false;return result;}
std::u16string decode_glyphs(const std::vector<uint16_t>& codes) {return codec.decode(codes);}
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
void select(uint64_t id,unsigned route) {
    std::lock_guard lock(mutex);
    if(!current.active || current.person!=Selection || current.serial!=id || current.pending || route>3)return;
    if(current.route!=route){current.route=route;++cursor_moves;}
}
void choose(uint64_t id,unsigned route) {
    std::lock_guard lock(mutex);
    if(!current.active || current.person!=Selection || current.serial!=id || current.pending || route>3)return;
    current.route=route;current.pending=true;
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
