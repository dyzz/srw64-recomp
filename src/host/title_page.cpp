#include "title_page.hpp"
#include "save_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_menu.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::title_page {
namespace {
using namespace guest;
using json=nlohmann::json;
// Title overlay (load_0010DA50, docs/native/native-title-menus.md). Major state
// D_801CC3A6: 5 オプション, 8 サウンドセレクト, 11 カラオケモード; substate D_801CC3A7
// (lists: 0 the songs, 1 EXIT, 2 the fade into the カラオケ battle).
constexpr uint32_t major_state=0x801CC3A6,sub_state=0x801CC3A7;
// オプション (801C697C / 801C6B14): the cursor D_801CC397 over サウンド, サウンドセレクト,
// カラオケモード. サウンド flips bit 0 of the settings byte D_8015DDA8 (1 = mono), clears
// bit 2, writes the SRAM header (8009187C) and sets the output (80076D90).
constexpr uint32_t option_cursor=0x801CC397,settings_byte=0x8015DDA8;
constexpr uint32_t write_settings=0x8009187C,sound_output=0x80076D90;
constexpr unsigned option_count=3;
constexpr uint16_t text_options=0xE1,text_sound=0x3EF,text_stereo=0x3F0,text_mono=0x3F1,text_sound_select=0xE2,text_karaoke=0xE5,text_exit=0xE6;
// Song lists (801C81C0 builds them: count, current, row 0-9 in the window, first shown,
// then the table indexes the player may pick). Tables of {text, song}: サウンドセレクト
// D_801CB290 (49), カラオケモード D_801CB354 (19). The playing song is D_800FFA6C.
constexpr uint32_t list_count=0x801CC190,list_current=0x801CC192,list_row=0x801CC194,list_top=0x801CC196,list_items=0x801CC198;
constexpr uint32_t sound_table=0x801CB290,karaoke_table=0x801CB354,playing=0x800FFA6C;
constexpr unsigned list_rows=10,sound_songs=49,karaoke_songs=19;
constexpr uint32_t release_panels=0x801C4850,build_list=0x801C81C0,play_music=0x8007E810,free_sprite=0x8008B888;
constexpr uint32_t arrow_slot=0x2F,slot_records=0x800FFA70,slot_size=0xC4;
// The three input words the title reads: pressed this frame, auto-repeat, held.
constexpr uint32_t pressed=0x80178A08,repeat=0x801612E0;
constexpr uint16_t button_a=0x8000,button_b=0x4000,button_up=0x800,button_down=0x400,button_zl=0x2020,button_r=0x10;
constexpr unsigned sound_move=0xB9;

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
    json row={{"schema","srw64.title-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
uint32_t call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);LOOKUP_FUNC(function)(ram,&c);
    return uint32_t(c.r2);
}
// Runs the original step with the page's button in the word it reads.
void feed(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*),uint32_t word,uint16_t button) {
    const auto old=uint16_t(read(ram,word,2));
    write16(ram,word,button);
    auto c=*ctx;step(ram,&c);
    write16(ram,word,old);
}
std::string text(const uint8_t* ram,uint16_t id){return dialogue::ui_text(ram,id);}
void hide(const char* reason) {
    if(!active)return;
    active=false;owning=false;pending.clear();current={{"visible",false}};record(reason);
}
void open(json page) {
    page["visible"]=true;page["serial"]=++serial;
    current=std::move(page);active=true;owning=true;pending.clear();locale=localization::catalog().locale;
    record("open",{{"screen",current.value("screen",std::string())}});
}
void republish(json next){next["visible"]=true;next["serial"]=current["serial"];current=std::move(next);}

// --- オプション -------------------------------------------------------------------------

