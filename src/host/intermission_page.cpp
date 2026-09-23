#include "intermission_page.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include <atomic>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::intermission_page {
namespace {
using namespace guest;
using json=nlohmann::json;
// Intermission overlay (load_0008F4B0) state.
constexpr uint32_t next_screen=0x801DECB8,exit_code=0x801DD540,cursor=0x801DD548,built=0x801DEC50,
    mode=0x801DECD8,swap_cursor=0x801DEC58,restricted_list=0x801DC6D4;
constexpr unsigned restricted_count=14;
// Resident: the transition state 80099B30 returns (-1 idle), the pressed buttons the
// menus read, and the campaign progress the menu shows.
constexpr uint32_t transition=0x8015E9C5,pressed=0x80178A08,turns=0x8010F5EC,funds=0x8010F5F4,
    episode=0x8010F5EF,scene=0x8010F5F1;
constexpr uint16_t button_a=0x8000;
constexpr unsigned sound_confirm=0xB7,sound_cancel=0xB8,sound_move=0xB9,swap_item=5;
// Text 0xFCC インターミッション, 0xFCD.. the nine items, 0xFD6 総ターン数<BR>資金,
// 0xFE0 クリア, 0xFEC パイロット, 0x1002 妖精; the cleared scene's title is 281 + scene.
constexpr uint16_t text_title=0xFCC,text_items=0xFCD,text_next=0xFD5,text_info=0xFD6,text_clear=0xFE0,
    text_pilot=0xFEC,text_fairy=0x1002,text_scene=281;

std::mutex mutex;
json current={{"visible",false}};
uint64_t serial{};
std::string pending,locale;
bool active{};
std::atomic_bool owning{},window_owning{};
uint16_t held{};
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.intermission-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
void call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0=0,uint32_t a1=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);function(ram,&c);
}
bool idle(const uint8_t* ram){return int8_t(read(ram,transition,1))==-1;}
void labels(const uint8_t* ram) {
    const bool restricted=current.value("restricted",false);
    json items=json::array();
    if(restricted)items={dialogue::ui_text(ram,text_items),dialogue::ui_text(ram,text_next)};
    else for(unsigned n=0;n<9;++n)items.push_back(dialogue::ui_text(ram,text_items+n));
    const auto info=dialogue::ui_text(ram,text_info);const auto line=info.find('\n');
    current["title"]=dialogue::ui_text(ram,text_title);current["items"]=items;
    current["turns_label"]=info.substr(0,line);current["funds_label"]=line==std::string::npos?"":info.substr(line+1);
    current["scene_title"]=dialogue::ui_text(ram,text_scene+current.value("scene",0u));
    current["clear_label"]=dialogue::ui_text(ram,text_clear);
    current["swap_items"]={dialogue::ui_text(ram,text_pilot),dialogue::ui_text(ram,text_fairy)};
    locale=localization::catalog().locale;
}
void hide(const char* reason) {
    if(!active)return;
    active=false;owning=false;pending.clear();current["visible"]=false;record(reason);
}
// 801CDFB0 without its drawing: the two-item menu after a （前） scene, the dimmed
// background (its random call included) and the built flag.
bool build(uint8_t* ram,recomp_context* ctx) {
    uint8_t list[restricted_count];
    for(unsigned n=0;n<restricted_count;++n)list[n]=uint8_t(read(ram,restricted_list+n,1));
    const auto cleared=uint8_t(read(ram,scene,1));
    const bool restricted=restricted_scene(list,restricted_count,cleared);
    write32(ram,mode,restricted?10:0);
    call(ram,ctx,resident_func_80085B94,0,1);
    write32(ram,built,1);
    std::lock_guard lock(mutex);
    current={{"visible",true},{"serial",++serial},{"restricted",restricted},{"cursor",int16_t(read(ram,cursor,2))},
        {"turns",read(ram,turns,2)},{"funds",read(ram,funds,4)},{"episode",read(ram,episode,1)},{"scene",cleared},
        {"submenu",false},{"swap_cursor",0},{"swap_refused",false}};
    labels(ram);active=true;owning=true;pending.clear();
    record("open",{{"restricted",restricted},{"scene",cleared}});
    return true;
}
// One A edge for the original step, which validates, plays the sound, sets the next
// screen or the exit code and starts the fade. True when it did start one.
bool confirm(uint8_t* ram,recomp_context* ctx) {
    const auto old=uint16_t(read(ram,pressed,2));
    write16(ram,pressed,button_a);
    auto c=*ctx;srw64_original_intermission_menu_step(ram,&c);
    write16(ram,pressed,old);
    return !idle(ram);
}
bool step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(!active)return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;   // an answer waits for the fade-in to end
    const auto action=std::move(pending);pending.clear();
    if(uint32_t value;parse_funds(action,value)) {
        write32(ram,funds,value);current["funds"]=value;record("funds",{{"funds",value}});return false;
    }
    const auto number=[&](size_t prefix){return action.size()==prefix+1 && action[prefix]>='0' && action[prefix]<='9'?unsigned(action[prefix]-'0'):99u;};
    const unsigned count=current.value("restricted",false)?2:9;
    const bool submenu=current.value("submenu",false);
    if(action.starts_with("move:") && !submenu) {
        const auto index=number(5);
        if(index>=count || index==current.value("cursor",0u))return false;
        write16(ram,cursor,uint16_t(index));current["cursor"]=index;current["swap_refused"]=false;
        lock.unlock();call(ram,ctx,resident_func_8007E8A8,sound_move);return false;
    }
    if(action.starts_with("choose:") && !submenu) {
        const auto index=number(7);
        if(index>=count)return false;
        write16(ram,cursor,uint16_t(index));current["cursor"]=index;
        if(count==9 && index==swap_item) {
            // The original opens its パイロット/妖精 window here; the page shows its own.
            current["submenu"]=true;current["swap_cursor"]=0;current["swap_refused"]=false;record("swap-open");
            lock.unlock();call(ram,ctx,resident_func_8007E8A8,sound_confirm);return false;
        }
        lock.unlock();
        // The step derives the next screen from the cursor before it reads A.
        const bool left=confirm(ram,ctx);
        lock.lock();record("choose",{{"index",index},{"left",left},{"next",read(ram,next_screen,4)},{"exit",read(ram,exit_code,1)}});
        if(left)hide("close");
        return true;
    }
    if(action.starts_with("swap-move:") && submenu) {
        const auto index=number(10);
        if(index>1 || index==current.value("swap_cursor",0u))return false;
        current["swap_cursor"]=index;current["swap_refused"]=false;
        lock.unlock();call(ram,ctx,resident_func_8007E8A8,sound_move);return false;
    }
    if(action=="swap-close" && submenu) {
        current["submenu"]=false;current["swap_refused"]=false;record("swap-close");
        lock.unlock();call(ram,ctx,resident_func_8007E8A8,sound_cancel);return false;
    }
    if(action.starts_with("swap:") && submenu) {
        const auto index=number(5);
        if(index>1)return false;
        current["swap_cursor"]=index;
        lock.unlock();
        // The window's own A branch (801CE45C): it lists the candidates, refuses with
        // the buzzer when there are none, else picks screen 6 or 20 and fades.
        write32(ram,next_screen,swap_item+1);write32(ram,mode,1);write16(ram,swap_cursor,uint16_t(index));
        const bool left=confirm(ram,ctx);
        if(!left)write32(ram,mode,0);
        lock.lock();record("swap",{{"index",index},{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");else current["swap_refused"]=true;
        return true;
    }
    return false;
}
// The soft reset is decided by the screen dispatcher, not by the menu step.
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(active && read(ram,exit_code,1))hide("exit");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_INTERMISSION");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original menu.
    log.open(directory/"intermission-events.jsonl");
    srw64_game_hooks.intermission_build=build;srw64_game_hooks.intermission_step=step;srw64_game_hooks.intermission_frame=frame;
}
json state(){std::lock_guard lock(mutex);return current;}
void answer(uint64_t id,const std::string& action) {
    std::lock_guard lock(mutex);
    if(current.value("visible",false) && current.value("serial",uint64_t{})==id && pending.empty())pending=action;
}
bool owns_input(){return owning || window_owning;}
void window_claim_input(bool value){window_owning=value;}
uint16_t input(uint16_t buttons){return filter_input(buttons,held,owns_input());}
}
