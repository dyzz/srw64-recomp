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
bool enabled{},ready{},closed{};
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
uint32_t word(const uint8_t* ram,uint32_t p) {uint32_t value;std::memcpy(&value,ram+p,4);return value;}
void word(uint8_t* ram,uint32_t p,uint32_t value) {std::memcpy(ram+p,&value,4);}
// Overlay state: 801C6FB0 is the page (0 selection, 1 protagonist's name, 2 partner's,
// 3 review), 801C70F4 = 2 leaves for the story once the fade ends (801C657C),
// 801C70FA the highlighted route, 801C70B0 the last confirmed one.
constexpr uint32_t page_state=0x1C6FB0,leave=0x1C70F4,highlight=0x1C70FA,confirmed=0x1C70B0;
constexpr uint32_t page_selection=0,page_player=1,page_review=3;
void record(const char* kind) {
    nlohmann::json names=nlohmann::json::array();
    for(const auto& person:current.names)for(const auto& value:person)names.push_back(dialogue::utf8(value));
    log<<nlohmann::json({{"schema","srw64.native-name-event.v1"},{"kind",kind},
        {"vi",srw64_current_vi()},{"serial",current.serial},{"person",current.person},
        {"route",current.route},{"names",names}}).dump()<<'\n';log.flush();
}
// Default names. 801C3744 builds them from overlay tables: given names at 801C6C00
// and family names at 801C6C70, 7 codes per route, the partner 0x38 bytes after the
// protagonist, padded with code 0xCA (the blank the original also writes, 0x1549). A
// code is the name grid's character: text id 0x147F + code, an 8-byte header and one
// font glyph.
constexpr uint16_t grid_base=0x147F, grid_blank=0xCA;
constexpr uint32_t text_table=0x01A34980;
uint16_t grid_glyph(uint16_t code) {
    auto word=[](uint32_t p){return (uint32_t(rom_image[p])<<24)|(rom_image[p+1]<<16)|(rom_image[p+2]<<8)|rom_image[p+3];};
    const uint32_t entry=text_table+4+uint32_t(grid_base+code)*8;
    if(!rom_image || entry+4>rom_bytes)return 0xFFFF;
    const uint32_t at=text_table+word(entry);
    return at+10>rom_bytes?0xFFFF:uint16_t((rom_image[at+8]<<8)|rom_image[at+9]);
}
uint32_t name_table(unsigned route,bool partner,bool family) {
    return (family?0x1C6C70:0x1C6C00)+(partner?0x38:0)+route*14;
}
// A default as font glyphs; empty when a code has no single glyph.
std::vector<uint16_t> default_glyphs(const uint8_t* ram,uint32_t table) {
    std::vector<uint16_t> codes;
    for(unsigned i=0;i<7;++i)codes.push_back(half(ram,table+2*i));
    while(!codes.empty() && codes.back()==grid_blank)codes.pop_back();
    for(auto& code:codes)if((code=grid_glyph(code))==0xFFFF)return {};
    return codes;
}
std::u16string default_name(const uint8_t* ram,uint32_t table) {
    const auto glyphs=default_glyphs(ram,table);
    return glyphs.empty()?std::u16string():codec.decode(glyphs);
}
// The player no longer names anyone: both people get the route's defaults the way the
// original commits a name. Given name, family name and nickname go into the editor
// buffer 801C71E0, then the validator 801C474C for the person writes the name, family,
// nickname and full name fields. The nickname is the given name cut as 801C3744 cuts
// it: 5 codes, the protagonist of route 2 (アークライト → アーク) 3.
bool commit_defaults(uint8_t* ram,recomp_context* ctx,unsigned route) {
    for(unsigned p=0;p<2;++p) {
        std::array<uint16_t,21> editor{};
        const auto given=default_glyphs(ram,name_table(route,p,false)),family=default_glyphs(ram,name_table(route,p,true));
        if(given.empty() || family.empty())return false;
        const size_t nick=std::min<size_t>(given.size(),!p && route==2?3:5);
        std::copy(given.begin(),given.end(),editor.begin()+Codec::offsets[0]);
        std::copy(family.begin(),family.end(),editor.begin()+Codec::offsets[1]);
        std::copy(given.begin(),given.begin()+nick,editor.begin()+Codec::offsets[2]);
        for(unsigned i=0;i<editor.size();++i)half(ram,0x1C71E0+2*i,editor[i]);
        auto call=*ctx;call.r4=p;load_001090A0_func_801C474C(ram,&call);
        if(call.r2)return false;
    }
    return true;
}
void begin_selection(uint8_t* ram) {
    current.serial=++serial;current.person=Selection;current.route=half(ram,highlight)&3;
    for(unsigned route=0;route<4;++route)for(unsigned p=0;p<2;++p) {
        auto& choice=current.choices[route];
        choice.portraits[p]=portrait_paths[p][route];
        for(unsigned f=0;f<2;++f) {
            choice.names[p][f]=default_name(ram,name_table(route,p,f));
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
    // The name editors are never opened: both names are committed on selection.
    if(person!=Review)return;
    current.serial=++serial;current.person=person;current.route=half(ram,highlight)&3;
    const uint32_t bank[2][3]={{0x10F5F8,0x10F618,0x10F638},{0x10F608,0x10F628,0x10F644}};
    for(unsigned p=0;p<2;++p)for(unsigned f=0;f<3;++f) {
        std::vector<uint16_t> codes;
        for(unsigned i=0;i<Codec::limits[f];++i) {
            auto code=half(ram,bank[p][f]+2*i);if(code==0xFFFF)break;codes.push_back(code);
        }
        current.names[p][f]=codec.decode(codes);
    }
    current.visible=true;owning=true;
}
// The original selection page, chosen in the settings window: after its はい
// (801C52DC: page 1, the fade to the protagonist's name) the names are committed and
// the fade leaves for the story instead.
void original_selection(uint8_t* ram,recomp_context* ctx) {
    const uint32_t before=word(ram,page_state);
    srw64_original_name_selection_step(ram,ctx);
    if(before!=page_selection || word(ram,page_state)!=page_player)return;
    current.route=half(ram,highlight)&3;
    if(commit_defaults(ram,ctx,current.route)){ram[leave^3]=2;record("original-started");}
    else record("defaults-rejected");   // the original name pages follow
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned person) {
    if(!enabled || !ready || overlay!=0x1090A0)return false;
    std::lock_guard lock(mutex);
    if(closed)return true;
    auto call=*ctx;resident_func_80099B30(ram,&call);
    if(int8_t(call.r2)!=-1)return true; // Let the original fade finish first.
    if(!current.visible) {
        if(person!=Selection)return false;
        original_selection(ram,ctx);return true;
    }
    if(!current.active) {
        current.active=true;++current.revision;record("open");
    }
    if(person==Selection && cursor_moves) {
        cursor_moves=0;call=*ctx;call.r4=0xB9;resident_func_8007E8A8(ram,&call);
    }
    if(mini_stage::quick_start()) {
        // Debug mini-stage entry takes route 0 and starts the story with its names.
        if(person==Selection)current.route=0;
        review_confirm=true;current.pending=true;
        record("mini-stage-default");
    }
    if(!current.pending)return true;
    current.pending=false;
    uint32_t state=page_selection;
    if(person==Selection) {
        // The original's はい (801C52DC): a route other than the last confirmed one
        // resets both name templates and loads that route's defaults (801C3744).
        const uint16_t route=uint16_t(current.route);
        call=*ctx;call.r4=0xB7;resident_func_8007E8A8(ram,&call);
        half(ram,highlight,route);
        if(route!=half(ram,confirmed)) {
            for(unsigned i=0;i<30;++i){half(ram,0x1C6FB8+2*i,0x1549);half(ram,0x1C70B8+2*i,0x1549);}
            call=*ctx;load_001090A0_func_801C3744(ram,&call);
        }
        half(ram,confirmed,route);
        // Straight to the review; should the validator refuse a default, the original
        // name pages follow.
        const bool committed=commit_defaults(ram,ctx,route);
        state=committed?page_review:page_player;
        record(committed?"selected":"defaults-rejected");
    } else if(review_confirm) {
        ram[leave^3]=2;state=word(ram,page_state);record("started");
    } else record("back");   // to the selection
    word(ram,page_state,state);
    // Keep the page and input ownership through the guest fade/next init.
    current.active=false;closed=true;++current.revision;
    call=*ctx;call.r4=5;call.r5=1;call.r6=2;resident_func_80099814(ram,&call);
    return true;
}
}
DefaultNames& default_names() {static DefaultNames table;return table;}
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
    // The grid and text tables are the pinned JP ROM's.
    if(size!=0x2000000)throw std::runtime_error("Native name entry requires the pinned JP ROM");
    rom_image=rom;rom_bytes=size;ready=true;
}
void overlay_loaded(uint32_t rom,uint32_t ram,uint32_t size) {
    // The following intro loads at 801C4500, overlapping this overlay rather
    // than reusing its start address. Any code overlap ends name-page ownership.
    if(uint64_t(ram)>=0x801C6FB0ULL || uint64_t(ram)+size<=0x801C2600ULL)return;
    std::lock_guard lock(mutex);overlay=ram==0x801C2600?rom:0;current={};closed=false;owning=false;
}
bool owns_input() {return owning || window_owning;}
void window_claim_input(bool value) {window_owning=value;}
uint16_t input(uint16_t buttons) {held &= buttons;if(owns_input())held|=buttons;return buttons & ~held;}
Request request() {std::lock_guard lock(mutex);auto result=current;if(mini_stage::quick_start())result.visible=false;return result;}
std::u16string decode_glyphs(const std::vector<uint16_t>& codes) {return codec.decode(codes);}
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
