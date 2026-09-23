#include "parts_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_menu.hpp"
#include "intermission_page.hpp"
#include "upgrade_rules.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include <atomic>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::parts_page {
namespace {
using namespace guest;
using json=nlohmann::json;
namespace up=srw64::upgrades;
// Intermission overlay state (load_0008F4B0), as upgrade_page.cpp.
constexpr uint32_t next_screen=0x801DECB8,current_screen=0x801DECCC,mode=0x801DECD8,transition=0x8015E9C5,pressed=0x80178A08;
// Machine list (screen 7): 801CB540 fills D_801DD210 with unit slots sorted by pilot
// level and counts eight-row pages; the cursor is a row (D_801DEBC8, mirrored in
// D_801DD53A) on a 1-based page (D_801DD14A); D_801DEC5C is the unit the next screen shows.
constexpr uint32_t list_count=0x801DD0A0,list_slots=0x801DD210,list_row=0x801DEBC8,list_row_copy=0x801DD53A,list_page=0x801DD14A,screen_unit=0x801DEC5C;
constexpr unsigned list_rows=8;
// Slots screen (18): slot cursor D_801DEC58; the inventory list D_801DD3A8 (801CBA78:
// 0x12 for はずす, then every part with a copy owned) on 1-based six-row pages
// D_801DD63C, row D_801DD0A2; D_801DECCA is the part under that cursor, D_801DECD8 the
// mode (0 slots, 1 inventory); D_801DD5CC counts the description texts the original
// draws; D_801DEC50 is its "drawn" flag; D_801DD63E remembers the slot for screen 19.
constexpr uint32_t slot_cursor=0x801DEC58,inventory_row=0x801DD0A2,inventory_page=0x801DD63C,inventory_list=0x801DD3A8,selected_part=0x801DECCA,
    description_count=0x801DD5CC,drawn=0x801DEC50,slot_remembered=0x801DD63E;
constexpr unsigned inventory_rows=6;
// Holders screen (19): 801CC988 fills D_801DCF10 with the unit slot holding each owned
// copy of D_801DECCA, 0xFFF for a free one; the cursor is D_801DEC58 again.
constexpr uint32_t holders=0x801DCF10;
constexpr unsigned free_copy=0xFFF;
// Part records: 18 parts; D_8015E990 holds owned (high byte) and equipped (low byte)
// counts; D_801DC578 seven u16 a part: HP, 移動力, 運動性, 装甲, 限界, two flags; the
// description table D_801DC97C gives text 0x10AF + base and the line count.
constexpr uint32_t inventory=0x8015E990,part_table=0x801DC578,description_table=0x801DC97C;
constexpr unsigned part_count=18,remove_sentinel=0x12,part_record=14;
// Unit fields: +0x21 slot count, +0x22 equipped count, +0x23.. the parts (s8, -1 empty),
// +0x34 crew count, +0x38 the first pilot (+2 number, +5 level).
constexpr uint32_t unit_en=0x08,unit_slot_count=0x21,unit_parts=0x23,unit_crew=0x34,unit_pilot=0x38;
constexpr uint32_t display_hp=0x801DEB08,display_move=0x801DD14C,display_mobility=0x801DD14E,display_armor=0x801DEB06,display_limit=0x801DEB04;
constexpr uint32_t build_list=0x801CB540,build_inventory=0x801CBA78,build_holders=0x801CC988,display_stats=0x801C4DE4;
constexpr uint16_t text_unit_names=0x20F,text_pilot_names=0x111E,text_part_names=0x469,text_descriptions=0x10AF,
    text_title=0xFD3,text_level=0xFE1,text_equipped=0x1015,text_select=0xFF0,text_select_title=0x1016,text_remove=0x1017,
    text_stat=0xFE7,text_move=0x1024,text_arrow=0x101E,text_page_prev=0x1041,text_page_next=0x1042;
constexpr unsigned sound_move=0xB9,sound_confirm=0xB7,sound_cancel=0xB8;
constexpr uint16_t button_a=0x8000,button_b=0x4000;

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
    json row={{"schema","srw64.parts-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
void call(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);function(ram,&c);
}
void call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0){call(ram,ctx,LOOKUP_FUNC(function),a0,a1);}
bool idle(const uint8_t* ram){return int8_t(read(ram,transition,1))==-1;}
void sound(uint8_t* ram,recomp_context* ctx,unsigned id){call(ram,ctx,resident_func_8007E8A8,id);}
// One button edge for an original step; true when it started a fade.
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

// --- Records ---------------------------------------------------------------------

struct Bonus{int hp{},move{},mobility{},armor{},limit{};};
Bonus bonus(const uint8_t* ram,int part) {
    if(part<0 || part>=int(part_count))return {};
    const uint32_t at=part_table+uint32_t(part)*part_record;
    return {int(read(ram,at,2)),int(read(ram,at+2,2)),int(read(ram,at+4,2)),int(read(ram,at+6,2)),int(read(ram,at+8,2))};
}
unsigned owned(const uint8_t* ram,unsigned part){return read(ram,inventory+part*2,1);}
unsigned equipped(const uint8_t* ram,unsigned part){return read(ram,inventory+part*2+1,1);}
std::string part_name(const uint8_t* ram,int part){return part==int(remove_sentinel)?text(ram,text_remove):part>=0 && part<int(part_count)?text(ram,uint16_t(text_part_names+part)):std::string("--------");}
json unit_json(const uint8_t* ram,uint32_t slot) {
    const uint32_t unit=up::unit_at(slot);
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    json parts=json::array();
    const unsigned slots=std::min<unsigned>(read(ram,unit+unit_slot_count,1),4);
    for(unsigned n=0;n<slots;++n) {
        const int part=int8_t(read(ram,unit+unit_parts+n,1));
        parts.push_back({{"part",part},{"name",part<0?std::string():part_name(ram,part)}});
    }
    json row={{"slot",slot},{"number",number},{"name",text(ram,text_unit_names+number)},{"parts",parts},{"en",read(ram,unit+unit_en,2)}};
    const uint32_t pilot=read(ram,unit+unit_pilot,4);
    if(read(ram,unit+unit_crew,1) && pilot && valid(pilot,8)){row["pilot"]=text(ram,uint16_t(text_pilot_names+read(ram,pilot+2,2)));row["level"]=read(ram,pilot+5,1);}
    else row["pilot"]=std::string();
    return row;
}
// The shown stats: the original recomputes the unit and lets 801C4DE4 add its parts.
void refresh_display(uint8_t* ram,recomp_context* ctx,uint32_t unit) {
    call(ram,ctx,resident_func_800A5254,unit,2);
    call(ram,ctx,display_stats,unit);
}
void labels(const uint8_t* ram) {
    current["labels"]={{"title",text(ram,text_title)},{"level",text(ram,text_level)},{"equipped",text(ram,text_equipped)},{"select",text(ram,text_select)},
        {"select_title",text(ram,text_select_title)},{"remove",text(ram,text_remove)},{"arrow",text(ram,text_arrow)},{"prev",text(ram,text_page_prev)},{"next",text(ram,text_page_next)},
        {"hp",text(ram,text_stat)},{"en",text(ram,text_stat+1)},{"move",text(ram,text_move)},{"mobility",text(ram,text_stat+2)},{"armor",text(ram,text_stat+3)},{"limit",text(ram,text_stat+4)}};
    locale=localization::catalog().locale;
}
void open(json page,const uint8_t* ram) {
    page["visible"]=true;page["serial"]=++serial;
    current=std::move(page);labels(ram);
    active=true;owning=true;pending.clear();
    record("open",{{"screen",current.value("screen",std::string())}});
}
// The original screens when the setting says so: any open page closes first.
bool original_screens() {
    if(settings::native_intermission_ui())return false;
    std::lock_guard lock(mutex);hide("original");return true;
}

// --- Machine list (screen 7) -----------------------------------------------------

unsigned list_size(const uint8_t* ram){return std::min<unsigned>(read(ram,list_count,2),up::unit_slots);}
void list_select(uint8_t* ram) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    const unsigned index=page*list_rows+unsigned(int16_t(read(ram,list_row,2)));
    if(index<list_size(ram))write32(ram,screen_unit,read(ram,list_slots+index*2,2));
}
json list_json(const uint8_t* ram) {
    const unsigned count=list_size(ram),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*list_rows;n<count && n<(page+1)*list_rows;++n)rows.push_back(unit_json(ram,read(ram,list_slots+n*2,2)));
    return {{"screen","list"},{"page",page},{"pages",std::max(1u,(count+list_rows-1)/list_rows)},{"cursor",read(ram,list_row,2)},{"rows",rows}};
}
// 801D4A00 without its drawing: dimmed background (its random call included), the
// sorted list and page count, the unit under the cursor.
bool list_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,build_list);
    write16(ram,list_row_copy,uint16_t(read(ram,list_row,2)));
    list_select(ram);
    std::lock_guard lock(mutex);
    open(list_json(ram),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool list_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="list")return false;
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
        list_select(ram);
        auto next=list_json(ram);next["visible"]=true;next["serial"]=current["serial"];next["labels"]=current["labels"];
        current=std::move(next);
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" || action=="back") {
        // A: the original sets screen 18 with the slot cursor at 0; B: screen 0.
        lock.unlock();
        const bool left=feed(ram,ctx,step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

// --- Slots and inventory (screen 18) ---------------------------------------------

unsigned inventory_size(const uint8_t* ram) {
    unsigned count=1;   // はずす
    for(unsigned part=0;part<part_count;++part)if(owned(ram,part))++count;
    return count;
}
int inventory_at(const uint8_t* ram,unsigned index){return index<inventory_size(ram)?int(read(ram,inventory_list+index*2,2)):int(remove_sentinel);}
unsigned inventory_index(const uint8_t* ram) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,inventory_page,2))))-1;
    return page*inventory_rows+unsigned(int16_t(read(ram,inventory_row,2)));
}
json slots_json(uint8_t* ram,recomp_context* ctx) {
    const uint32_t slot=read(ram,screen_unit,4),unit=up::unit_at(slot);
    refresh_display(ram,ctx,unit);
    json u=unit_json(ram,slot);
    const unsigned cursor=std::min<unsigned>(read(ram,slot_cursor,2),3),m=read(ram,mode,4);
    // As 801CBFAC: the "current" column is the shown value less the part in the
    // cursor slot; the preview adds the part under the inventory cursor.
    const int held_part=int8_t(read(ram,unit+unit_parts+cursor,1));
    const Bonus was=bonus(ram,held_part);
    const int selected=m?inventory_at(ram,inventory_index(ram)):-1;
    const Bonus add=bonus(ram,selected==int(remove_sentinel)?-1:selected);
    const int cur[5]={int(read(ram,display_hp,2))-was.hp,int(read(ram,display_move,2))-was.move,int(read(ram,display_mobility,2))-was.mobility,
        int(read(ram,display_armor,2))-was.armor,int(read(ram,display_limit,2))-was.limit};
    const int inc[5]={add.hp,add.move,add.mobility,add.armor,add.limit};
    const char* keys[5]={"hp","move","mobility","armor","limit"};
    json stats=json::array();
    stats.push_back({{"key","hp"},{"current",cur[0]},{"preview",m?cur[0]+inc[0]:cur[0]}});
    stats.push_back({{"key","en"},{"current",u.value("en",0)},{"preview",u.value("en",0)}});
    for(unsigned n=1;n<5;++n)stats.push_back({{"key",keys[n]},{"current",cur[n]},{"preview",m?cur[n]+inc[n]:cur[n]}});
    // The inventory page: row 0 of page 1 is はずす.
    const unsigned count=inventory_size(ram),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,inventory_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*inventory_rows;n<count && n<(page+1)*inventory_rows;++n) {
        const int part=inventory_at(ram,n);
        json row={{"part",part},{"name",part_name(ram,part)},{"remove",part==int(remove_sentinel)}};
        if(part>=0 && part<int(part_count)){row["equipped"]=equipped(ram,unsigned(part));row["owned"]=owned(ram,unsigned(part));}
        rows.push_back(row);
    }
    json description=json::array();
    if(m && selected>=0 && selected<int(part_count)) {
        const unsigned base=read(ram,description_table+uint32_t(selected)*4,2),lines=std::min<unsigned>(read(ram,description_table+uint32_t(selected)*4+2,2),4);
        for(unsigned n=0;n<lines;++n)description.push_back(text(ram,uint16_t(text_descriptions+base+n)));
    }
    return {{"screen","slots"},{"unit",u},{"cursor",cursor},{"mode",m},{"stats",stats},{"selected",selected},{"description",description},
        {"inventory",{{"page",page},{"pages",std::max(1u,(count+inventory_rows-1)/inventory_rows)},{"cursor",std::max<int>(0,int16_t(read(ram,inventory_row,2)))},{"rows",rows}}}};
}
// 801D4BEC without its drawing: background, the inventory list on page 1, the slot
// mode, the original's counters at rest.
bool slots_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    write16(ram,inventory_page,1);
    call(ram,ctx,build_inventory);
    // The inventory row is only reset by the original when the inventory opens; the
    // snapshot reads it from the start, so it starts at 0 here.
    write16(ram,inventory_row,0);
    write8(ram,selected_part,uint8_t(remove_sentinel));write32(ram,mode,0);write32(ram,description_count,0);write32(ram,drawn,0);
    std::lock_guard lock(mutex);
    open(slots_json(ram,ctx),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
void republish(uint8_t* ram,recomp_context* ctx) {
    auto next=slots_json(ram,ctx);next["visible"]=true;next["serial"]=current["serial"];next["labels"]=current["labels"];
    current=std::move(next);
}
bool slots_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="slots")return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const uint32_t unit=up::unit_at(read(ram,screen_unit,4));
    const unsigned slots=std::min<unsigned>(read(ram,unit+unit_slot_count,1),4);
    if(read(ram,mode,4)==0) {
        if(action.starts_with("move:")) {
            const unsigned index=unsigned(std::atoi(action.c_str()+5));
            if(index>=slots || index==read(ram,slot_cursor,2))return false;
            write16(ram,slot_cursor,uint16_t(index));republish(ram,ctx);
            lock.unlock();sound(ram,ctx,sound_move);return false;
        }
        if(action=="choose") {
            // The original's A branch: the inventory cursor at row 0 with no
            // description drawn; its cursor sprite is not built.
            write16(ram,inventory_row,0);write32(ram,description_count,0);write32(ram,mode,1);
            write8(ram,selected_part,uint8_t(inventory_at(ram,inventory_index(ram))));
            republish(ram,ctx);record("inventory-open");
            lock.unlock();sound(ram,ctx,sound_confirm);return false;
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
    const unsigned count=inventory_size(ram),pages=std::max(1u,(count+inventory_rows-1)/inventory_rows);
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,inventory_page,2))))-1,row=unsigned(int16_t(read(ram,inventory_row,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*inventory_rows) || value>=inventory_rows || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*inventory_rows-1);}
        write16(ram,inventory_page,uint16_t(page+1));write16(ram,inventory_row,uint16_t(row));
        write8(ram,selected_part,uint8_t(inventory_at(ram,page*inventory_rows+row)));
        republish(ram,ctx);
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="cancel" || action=="back") {
        // The original's B branch less the sprite and texts it never built here.
        write32(ram,mode,0);write32(ram,description_count,0);
        republish(ram,ctx);record("inventory-close");
        lock.unlock();sound(ram,ctx,sound_cancel);return false;
    }
    if(action=="choose") {
        // A: はずす on page 1 removes the cursor slot's part (or just re-enters when
        // it is empty); a part row remembers the slot and opens screen 19.
        lock.unlock();
        const bool left=feed(ram,ctx,step,button_a);
        lock.lock();record("choose",{{"left",left},{"next",read(ram,next_screen,4)},{"part",read(ram,selected_part,1)},{"slot",read(ram,slot_remembered,1)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

// --- Owned copies of one part (screen 19) ----------------------------------------

json holders_json(const uint8_t* ram) {
    const uint32_t slot=read(ram,screen_unit,4);
    const unsigned part=read(ram,selected_part,1),count=part<part_count?owned(ram,part):0;
    json rows=json::array();
    for(unsigned n=0;n<count;++n) {
        const unsigned holder=read(ram,holders+n*2,2);
        if(holder==free_copy || holder>=up::unit_slots){rows.push_back({{"free",true}});continue;}
        json u=unit_json(ram,holder);
        rows.push_back({{"free",false},{"slot",holder},{"name",u.value("name",std::string())},{"pilot",u.value("pilot",std::string())}});
    }
    return {{"screen","holders"},{"unit",unit_json(ram,slot)},{"part",{{"part",part},{"name",part_name(ram,int(part))}}},{"target_slot",read(ram,slot_remembered,1)},
        {"cursor",read(ram,slot_cursor,2)},{"rows",rows}};
}
// 801D5168 without its drawing: background and the holder list.
bool holders_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,build_holders);
    std::lock_guard lock(mutex);
    open(holders_json(ram),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool holders_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="holders")return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const unsigned count=unsigned(current.at("rows").size());
    if(action.starts_with("move:")) {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>=count || index==read(ram,slot_cursor,2))return false;
        write16(ram,slot_cursor,uint16_t(index));current["cursor"]=index;
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" || action=="back") {
        // A: the original equips a free copy or takes it from its holder, then
        // recomputes both units; B leaves it; both return to screen 18.
        lock.unlock();
        const bool left=feed(ram,ctx,step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}

bool build(uint8_t* ram,recomp_context* ctx,unsigned screen) {
    if(original_screens())return false;
    return screen==7?list_build(ram,ctx):screen==18?slots_build(ram,ctx):screen==19?holders_build(ram,ctx):false;
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    return screen==7?list_step(ram,ctx,original):screen==18?slots_step(ram,ctx,original):screen==19?holders_step(ram,ctx,original):false;
}
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    // Any other screen taking over closes the page; a re-entry of the same screen
    // rebuilds it through build().
    if(active && !idle(ram) && read(ram,next_screen,4)!=read(ram,current_screen,4))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_PARTS");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original screens.
    log.open(directory/"parts-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.parts_build=build;h.parts_step=step;h.parts_frame=frame;
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
