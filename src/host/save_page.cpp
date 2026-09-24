#include "save_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_page.hpp"
#include "native_dialogue.hpp"
#include "native_name_entry.hpp"
#include "localization/catalog.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::save_page {
namespace {
using namespace guest;
using json=nlohmann::json;
// Intermission overlay state (load_0008F4B0), as upgrade_page.cpp. D_801DECD8 is the
// choice screen's countdown and the slot screen's mode (0 list, 1 overwrite window,
// 2 message).
constexpr uint32_t next_screen=0x801DECB8,current_screen=0x801DECCC,mode=0x801DECD8,transition=0x8015E9C5,pressed=0x80178A08;
// Screen 1 (801CEA30 / 801CEABC): the medium cursor D_801DEBC8 (0 ROM, 1 Controller
// Pak); A sets D_801DDA30 and a 30-frame countdown, then screen 9 with the slot cursor
// D_801DD0A2 cleared.
constexpr uint32_t medium=0x801DEBC8,message_shown=0x801DDA30,slot_cursor=0x801DD0A2;
constexpr unsigned wait_frames=30;
// Screen 9 (801CECE8 / 801CEEF8): 80085CD4(medium, D_801DD118) reads the two slot
// headers (0x18 each: +0 used, +2 six name glyphs, +0xE protagonist, +0xF level,
// +0x10 episode, +0x11 title, +0x12 turns, +0x14 funds); the Controller Pak status
// D_801DD116 (80094168; 2 and up is a message), the はい／いいえ cursor D_801DDA08.
constexpr uint32_t slots=0x801DD118,slot_size=0x18,pak_status=0x801DD116,window_cursor=0x801DDA08,pak_control=0x8015F508;
constexpr unsigned slot_count=2;
constexpr uint32_t read_slots=0x80085CD4,write_sram=0x80092678,write_pak=0x80093FD4,pak_check=0x80094168,pak_removed=0x8009412C,pak_poll=0x800906A0,pak_repair=0x80090844;
// Protagonist portraits D_801DC680: resource 0x51C+class with palette 0x520+class — the
// name-entry faces 25+class of the profile (アークライト, セレイン, ブラッド, マナミ).
constexpr unsigned portrait_face=25;
constexpr uint16_t text_rom=0xFD9,text_pak=0xFDA,text_save_to=0xFDC,text_checking=0xFDD,text_slot=0xFDE,text_level=0xFE1,text_episode=0xFD7,text_clear=0xFE0,
    text_turns=0x101B,text_funds=0x1019,text_overwrite=0xFE3,text_ask=0xFE4,text_yes=0xFE5,text_no=0xFE6,text_titles=0x119;
constexpr uint16_t glyph_blank=0x1549;
constexpr unsigned sound_move=0xB9,sound_confirm=0xB7,sound_cancel=0xB8;
constexpr uint16_t button_b=0x4000;

std::mutex mutex;
json current={{"visible",false}};
json art;
uint64_t serial{};
std::string pending,locale;
bool active{};
std::atomic_bool owning{},window_owning{};
uint16_t held{};
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.save-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
uint32_t call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);function(ram,&c);
    return uint32_t(c.r2);
}
uint32_t call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0){return call(ram,ctx,LOOKUP_FUNC(function),a0,a1);}
bool idle(const uint8_t* ram){return int8_t(read(ram,transition,1))==-1;}
void sound(uint8_t* ram,recomp_context* ctx,unsigned id){call(ram,ctx,resident_func_8007E8A8,id);}
void fade_out(uint8_t* ram,recomp_context* ctx,unsigned to){write32(ram,next_screen,to);call(ram,ctx,resident_func_80099814,5,1,2);}
bool feed(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*),uint16_t button) {
    const auto old=uint16_t(read(ram,pressed,2));
    write16(ram,pressed,button);
    auto c=*ctx;step(ram,&c);
    write16(ram,pressed,old);
    return !idle(ram);
}
void hide(const char* reason) {
    if(!active)return;
    active=false;owning=false;pending.clear();current["visible"]=false;record(reason);
}
std::string text(const uint8_t* ram,uint16_t id){return dialogue::ui_text(ram,id);}
void labels(const uint8_t* ram) {
    const std::pair<const char*,uint16_t> keys[]={{"rom",text_rom},{"pak",text_pak},{"save_to",text_save_to},{"checking",text_checking},{"slot",text_slot},{"level",text_level},
        {"episode",text_episode},{"clear",text_clear},{"turns",text_turns},{"funds",text_funds},{"overwrite",text_overwrite},{"ask",text_ask},{"yes",text_yes},{"no",text_no}};
    json l;
    for(const auto& [key,id]:keys)l[key]=text(ram,id);
    current["labels"]=l;
    locale=localization::catalog().locale;
}
void open(json page,const uint8_t* ram) {
    page["visible"]=true;page["serial"]=++serial;
    current=std::move(page);labels(ram);
    active=true;owning=true;pending.clear();
    record("open",{{"screen",current.value("screen",std::string())},{"mode",current.value("mode",0u)}});
}
bool original_screens() {
    if(settings::native_intermission_ui())return false;
    std::lock_guard lock(mutex);hide("original");return true;
}
void republish(json next) {
    next["visible"]=true;next["serial"]=current["serial"];next["labels"]=current["labels"];
    current=std::move(next);
}

