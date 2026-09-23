#include "swap_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_menu.hpp"
#include "intermission_page.hpp"
#include "upgrade_rules.hpp"
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
namespace srw64::swap_page {
namespace {
using namespace guest;
using json=nlohmann::json;
namespace up=srw64::upgrades;
// Intermission overlay state (load_0008F4B0), as upgrade_page.cpp.
constexpr uint32_t next_screen=0x801DECB8,current_screen=0x801DECCC,mode=0x801DECD8,transition=0x8015E9C5,pressed=0x80178A08;
// Nine-row lists (screens 6 and 20): 801CA75C fills D_801DD3A8 with the pilots that can
// change machines (801C5618, sorted by 801C58BC), 801CAC0C fills D_801DD550 with the
// fairies (801C60DC); count D_801DD0A0, 1-based page D_801DD14A, row D_801DEBC8 (copy
// D_801DD53A); D_801DEC5C is the pilot (fairy) index the next screens show.
constexpr uint32_t list_count=0x801DD0A0,pilot_list=0x801DD3A8,fairy_list=0x801DD550,list_row=0x801DEBC8,list_row_copy=0x801DD53A,list_page=0x801DD14A,screen_subject=0x801DEC5C;
constexpr unsigned list_rows=9;
constexpr uint32_t build_pilots=0x801CA75C,build_fairies=0x801CAC0C;
// Seven-row target lists (screens 16 and 21): 801CA824 (machines, 801C59AC) and 801CB1A8
// (pilots, 801C5C00) fill D_801DCF02 (count D_801DD0A4) on 1-based pages D_801DD544
// (pages D_801DD208, rows on the last D_801DEBD0), row D_801DEC58; the arrays are indexed
// by the 1-based page, so page 1 starts at D_801DCF10; D_801DD3A0 is the
// target under the cursor (a unit slot on 16, a pilot index on 21).
constexpr uint32_t target_list=0x801DCF10,target_count=0x801DD0A4,target_page=0x801DD544,target_row=0x801DEC58,target=0x801DD3A0;
constexpr unsigned target_rows=7;
constexpr uint32_t build_targets=0x801CA824,build_fairy_targets=0x801CB1A8;
// Confirm (17) and the fairy window (21): はい／いいえ cursor D_801DD0A2; 801D2B64 keeps
// the current machine's 運動性 in D_801DECBC for the second column.
constexpr uint32_t confirm_cursor=0x801DD0A2,current_mobility=0x801DECBC,description_count=0x801DD5CC;
constexpr uint32_t display_stats=0x801C4DE4,getter_unit=0x801C924C,getter_form=0x801C8E50;
constexpr uint32_t display_hp=0x801DEB08,display_mobility=0x801DD14E,display_limit=0x801DEB04,air_override=0x801DDA06;
// Pilot records (0x80172F40, 0x4C each) and unit fields, as ability_page.cpp.
constexpr uint32_t pilots=0x80172F40,pilot_size=0x4C,pilot_number=0x02,pilot_flags=0x04,pilot_level=0x05,pilot_evade=0x26,pilot_hit=0x28,pilot_terrain=0x2E,pilot_has_unit=0x37,pilot_unit_field=0x38;
constexpr unsigned pilot_count=100;
constexpr uint32_t unit_terrain=0x16,unit_crew=0x34,unit_pilot=0x38,unit_second=0x3C;
constexpr uint16_t text_unit_names=0x20F,text_pilot_names=0x111E,text_pilot_full_names=0x1287,text_title=0xFD2,text_fairy=0xFEC,text_sub=0x1002,text_level=0xFE1,text_hp=0xFE7,
    text_limit=0xFEB,text_evade=0x100D,text_hit=0xFF4,text_terrain=0xFF6,text_current=0x1013,text_after=0x1014,text_board=0x1012,text_ask=0xFE4,text_yes=0xFE5,text_no=0xFE6;
constexpr unsigned sound_move=0xB9,sound_confirm=0xB7,sound_cancel=0xB8;
constexpr uint16_t button_a=0x8000,button_b=0x4000;

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
    json row={{"schema","srw64.swap-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
uint32_t call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);function(ram,&c);
    return uint32_t(c.r2);
}
uint32_t call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0){return call(ram,ctx,LOOKUP_FUNC(function),a0,a1);}
bool idle(const uint8_t* ram){return int8_t(read(ram,transition,1))==-1;}
void sound(uint8_t* ram,recomp_context* ctx,unsigned id){call(ram,ctx,resident_func_8007E8A8,id);}
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
char terrain_letter(unsigned rank){return rank==1?'D':rank==2?'C':rank==3?'B':rank==4?'A':'-';}
uint32_t pilot_at(unsigned index){return pilots+std::min(index,pilot_count-1)*pilot_size;}

// --- Records ---------------------------------------------------------------------

void refresh_display(uint8_t* ram,recomp_context* ctx,uint32_t unit) {
    call(ram,ctx,resident_func_800A5254,unit,2);
    call(ram,ctx,display_stats,unit);
}
uint32_t pilot_unit(uint8_t* ram,recomp_context* ctx,uint32_t pilot) {
    const unsigned number=read(ram,pilot+pilot_number,2);
    if(number==0x78 || number==0x79 || number==0x7B || number==0x7C)
        if(const uint32_t unit=call(ram,ctx,getter_unit,number);unit && valid(unit,0x54))return unit;
    if(!read(ram,pilot+pilot_has_unit,1))return 0;
    const uint32_t unit=read(ram,pilot+pilot_unit_field,4);
    return unit && valid(unit,0x54)?unit:0;
}
json pilot_json(const uint8_t* ram,unsigned index) {
    const uint32_t pilot=pilot_at(index);
    const unsigned number=read(ram,pilot+pilot_number,2);
    json p={{"index",index},{"number",number},{"name",text(ram,uint16_t(text_pilot_names+number))},{"full_name",text(ram,uint16_t(text_pilot_full_names+number))},{"level",read(ram,pilot+pilot_level,1)}};
    if(art.contains("portraits") && art["portraits"].contains(std::to_string(number)))p["art"]=art["portraits"][std::to_string(number)];
    return p;
}
json unit_json(const uint8_t* ram,uint32_t unit) {
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    json u={{"number",number},{"name",text(ram,text_unit_names+number)}};
    if(art.contains("units") && art["units"].contains(std::to_string(number)))u["art"]=art["units"][std::to_string(number)];
    const uint32_t pilot=read(ram,unit+unit_pilot,4);
    u["pilot"]=read(ram,unit+unit_crew,1) && pilot && valid(pilot,pilot_size)?text(ram,uint16_t(text_pilot_names+read(ram,pilot+pilot_number,2))):std::string();
    return u;
}
// The second crew member (fairy) of a unit, as the list details print it.
json sub_pilot(const uint8_t* ram,uint32_t unit) {
    if(!unit || !valid(unit,0x54) || read(ram,unit+unit_crew,1)<2)return {{"name",std::string()}};
    const uint32_t second=read(ram,unit+unit_second,4);
    if(!second || !valid(second,pilot_size) || !(read(ram,second+pilot_flags,1)&0x40))return {{"name",std::string()}};
    return {{"name",text(ram,uint16_t(text_pilot_names+read(ram,second+pilot_number,2)))},{"level",read(ram,second+pilot_level,1)}};
}
void labels(const uint8_t* ram) {
    const std::pair<const char*,uint16_t> keys[]={{"title",text_title},{"fairy",text_fairy},{"sub",text_sub},{"level",text_level},{"hp",text_hp},{"limit",text_limit},{"evade",text_evade},{"hit",text_hit},
        {"terrain",text_terrain},{"air",uint16_t(text_terrain+1)},{"land",uint16_t(text_terrain+2)},{"sea",uint16_t(text_terrain+3)},{"space",uint16_t(text_terrain+4)},
        {"current",text_current},{"after",text_after},{"board",text_board},{"ask",text_ask},{"yes",text_yes},{"no",text_no}};
    json l;
    for(const auto& [key,id]:keys)l[key]=text(ram,id);
    current["labels"]=l;
    locale=localization::catalog().locale;
}
void open(json page,const uint8_t* ram) {
    page["visible"]=true;page["serial"]=++serial;
    current=std::move(page);labels(ram);
    active=true;owning=true;pending.clear();
    record("open",{{"screen",current.value("screen",std::string())}});
}
bool original_screens() {
    if(settings::native_intermission_ui())return false;
    std::lock_guard lock(mutex);hide("original");return true;
}
void republish(json next) {
    next["visible"]=true;next["serial"]=current["serial"];next["labels"]=current["labels"];
    current=std::move(next);
}

// --- Nine-row lists: pilots (6) and fairies (20) ----------------------------------

unsigned list_size(const uint8_t* ram){return std::min<unsigned>(read(ram,list_count,2),pilot_count);}
unsigned list_index(const uint8_t* ram) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    return page*list_rows+unsigned(int16_t(read(ram,list_row,2)));
}
uint32_t list_entry(const uint8_t* ram,bool fairies,unsigned index){return read(ram,(fairies?fairy_list:pilot_list)+index*2,2);}
void list_select(uint8_t* ram,bool fairies) {
    const unsigned index=list_index(ram);
    if(index<list_size(ram))write32(ram,screen_subject,list_entry(ram,fairies,index));
}
json list_json(uint8_t* ram,recomp_context* ctx,bool fairies) {
    const unsigned count=list_size(ram),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*list_rows;n<count && n<(page+1)*list_rows;++n) {
        const unsigned index=list_entry(ram,fairies,n);
        const uint32_t pilot=pilot_at(index);
        json row=pilot_json(ram,index);
        if(const uint32_t unit=pilot_unit(ram,ctx,pilot))row["unit"]=text(ram,uint16_t(text_unit_names+read(ram,unit+up::unit_number,2)+(fairies?0:call(ram,ctx,getter_form,pilot))));
        else row["unit"]=std::string();
        rows.push_back(row);
    }
    // The details line: the fairy of the pilot's machine (6) or the pilot whose
    // machine the fairy rides (20).
    json sub={{"name",std::string()}};
    const unsigned index=list_index(ram);
    if(index<count) {
        const uint32_t unit=pilot_unit(ram,ctx,pilot_at(list_entry(ram,fairies,index)));
        if(fairies && unit){const uint32_t host=read(ram,unit+unit_pilot,4);if(host && valid(host,pilot_size))sub={{"name",text(ram,uint16_t(text_pilot_names+read(ram,host+pilot_number,2)))},{"level",read(ram,host+pilot_level,1)}};}
        else if(unit)sub=sub_pilot(ram,unit);
    }
    return {{"screen",fairies?"fairies":"pilots"},{"page",page},{"pages",std::max(1u,(count+list_rows-1)/list_rows)},{"cursor",read(ram,list_row,2)},{"rows",rows},{"sub",sub}};
}
bool list_build(uint8_t* ram,recomp_context* ctx,bool fairies) {
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,fairies?build_fairies:build_pilots);
    write16(ram,list_row_copy,uint16_t(read(ram,list_row,2)));
    list_select(ram,fairies);
    std::lock_guard lock(mutex);
    open(list_json(ram,ctx,fairies),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool list_step(uint8_t* ram,recomp_context* ctx,bool fairies,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!=(fairies?"fairies":"pilots"))return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const unsigned count=list_size(ram),pages=std::max(1u,(count+list_rows-1)/list_rows);
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1,row=unsigned(int16_t(read(ram,list_row,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*list_rows) || value>=list_rows || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*list_rows-1);}
        write16(ram,list_page,uint16_t(page+1));write16(ram,list_row,uint16_t(row));write16(ram,list_row_copy,uint16_t(row));
        list_select(ram,fairies);
        republish(list_json(ram,ctx,fairies));
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" || action=="back") {
        lock.unlock();
        const bool left=feed(ram,ctx,step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

// --- Seven-row target lists: machines (16) and pilots for a fairy (21) --------------

unsigned targets_size(const uint8_t* ram){return std::min<unsigned>(read(ram,target_count,2),up::unit_slots);}
unsigned target_index(const uint8_t* ram) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,target_page,2))))-1;
    return page*target_rows+unsigned(int16_t(read(ram,target_row,2)));
}
void target_select(uint8_t* ram) {
    const unsigned index=target_index(ram);
    if(index<targets_size(ram))write32(ram,target,read(ram,target_list+index*2,2));
}
json targets_json(uint8_t* ram,recomp_context* ctx,bool fairy) {
    const unsigned subject=read(ram,screen_subject,4);
    const uint32_t pilot=pilot_at(subject);
    const unsigned count=targets_size(ram),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,target_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*target_rows;n<count && n<(page+1)*target_rows;++n) {
        const unsigned entry=read(ram,target_list+n*2,2);
        if(fairy) {
            json row=pilot_json(ram,entry);
            const uint32_t unit=pilot_unit(ram,ctx,pilot_at(entry));
            row["unit"]=unit?text(ram,uint16_t(text_unit_names+read(ram,unit+up::unit_number,2))):std::string();
            rows.push_back(row);
        } else {
            const uint32_t unit=up::unit_at(entry);
            refresh_display(ram,ctx,unit);
            json row=unit_json(ram,unit);row["slot"]=entry;row["hp"]=read(ram,display_hp,2);
            rows.push_back(row);
        }
    }
    json p=pilot_json(ram,subject);
    // The header's second line: the fairy on the pilot's machine (16) or the pilot
    // whose machine the fairy rides (21).
    json sub={{"name",std::string()}};
    if(const uint32_t unit=pilot_unit(ram,ctx,pilot)) {
        p["unit"]=text(ram,uint16_t(text_unit_names+read(ram,unit+up::unit_number,2)));
        if(fairy){const uint32_t host=read(ram,unit+unit_pilot,4);if(host && valid(host,pilot_size))sub={{"name",text(ram,uint16_t(text_pilot_names+read(ram,host+pilot_number,2)))},{"level",read(ram,host+pilot_level,1)}};}
        else sub=sub_pilot(ram,unit);
    }
    json page_json={{"screen",fairy?"fairy_targets":"targets"},{"pilot",p},{"sub",sub},{"page",page},{"pages",std::max(1u,(count+target_rows-1)/target_rows)},{"cursor",read(ram,target_row,2)},{"rows",rows}};
    if(fairy){page_json["mode"]=read(ram,mode,4);page_json["window_cursor"]=read(ram,confirm_cursor,2);}
    return page_json;
}
bool targets_build(uint8_t* ram,recomp_context* ctx,bool fairy) {
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,fairy?build_fairy_targets:build_targets);
    target_select(ram);
    if(fairy)write32(ram,mode,0);
    std::lock_guard lock(mutex);
    open(targets_json(ram,ctx,fairy),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool targets_step(uint8_t* ram,recomp_context* ctx,bool fairy,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!=(fairy?"fairy_targets":"targets"))return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const bool window=fairy && read(ram,mode,4)!=0;
    if(window) {
        // The 乗せますか window of screen 21: the original's A branch built its sprite
        // and texts, which the page draws instead; はい feeds A, いいえ closes it.
        if(action.starts_with("move:")) {
            const unsigned index=unsigned(std::atoi(action.c_str()+5));
            if(index>1 || index==read(ram,confirm_cursor,2))return false;
            write16(ram,confirm_cursor,uint16_t(index));republish(targets_json(ram,ctx,true));
            lock.unlock();sound(ram,ctx,sound_move);return false;
        }
        if(action=="choose" && read(ram,confirm_cursor,2)==0) {
            lock.unlock();
            const bool left=feed(ram,ctx,step,button_a);
            lock.lock();record("board",{{"left",left},{"next",read(ram,next_screen,4)}});
            if(left)hide("close");
            return true;
        }
        if(action=="choose" || action=="cancel" || action=="back") {
            write32(ram,mode,0);republish(targets_json(ram,ctx,true));record("window-close");
            lock.unlock();sound(ram,ctx,sound_cancel);return false;
        }
        return false;
    }
    const unsigned count=targets_size(ram),pages=std::max(1u,(count+target_rows-1)/target_rows);
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,target_page,2))))-1,row=unsigned(int16_t(read(ram,target_row,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*target_rows) || value>=target_rows || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*target_rows-1);}
        write16(ram,target_page,uint16_t(page+1));write16(ram,target_row,uint16_t(row));
        target_select(ram);
        republish(targets_json(ram,ctx,fairy));
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" && fairy) {
        // The original's A branch in list mode only builds the window; the page shows it.
        write16(ram,confirm_cursor,0);write32(ram,mode,1);republish(targets_json(ram,ctx,true));record("window-open");
        lock.unlock();sound(ram,ctx,sound_confirm);return false;
    }
    if(action=="choose" || action=="back") {
        lock.unlock();
        const bool left=feed(ram,ctx,step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

// --- Confirm (17) -------------------------------------------------------------------

json confirm_json(uint8_t* ram,recomp_context* ctx) {
    const unsigned subject=read(ram,screen_subject,4),slot=read(ram,target,4);
    const uint32_t pilot=pilot_at(subject),unit=up::unit_at(std::min<uint32_t>(slot,up::unit_slots-1));
    json p=pilot_json(ram,subject);
    json from=json::object();
    int mobility_now=0;
    if(const uint32_t now=pilot_unit(ram,ctx,pilot)) {
        refresh_display(ram,ctx,now);mobility_now=read(ram,display_mobility,2);
        from=unit_json(ram,now);from["sub"]=sub_pilot(ram,now);
    }
    write16(ram,current_mobility,uint16_t(mobility_now));
    refresh_display(ram,ctx,unit);
    json to=unit_json(ram,unit);to["slot"]=slot;to["hp"]=read(ram,display_hp,2);to["limit"]=read(ram,display_limit,2);to["mobility"]=read(ram,display_mobility,2);
    to["sub"]=sub_pilot(ram,unit);
    const int limit=read(ram,display_limit,2),mobility=read(ram,display_mobility,2),evade=read(ram,pilot+pilot_evade,2),hit=read(ram,pilot+pilot_hit,2);
    // 800A6194(1, pilot rank, unit rank): the sum of the two — 0 '-', 1–3 D, 4–5 C,
    // 6–7 B, 8 and up A (verified against the original page).
    std::string terrain;
    for(unsigned n=0;n<4;++n) {
        unsigned rank=read(ram,unit+unit_terrain+n,1);
        if(n==0 && read(ram,air_override,1))rank=4;
        const unsigned sum=rank+read(ram,pilot+pilot_terrain+n,1);
        terrain+=sum==0?'-':sum<4?'D':sum<6?'C':sum<8?'B':'A';
    }
    return {{"screen","confirm"},{"pilot",p},{"from",from},{"to",to},{"cursor",read(ram,confirm_cursor,2)},
        {"evade",{{"value",evade},{"after",evade+mobility},{"now",evade+mobility_now},{"over",limit<evade+mobility}}},
        {"hit",{{"value",hit},{"after",hit+mobility},{"now",hit+mobility_now},{"over",limit<hit+mobility}}},{"terrain",terrain}};
}
bool confirm_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    write16(ram,confirm_cursor,0);
    std::lock_guard lock(mutex);
    open(confirm_json(ram,ctx),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool confirm_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="confirm")return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    if(action.starts_with("move:")) {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>1 || index==read(ram,confirm_cursor,2))return false;
        write16(ram,confirm_cursor,uint16_t(index));current["cursor"]=index;
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" || action=="back") {
        // A with はい: the original moves the pilot (and the sub-pilot, the parts) and
        // returns to screen 6; A with いいえ or B: back to screen 16.
        lock.unlock();
        const bool left=feed(ram,ctx,step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)},{"cursor",read(ram,confirm_cursor,2)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

bool build(uint8_t* ram,recomp_context* ctx,unsigned screen) {
    if(original_screens())return false;
    switch(screen) {
        case 6: return list_build(ram,ctx,false);
        case 20: return list_build(ram,ctx,true);
        case 16: return targets_build(ram,ctx,false);
        case 21: return targets_build(ram,ctx,true);
        case 17: return confirm_build(ram,ctx);
        default: return false;
    }
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    switch(screen) {
        case 6: return list_step(ram,ctx,false,original);
        case 20: return list_step(ram,ctx,true,original);
        case 16: return targets_step(ram,ctx,false,original);
        case 21: return targets_step(ram,ctx,true,original);
        case 17: return confirm_step(ram,ctx,original);
        default: return false;
    }
}
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(active && !idle(ram) && read(ram,next_screen,4)!=read(ram,current_screen,4))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_SWAP");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original screens.
    std::ifstream source(std::getenv("SRW64_DIALOGUE_DATA"));
    const auto data=json::parse(source,nullptr,false);
    if(!data.is_discarded() && data.contains("battle_assets"))art=data.at("battle_assets");
    log.open(directory/"swap-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.swap_build=build;h.swap_step=step;h.swap_frame=frame;
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