json options_json(const uint8_t* ram) {
    const bool mono=read(ram,settings_byte,1)&1;
    return {{"screen","options"},{"title",text(ram,text_options)},{"cursor",std::min<unsigned>(read(ram,option_cursor,1),option_count-1)},
        {"items",json::array({{{"label",text(ram,text_sound)},{"value",text(ram,mono?text_mono:text_stereo)}},{{"label",text(ram,text_sound_select)}},{{"label",text(ram,text_karaoke)}}})},
        {"mono",mono}};
}
bool options_build(uint8_t* ram,recomp_context* ctx) {
    // 801C697C frees the ring (slots 2-6) and the flames (9), then draws layout 0x5A.
    for(uint32_t slot:{2u,3u,4u,5u,6u,9u})call(ram,ctx,free_sprite,slot);
    std::lock_guard lock(mutex);
    open(options_json(ram));
    return true;
}
bool options_step(uint8_t* ram,recomp_context* ctx,void(*original)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="options")return false;
    if(locale!=localization::catalog().locale){republish(options_json(ram));locale=localization::catalog().locale;}
    const auto action=std::move(pending);pending.clear();
    const unsigned cursor=read(ram,option_cursor,1);
    if(action.starts_with("move:")) {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>=option_count || index==cursor)return true;
        write8(ram,option_cursor,uint8_t(index));republish(options_json(ram));
        lock.unlock();call(ram,ctx,0x8007E8A8,sound_move);return true;
    }
    if(action=="choose" && cursor==0) {
        // 801C6B14's サウンド branch without its label: the page shows the value.
        call(ram,ctx,0x8007E8A8,0xB7);
        const uint8_t value=uint8_t((read(ram,settings_byte,1)^1)&0xFB);
        write8(ram,settings_byte,value);
        call(ram,ctx,write_settings);
        call(ram,ctx,sound_output,value&1);
        republish(options_json(ram));record("sound",{{"mono",bool(value&1)}});
        return true;
    }
    if(action=="choose" || action=="back") {
        // サウンドセレクト / カラオケモード build their lists (taken below); B fades back to the ring.
        record(action=="back"?"back":"choose",{{"cursor",cursor}});
        lock.unlock();feed(ram,ctx,original,pressed,action=="back"?button_b:button_a);
        return true;
    }
    return true;
}

// --- サウンドセレクト / カラオケモード ---------------------------------------------------

json list_json(const uint8_t* ram,bool karaoke) {
    const unsigned count=std::min<unsigned>(read(ram,list_count,2),karaoke?karaoke_songs:sound_songs);
    const uint32_t table=karaoke?karaoke_table:sound_table;
    const unsigned now=read(ram,playing,2);
    json songs=json::array();
    for(unsigned n=0;n<count;++n) {
        const unsigned index=read(ram,list_items+2*n,2);
        const unsigned song=read(ram,table+4*index+2,2);
        songs.push_back({{"text",text(ram,uint16_t(read(ram,table+4*index,2)))},{"song",song},{"playing",!karaoke && song==now}});
    }
    return {{"screen",karaoke?"karaoke":"sound"},{"title",text(ram,karaoke?text_karaoke:text_sound_select)},{"exit",text(ram,text_exit)},
        {"songs",songs},{"current",read(ram,list_current,2)},{"top",read(ram,list_top,2)},{"rows",list_rows},
        {"exit_focus",read(ram,sub_state,1)==1}};
}
bool list_build(uint8_t* ram,recomp_context* ctx,bool karaoke) {
    // 801C86A8 / 801C9888 without the layouts, the drawn list and the cursor.
    call(ram,ctx,release_panels);
    call(ram,ctx,play_music,uint32_t(-1));
    call(ram,ctx,build_list,karaoke?1:0);
    std::lock_guard lock(mutex);
    open(list_json(ram,karaoke));
    return true;
}
// The list steps draw only through 801C83D0 (a no-op here) and the arrow layouts in
// slot 0x2F, which go again after every step.
void clear_arrows(uint8_t* ram,recomp_context* ctx) {
    if(read(ram,slot_records+arrow_slot*slot_size+1,1))call(ram,ctx,free_sprite,arrow_slot);
}
bool list_step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    const bool karaoke=screen==srw64_title_karaoke || screen==srw64_title_karaoke_exit;
    const bool exit=screen==srw64_title_sound_exit || screen==srw64_title_karaoke_exit;
    std::unique_lock lock(mutex);
    const auto kind=current.value("screen",std::string());
    if(!active || kind!=(karaoke?"karaoke":"sound"))return false;
    auto action=std::move(pending);pending.clear();
    if(action=="exit" && !exit) {
        // EXIT as a click: focus it now, confirm on its own step next frame.
        write8(ram,sub_state,1);pending="choose";
        republish(list_json(ram,karaoke));
        lock.unlock();call(ram,ctx,0x8007E8A8,sound_move);return true;
    }
    if(action.starts_with("jump:") && !exit) {
        const unsigned count=read(ram,list_count,2),target=unsigned(std::atoi(action.c_str()+5));
        if(target>=count)return true;
        if(target==read(ram,list_current,2))action="choose";
        else {
            // The original's window: ten rows, the cursor row kept inside it.
            unsigned top=read(ram,list_top,2);
            if(target<top)top=target;
            else if(target>=top+list_rows)top=target-list_rows+1;
            write16(ram,list_current,uint16_t(target));write16(ram,list_top,uint16_t(top));write16(ram,list_row,uint16_t(target-top));
            republish(list_json(ram,karaoke));
            lock.unlock();call(ram,ctx,0x8007E8A8,sound_move);return true;
        }
    }
    uint32_t word=pressed;uint16_t button=0;
    if(action=="move:-1"){word=repeat;button=button_up;}
    else if(action=="move:+1"){word=repeat;button=button_down;}
    else if(action=="choose")button=button_a;
    else if(action=="back")button=button_b;
    else if(action=="prev" && !karaoke && !exit)button=button_zl;
    else if(action=="next" && !karaoke && !exit)button=button_r;
    lock.unlock();
    // No button: the step still runs (it tracks whether a song is playing for B).
    feed(ram,ctx,original,word,button);
    clear_arrows(ram,ctx);
    lock.lock();
    if(button)record("button",{{"action",action},{"current",read(ram,list_current,2)},{"sub",read(ram,sub_state,1)}});
    if(active && current.value("screen",std::string())==kind)republish(list_json(ram,karaoke));
    return true;
}