json slots_json(const uint8_t* ram);
// A language switch while a screen is open: slot titles and pak messages are
// rebuilt in the new language along with the labels.
void relocalize(const uint8_t* ram) {
    if(locale==localization::catalog().locale)return;
    if(current.value("screen",std::string())=="slots")republish(slots_json(ram));
    labels(ram);
}

// --- Medium choice (1) ---------------------------------------------------------------

json choice_json(const uint8_t* ram) {
    return {{"screen","choice"},{"cursor",std::min<unsigned>(read(ram,medium,2),1)},{"waiting",read(ram,message_shown,4)!=0}};
}
bool choice_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    write32(ram,message_shown,0);write32(ram,mode,0);
    std::lock_guard lock(mutex);
    open(choice_json(ram),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool choice_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="choice")return false;
    relocalize(ram);
    // Once the message is up the original step counts D_801DECD8 down and leaves for
    // screen 9 without drawing; before that it only animates a cursor the page owns.
    if(pending.empty() || !idle(ram) || read(ram,message_shown,4))return false;
    const auto action=std::move(pending);pending.clear();
    if(action.starts_with("move:")) {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>1 || index==read(ram,medium,2))return false;
        write16(ram,medium,uint16_t(index));republish(choice_json(ram));
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose") {
        write32(ram,message_shown,1);write32(ram,mode,wait_frames);
        republish(choice_json(ram));record("choose",{{"medium",read(ram,medium,2)}});
        lock.unlock();sound(ram,ctx,sound_confirm);return true;
    }
    if(action=="back") {
        lock.unlock();
        const bool left=feed(ram,ctx,step,button_b);
        lock.lock();record("back",{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

// --- Slots (9) -----------------------------------------------------------------------

json slot_json(const uint8_t* ram,unsigned n) {
    const uint32_t rec=slots+n*slot_size;
    json s={{"index",n},{"used",read(ram,rec,1)!=0}};
    if(!s["used"].get<bool>())return s;
    std::vector<uint16_t> codes;
    for(unsigned i=0;i<6;++i){const auto code=uint16_t(read(ram,rec+2+2*i,2));if(code==0xFFFF)break;codes.push_back(code);}
    while(!codes.empty() && codes.back()==glyph_blank)codes.pop_back();
    std::u16string name;
    for(const auto code:codes){const auto glyph=names::decode_glyphs({code});name+=glyph.empty()?u"?":glyph;}
    const unsigned protagonist=read(ram,rec+0xE,1);
    s["name"]=dialogue::utf8(name);s["protagonist"]=protagonist;
    s["level"]=read(ram,rec+0xF,1);s["episode"]=read(ram,rec+0x10,1);s["title"]=text(ram,uint16_t(text_titles+read(ram,rec+0x11,1)));
    s["turns"]=read(ram,rec+0x12,2);s["funds"]=read(ram,rec+0x14,4);
    const auto face=std::to_string(portrait_face+protagonist);
    if(art.contains("portraits") && art.at("portraits").contains(face) && art.at("portraits").at(face).contains("original")) {
        const auto& row=art.at("portraits").at(face);
        s["art"]={{"path",row.at("original")},{"width",64},{"height",64}};
        if(row.contains("hd"))s["art"]["hd"]=row.at("hd");  // whole HD portrait, shown in HD image mode
    }
    return s;
}
// 801CE578: the Controller Pak messages by status, each text at its original position.
json message_json(const uint8_t* ram,unsigned status) {
    struct Line{uint16_t id;int x,y;};
    std::vector<Line> lines;
    switch(status) {
        case 2: case 3: lines={{0x1A8,80,86},{0x1AA,80,106},{0x1AE,104,126},{0x1AF,72,156}};break;
        case 4: lines={{0x1BC,80,86},{0x1BD,192,86},{0x1BE,47,106},{0x1BF,183,106},{0x1AF,80,156}};break;
        case 5: lines={{0x1B7,113,86},{0x1BA,59,106},{0x1BB,163,106},{0x1AF,80,156}};break;
        case 6: lines={{0x1B3,57,86},{0x1B4,187,86},{0x1C0,96,106},{0x1AE,112,126},{0x1B0,80,156}};break;
        case 7: lines={{0x1A9,96,82},{0x1B8,36,98},{0x1B9,212,98},{0x1B2,120,120},{0x1B5,64,136},{0x1B6,156,136},{0x1AF,72,160}};break;
        default: break;
    }
    json out=json::array();
    for(const auto& line:lines)out.push_back({{"text",text(ram,line.id)},{"x",line.x},{"y",line.y}});
    return out;
}
json slots_json(const uint8_t* ram) {
    const unsigned m=read(ram,mode,4),status=read(ram,pak_status,1);
    json page={{"screen","slots"},{"medium",std::min<unsigned>(read(ram,medium,2),1)},{"cursor",std::min<unsigned>(read(ram,slot_cursor,2),slot_count-1)},{"mode",m},
        {"window_cursor",std::min<unsigned>(read(ram,window_cursor,2),1)},{"status",status},{"slots",json::array()}};
    for(unsigned n=0;n<slot_count;++n)page["slots"].push_back(slot_json(ram,n));
    if(m==2)page["message"]=message_json(ram,status);
    return page;
}
bool slots_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    const unsigned m=read(ram,medium,2);
    write32(ram,mode,0);write16(ram,window_cursor,0);
    bool message=false;
    if(m) {
        const unsigned status=call(ram,ctx,pak_check,0);
        write8(ram,pak_status,uint8_t(status));
        if(status>=2){write32(ram,mode,2);message=true;}
    }
    if(!message)call(ram,ctx,read_slots,m,slots);
    std::lock_guard lock(mutex);
    open(slots_json(ram),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
// The original A path: SRAM or Controller Pak write, then the screen again or the
// message when the pak status is non-zero.
void write_slot(uint8_t* ram,recomp_context* ctx,unsigned slot) {
    write8(ram,pak_status,0);
    if(read(ram,medium,2)==0)call(ram,ctx,write_sram,slot);
    else {
        const unsigned status=call(ram,ctx,pak_check,0);
        write8(ram,pak_status,uint8_t(status));
        if(status<2)call(ram,ctx,write_pak,slot);
    }
    const unsigned status=read(ram,pak_status,1);
    record("write",{{"slot",slot},{"medium",read(ram,medium,2)},{"status",status}});
    if(status==0)fade_out(ram,ctx,9);
    else write32(ram,mode,2);
}
bool slots_step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="slots")return false;
    relocalize(ram);
    if(!idle(ram))return true;
    const unsigned m=read(ram,medium,2);
    unsigned state=read(ram,mode,4);
    if(state==2)call(ram,ctx,pak_poll);
    else if(m && call(ram,ctx,pak_removed)) {
        write8(ram,pak_status,2);write32(ram,mode,2);state=2;
        republish(slots_json(ram));record("pak-removed");
    }
    if(pending.empty())return true;
    const auto action=std::move(pending);pending.clear();
    const unsigned cursor=read(ram,slot_cursor,2);
    if(state==0) {
        if(action.starts_with("move:")) {
            const unsigned index=unsigned(std::atoi(action.c_str()+5));
            if(index>=slot_count || index==cursor)return true;
            write16(ram,slot_cursor,uint16_t(index));republish(slots_json(ram));
            lock.unlock();sound(ram,ctx,sound_move);return true;
        }
        if(action=="choose") {
            sound(ram,ctx,sound_confirm);
            if(read(ram,slots+cursor*slot_size,1)){write32(ram,mode,1);write16(ram,window_cursor,0);record("window-open",{{"slot",cursor}});}
            else write_slot(ram,ctx,cursor);
            republish(slots_json(ram));return true;
        }
        if(action=="back"){sound(ram,ctx,sound_cancel);fade_out(ram,ctx,1);record("back");return true;}
        return true;
    }
    if(state==1) {
        if(action.starts_with("move:")) {
            const unsigned index=unsigned(std::atoi(action.c_str()+5));
            if(index>1 || index==read(ram,window_cursor,2))return true;
            write16(ram,window_cursor,uint16_t(index));republish(slots_json(ram));
            lock.unlock();sound(ram,ctx,sound_move);return true;
        }
        if(action=="choose" && read(ram,window_cursor,2)==0){sound(ram,ctx,sound_confirm);write_slot(ram,ctx,cursor);republish(slots_json(ram));return true;}
        if(action=="choose" || action=="cancel"){sound(ram,ctx,action=="choose"?sound_confirm:sound_cancel);write32(ram,mode,0);republish(slots_json(ram));record("window-close");return true;}
        return true;
    }
    if(action=="back"){fade_out(ram,ctx,1);record("back");return true;}
    if(action=="choose") {
        // A on the message: repair (status 7) or check again; the screen restarts
        // when the pak is fine or the status changed.
        const unsigned before=read(ram,pak_status,1);
        const unsigned status=before==7?call(ram,ctx,pak_repair,pak_control,0):call(ram,ctx,pak_check,0);
        record("recheck",{{"before",before},{"status",status}});
        if(status<2)fade_out(ram,ctx,9);
        else if(status!=before){write8(ram,pak_status,uint8_t(status));fade_out(ram,ctx,9);}
        return true;
    }
    return true;
}

bool build(uint8_t* ram,recomp_context* ctx,unsigned screen) {
    if(original_screens())return false;
    switch(screen) {
        case 1: return choice_build(ram,ctx);
        case 9: return slots_build(ram,ctx);
        default: return false;
    }
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    switch(screen) {
        case 1: return choice_step(ram,ctx,original);
        case 9: return slots_step(ram,ctx);
        default: return false;
    }
}
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(active && !idle(ram) && read(ram,next_screen,4)!=read(ram,current_screen,4))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_SAVE");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original screens.
    std::ifstream source(std::getenv("SRW64_DIALOGUE_DATA"));
    const auto data=json::parse(source,nullptr,false);
    if(!data.is_discarded() && data.contains("name_entry_assets"))art=data.at("name_entry_assets");
    log.open(directory/"save-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.save_build=build;h.save_step=step;h.save_frame=frame;
}
json state(){std::lock_guard lock(mutex);return current;}
void answer(uint64_t id,const std::string& action) {
    std::lock_guard lock(mutex);
    if(current.value("visible",false) && current.value("serial",uint64_t{})==id && pending.empty())pending=action;
}
bool owns_input(){return owning || window_owning;}
void window_claim_input(bool value){window_owning=value;}
uint16_t input(uint16_t buttons){return intermission_page::filter_input(buttons,held,owns_input());}
}