bool build(uint8_t* ram,recomp_context* ctx,unsigned screen) {
    switch(screen) {
        case srw64_title_medium: case srw64_title_slots: case srw64_title_message:
            return save_page::title_screen_build(ram,ctx,screen);
        case srw64_title_options:
            return settings::native_title_ui() && options_build(ram,ctx);
        case srw64_title_sound: case srw64_title_karaoke:
            return settings::native_title_ui() && list_build(ram,ctx,screen==srw64_title_karaoke);
        case srw64_title_list_draw: {
            // The page draws the list. 801C8520 calls this whenever the list moves, also
            // while the カラオケ return walks the cursor to the song during the fade-in,
            // when no step runs: the page follows it here.
            if(!settings::native_title_ui())return false;
            std::lock_guard lock(mutex);
            const auto kind=current.value("screen",std::string());
            if(active && (kind=="sound" || kind=="karaoke"))republish(list_json(ram,kind=="karaoke"));
            return true;
        }
        default: return false;
    }
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    switch(screen) {
        case srw64_title_medium: case srw64_title_slots: case srw64_title_confirm: case srw64_title_message:
            return save_page::title_screen_step(ram,ctx,screen,original);
        case srw64_title_options: return options_step(ram,ctx,original);
        default: return list_step(ram,ctx,screen,original);
    }
}
void frame(uint8_t* ram) {
    save_page::title_screen_frame(ram);
    std::lock_guard lock(mutex);
    if(!active)return;
    const unsigned major=read(ram,major_state,1);
    const auto kind=current.value("screen",std::string());
    if((kind=="options" && major!=5) || (kind=="sound" && major!=8) || (kind=="karaoke" && major!=11))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    log.open(directory/"title-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.title_build=build;h.title_step=step;h.title_frame=frame;
}
json state(){std::lock_guard lock(mutex);return current;}
void answer(uint64_t id,const std::string& action) {
    std::lock_guard lock(mutex);
    if(current.value("visible",false) && current.value("serial",uint64_t{})==id && pending.empty())pending=action;
}
bool owns_input(){return owning || window_owning;}
void window_claim_input(bool value){window_owning=value;}
uint16_t input(uint16_t buttons){return intermission_page::filter_input(buttons,held,owns_input());}
void overlay_loaded(uint32_t,uint32_t ram,uint32_t size) {
    // The title overlay (RAM 801C4500) replaced: the load, カラオケ or コンティニュー.
    if(uint64_t(ram)<0x801CC150ULL && uint64_t(ram)+size>0x801C4500ULL) {
        {std::lock_guard lock(mutex);hide("overlay");}
        save_page::title_overlay_changed();
    }
}
}
