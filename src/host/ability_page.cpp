#include "ability_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_menu.hpp"
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
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
namespace srw64::ability_page {
namespace {
using namespace guest;
using json=nlohmann::json;
namespace up=srw64::upgrades;
// Intermission overlay state (load_0008F4B0), as upgrade_page.cpp.
constexpr uint32_t next_screen=0x801DECB8,current_screen=0x801DECCC,transition=0x8015E9C5,pressed=0x80178A08;
// Lists: 801C87DC fills D_801DD210 with every unit slot, 801C8DD8 fills D_801DD3A8 with
// every pilot index, both on 1-based nine-row pages D_801DD14A with row D_801DEBC8
// (copy D_801DD53A); D_801DEC5C is the unit slot or the pilot index the page shows.
constexpr uint32_t list_count=0x801DD0A0,unit_list=0x801DD210,pilot_list=0x801DD3A8,list_row=0x801DEBC8,list_row_copy=0x801DD53A,list_page=0x801DD14A,screen_subject=0x801DEC5C;
constexpr unsigned rows_per_page=9;
constexpr uint32_t build_unit_list=0x801C87DC,build_pilot_list=0x801C8DD8,getter_unit=0x801C924C,getter_form=0x801C8E50,display_stats=0x801C4DE4;
// Weapon list (screen 14): 801C7254 fills D_801DEC68 (count D_801DECBE) for the screen's
// unit, six rows a page D_801DD20C (1-based), row D_801DEC58 (copy D_801DDA34).
constexpr uint32_t weapon_fill=0x801C7254,weapon_rows=0x801DEC68,weapon_count=0x801DECBE,weapon_page=0x801DD20C,weapon_cursor=0x801DEC58,weapon_row_copy=0x801DDA34;
constexpr unsigned weapon_rows_per_page=6;
// Unit fields.
constexpr uint32_t unit_en=0x08,unit_en_max=0x0A,unit_size_flags=0x0C,unit_move_flags=0x0D,unit_terrain=0x16,unit_repair=0x1C,unit_flags=0x20,unit_slot_count=0x21,unit_parts=0x23,
    unit_ability_flags=0x28,unit_crew=0x34,unit_pilot=0x38,unit_second=0x3C;
constexpr uint32_t display_hp=0x801DEB08,display_move=0x801DD14C,display_mobility=0x801DD14E,display_armor=0x801DEB06,display_limit=0x801DEB04,air_override=0x801DDA06;
// Pilot records: 100 of 0x4C bytes at 0x80172F40.
constexpr uint32_t pilots=0x80172F40,pilot_size=0x4C,pilot_number=0x02,pilot_flags=0x04,pilot_level=0x05,pilot_skill_level=0x06,pilot_skill2=0x07,pilot_skill3=0x08,
    pilot_spirit_count=0x0A,pilot_spirits=0x0B,pilot_exp=0x12,pilot_sp=0x16,pilot_sp_max=0x18,pilot_morale=0x20,pilot_melee=0x22,pilot_ranged=0x24,pilot_evade=0x26,pilot_hit=0x28,
    pilot_reaction=0x2A,pilot_skill=0x2C,pilot_terrain=0x2E,pilot_skill_flags=0x36,pilot_has_unit=0x37,pilot_unit_field=0x38;
constexpr unsigned pilot_count=100;
// Tables in the overlay: movement icons, the special-ability list {mask u32, text u16, width u8}.
constexpr uint32_t move_icons=0x801DC8F0,ability_table=0x801DC8FC,unknown_spirit=0x801DCE2C;
constexpr unsigned ability_entries=16;
constexpr uint16_t text_unit_names=0x20F,text_pilot_names=0x111E,text_pilot_full_names=0x1287,text_part_names=0x469,text_sizes=0x446,text_shield_yes=0x38F,text_shield_no=0x390,
    text_spirits=0x3C9,text_special_skill=0xF1,text_unit_list_title=0xFD0,text_pilot_list_title=0xFD1,text_sub=0x1002,text_level=0xFE1,text_stat=0xFE7,
    text_size=0x1003,text_repair=0x1004,text_abilities=0x1005,text_type=0x1006,text_move=0x1007,text_terrain=0xFF6,text_pilot_title=0x1008,text_morale=0x1009,text_next=0x100A,text_sp=0x100B,
    text_melee=0x100C,text_evade=0x100D,text_reaction=0x100E,text_ranged=0x1023,text_hit=0xFF4,text_skill=0x100F,text_spirits_title=0x1010,text_skills_title=0x1011;
constexpr unsigned sound_move=0xB9,sound_confirm=0xB7,sound_cancel=0xB8;
constexpr uint16_t button_a=0x8000,button_b=0x4000,button_l=0x20,button_r=0x10;

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
    json row={{"schema","srw64.ability-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
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
// 800A6104(1, rank) for unit and pilot ranks: 1 'D', 2 'C', 3 'B', 4 'A', else '-'
// (weapon ranks are stored one higher; see upgrade_page.cpp).
char terrain_letter(unsigned rank){return rank==1?'D':rank==2?'C':rank==3?'B':rank==4?'A':'-';}
uint32_t pilot_at(unsigned index){return pilots+index*pilot_size;}

// --- Records ---------------------------------------------------------------------

void refresh_display(uint8_t* ram,recomp_context* ctx,uint32_t unit) {
    call(ram,ctx,resident_func_800A5254,unit,2);
    call(ram,ctx,display_stats,unit);
}
json pilot_brief(const uint8_t* ram,uint32_t pilot) {
    if(!pilot || !valid(pilot,pilot_size))return {{"name",std::string()}};
    return {{"number",read(ram,pilot+pilot_number,2)},{"name",text(ram,uint16_t(text_pilot_names+read(ram,pilot+pilot_number,2)))},{"level",read(ram,pilot+pilot_level,1)}};
}
// The second crew member the list details print: crew of two or more and the
// second pilot's +4 bit 0x40.
json sub_pilot(const uint8_t* ram,uint32_t unit) {
    if(!unit || !valid(unit,0x54) || read(ram,unit+unit_crew,1)<2)return {{"name",std::string()}};
    const uint32_t second=read(ram,unit+unit_second,4);
    if(!second || !valid(second,pilot_size) || !(read(ram,second+pilot_flags,1)&0x40))return {{"name",std::string()}};
    return pilot_brief(ram,second);
}
// The unit a pilot rides: the Getter team shares one, resolved by 801C924C.
uint32_t pilot_unit(uint8_t* ram,recomp_context* ctx,uint32_t pilot) {
    const unsigned number=read(ram,pilot+pilot_number,2);
    if(number==0x78 || number==0x79 || number==0x7B || number==0x7C)
        if(const uint32_t unit=call(ram,ctx,getter_unit,number);unit && valid(unit,0x54))return unit;
    if(!read(ram,pilot+pilot_has_unit,1))return 0;
    const uint32_t unit=read(ram,pilot+pilot_unit_field,4);
    return unit && valid(unit,0x54)?unit:0;
}
std::string unit_art(unsigned number) {
    return art.contains("units") && art["units"].contains(std::to_string(number))?art["units"][std::to_string(number)].value("path",std::string()):std::string();
}
json unit_brief(const uint8_t* ram,uint32_t unit) {
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    json u={{"number",number},{"name",text(ram,text_unit_names+number)}};
    if(art.contains("units") && art["units"].contains(std::to_string(number)))u["art"]=art["units"][std::to_string(number)];
    return u;
}
void labels(const uint8_t* ram) {
    const std::pair<const char*,uint16_t> keys[]={{"unit_list",text_unit_list_title},{"pilot_list",text_pilot_list_title},{"sub",text_sub},{"level",text_level},
        {"hp",text_stat},{"en",uint16_t(text_stat+1)},{"mobility",uint16_t(text_stat+2)},{"armor",uint16_t(text_stat+3)},{"limit",uint16_t(text_stat+4)},
        {"size",text_size},{"repair",text_repair},{"abilities",text_abilities},{"type",text_type},{"move",text_move},{"terrain",text_terrain},
        {"air",uint16_t(text_terrain+1)},{"land",uint16_t(text_terrain+2)},{"sea",uint16_t(text_terrain+3)},{"space",uint16_t(text_terrain+4)},
        {"pilot",text_pilot_title},{"morale",text_morale},{"next",text_next},{"sp",text_sp},{"melee",text_melee},{"hit",text_hit},{"skill",text_skill},
        {"ranged",text_ranged},{"evade",text_evade},{"reaction",text_reaction},{"spirits",text_spirits_title},{"skills",text_skills_title}};
    json l;
    for(const auto& [key,id]:keys)l[key]=text(ram,id);
    // The unlearned-spirit placeholder is a literal in the overlay ("?????").
    std::string unknown;
    for(uint32_t n=0;n<8;++n){const auto c=read(ram,unknown_spirit+n,1);if(!c)break;unknown+=char(c);}
    l["unknown"]=unknown;
    current["labels"]=l;current["weapon_labels"]=upgrade_page::weapon_labels_json(ram);
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

// --- Lists (screens 4 and 5) ------------------------------------------------------

unsigned list_size(const uint8_t* ram,bool pilots_list){return std::min<unsigned>(read(ram,list_count,2),pilots_list?pilot_count:up::unit_slots);}
unsigned list_index(const uint8_t* ram) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    return page*rows_per_page+unsigned(int16_t(read(ram,list_row,2)));
}
uint32_t list_entry(const uint8_t* ram,bool pilots_list,unsigned index){return read(ram,(pilots_list?pilot_list:unit_list)+index*2,2);}
void list_select(uint8_t* ram,bool pilots_list) {
    const unsigned index=list_index(ram);
    if(index<list_size(ram,pilots_list))write32(ram,screen_subject,list_entry(ram,pilots_list,index));
}
json list_json(uint8_t* ram,recomp_context* ctx,bool pilots_list) {
    const unsigned count=list_size(ram,pilots_list),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*rows_per_page;n<count && n<(page+1)*rows_per_page;++n) {
        const uint32_t entry=list_entry(ram,pilots_list,n);
        if(pilots_list) {
            const uint32_t pilot=pilot_at(entry);
            json row=pilot_brief(ram,pilot);row["index"]=entry;
            // The unit name takes the Getter form offset 801C8E50 adds for its pilots.
            if(const uint32_t unit=pilot_unit(ram,ctx,pilot))row["unit"]=text(ram,uint16_t(text_unit_names+read(ram,unit+up::unit_number,2)+call(ram,ctx,getter_form,pilot)));
            else row["unit"]=std::string();
            rows.push_back(row);
        } else {
            const uint32_t unit=up::unit_at(entry);
            refresh_display(ram,ctx,unit);
            json row=unit_brief(ram,unit);row["slot"]=entry;row["hp"]=read(ram,display_hp,2);
            const uint32_t pilot=read(ram,unit+unit_pilot,4);
            row["pilot"]=read(ram,unit+unit_crew,1) && pilot && valid(pilot,pilot_size)?text(ram,uint16_t(text_pilot_names+read(ram,pilot+pilot_number,2))):std::string();
            rows.push_back(row);
        }
    }
    // The details line: the second crew member of the unit under the cursor.
    const unsigned index=list_index(ram);
    uint32_t unit=0;
    if(index<count)unit=pilots_list?pilot_unit(ram,ctx,pilot_at(list_entry(ram,true,index))):up::unit_at(list_entry(ram,false,index));
    return {{"screen",pilots_list?"pilots":"units"},{"page",page},{"pages",std::max(1u,(count+rows_per_page-1)/rows_per_page)},{"cursor",read(ram,list_row,2)},{"rows",rows},{"sub",sub_pilot(ram,unit)}};
}
bool list_build(uint8_t* ram,recomp_context* ctx,bool pilots_list) {
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,pilots_list?build_pilot_list:build_unit_list);
    write16(ram,list_row_copy,uint16_t(read(ram,list_row,2)));
    list_select(ram,pilots_list);
    std::lock_guard lock(mutex);
    open(list_json(ram,ctx,pilots_list),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool list_step(uint8_t* ram,recomp_context* ctx,bool pilots_list,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!=(pilots_list?"pilots":"units"))return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const unsigned count=list_size(ram,pilots_list),pages=std::max(1u,(count+rows_per_page-1)/rows_per_page);
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1,row=unsigned(int16_t(read(ram,list_row,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*rows_per_page) || value>=rows_per_page || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*rows_per_page-1);}
        write16(ram,list_page,uint16_t(page+1));write16(ram,list_row,uint16_t(row));write16(ram,list_row_copy,uint16_t(row));
        list_select(ram,pilots_list);
        auto next=list_json(ram,ctx,pilots_list);next["visible"]=true;next["serial"]=current["serial"];
        for(const char* key:{"labels","weapon_labels"})next[key]=current[key];
        current=std::move(next);
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

// --- Unit page (screen 13) --------------------------------------------------------

json unit_json(uint8_t* ram,recomp_context* ctx) {
    const uint32_t slot=read(ram,screen_subject,4),unit=up::unit_at(slot);
    refresh_display(ram,ctx,unit);
    json u=unit_brief(ram,unit);u["slot"]=slot;
    // 801D1680: the size from +0xC bits 1/2/4/8/0x10.
    const unsigned size_flags=read(ram,unit+unit_size_flags,1);
    const unsigned size=size_flags&1?0:size_flags&2?1:size_flags&4?2:size_flags&8?3:4;
    json parts=json::array();
    for(unsigned n=0;n<std::min<unsigned>(read(ram,unit+unit_slot_count,1),4);++n) {
        const int part=int8_t(read(ram,unit+unit_parts+n,1));
        if(part>=0)parts.push_back(text(ram,uint16_t(text_part_names+part)));
    }
    const unsigned move_flags=read(ram,unit+unit_move_flags,1);
    json types=json::array();
    for(unsigned bit=0;bit<4;++bit)if(move_flags&(1u<<bit))types.push_back(text(ram,uint16_t(read(ram,move_icons+bit*2,2))));
    if(move_flags&0x10)types.push_back(text(ram,uint16_t(read(ram,move_icons+8,2))));
    const uint32_t flags=read(ram,unit+unit_ability_flags,4);
    json abilities=json::array();
    for(unsigned n=0;n<ability_entries;++n) {
        const uint32_t entry=ability_table+n*8;
        if(flags&read(ram,entry,4))abilities.push_back(text(ram,uint16_t(read(ram,entry+4,2))));
    }
    std::string terrain;
    for(unsigned n=0;n<4;++n) {
        unsigned rank=read(ram,unit+unit_terrain+n,1);
        if(n==0 && read(ram,air_override,1))rank=4;
        terrain+=terrain_letter(rank);
    }
    const unsigned count=list_size(ram,false),index=list_index(ram);
    return {{"screen","unit"},{"unit",u},{"size",text(ram,uint16_t(text_sizes+size))},{"repair",read(ram,unit+unit_repair,2)},{"parts",parts},
        {"hp",read(ram,display_hp,2)},{"hp_max",read(ram,display_hp,2)},{"en",read(ram,unit+unit_en,2)},{"en_max",read(ram,unit+unit_en_max,2)},
        {"types",types},{"shield",text(ram,read(ram,unit+unit_flags,1)&2?text_shield_yes:text_shield_no)},{"abilities",abilities},
        {"move",read(ram,display_move,2)},{"mobility",read(ram,display_mobility,2)},{"armor",read(ram,display_armor,2)},{"limit",read(ram,display_limit,2)},
        {"terrain",terrain},{"index",index},{"count",count}};
}
bool unit_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    std::lock_guard lock(mutex);
    open(unit_json(ram,ctx),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
// The unit and pilot pages: prev / next feed the original's L / R, which moves the
// list cursor and re-enters the screen; A opens the weapons (unit page); B returns.
bool page_step(uint8_t* ram,recomp_context* ctx,const char* screen,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!=screen)return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    uint16_t button=0;
    if(action=="prev")button=button_l;else if(action=="next")button=button_r;
    else if(action=="choose" && std::string(screen)=="unit")button=button_a;
    else if(action=="back")button=button_b;
    if(!button)return false;
    lock.unlock();
    const bool left=feed(ram,ctx,step,button);
    lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
    if(left)hide("close");
    return true;
}

// --- Weapon list (screen 14) ------------------------------------------------------

json weapons_json(uint8_t* ram,recomp_context* ctx) {
    const uint32_t slot=read(ram,screen_subject,4),unit=up::unit_at(slot);
    refresh_display(ram,ctx,unit);
    const unsigned count=read(ram,weapon_count,2),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,weapon_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*weapon_rows_per_page;n<count && n<(page+1)*weapon_rows_per_page;++n)rows.push_back(upgrade_page::weapon_row_json(ram,unit,read(ram,weapon_rows+n*2,2)));
    json u=unit_brief(ram,unit);u["slot"]=slot;u["en"]=read(ram,unit+unit_en,2);
    const uint32_t pilot=read(ram,unit+unit_pilot,4);
    u["morale"]=pilot && valid(pilot,pilot_size)?int(read(ram,pilot+pilot_morale,2)):-1;
    return {{"screen","weapons"},{"unit",u},{"page",page},{"pages",std::max(1u,(count+weapon_rows_per_page-1)/weapon_rows_per_page)},{"cursor",read(ram,weapon_cursor,2)},{"rows",rows}};
}
bool weapons_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    write16(ram,weapon_page,1);write16(ram,weapon_row_copy,uint16_t(read(ram,weapon_cursor,2)));
    call(ram,ctx,weapon_fill);
    std::lock_guard lock(mutex);
    open(weapons_json(ram,ctx),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool weapons_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="weapons")return false;
    if(locale!=localization::catalog().locale)labels(ram);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    const unsigned count=read(ram,weapon_count,2),pages=std::max(1u,(count+weapon_rows_per_page-1)/weapon_rows_per_page);
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,weapon_page,2))))-1,row=unsigned(int16_t(read(ram,weapon_cursor,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*weapon_rows_per_page) || value>=weapon_rows_per_page || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*weapon_rows_per_page-1);}
        write16(ram,weapon_page,uint16_t(page+1));write16(ram,weapon_cursor,uint16_t(row));write16(ram,weapon_row_copy,uint16_t(row));
        auto next=weapons_json(ram,ctx);next["visible"]=true;next["serial"]=current["serial"];
        for(const char* key:{"labels","weapon_labels"})next[key]=current[key];
        current=std::move(next);
        lock.unlock();sound(ram,ctx,sound_move);return false;
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

// --- Pilot page (screen 15) -------------------------------------------------------

json pilot_json(uint8_t* ram,recomp_context* ctx) {
    const unsigned index=std::min<unsigned>(read(ram,screen_subject,4),pilot_count-1);
    const uint32_t pilot=pilot_at(index);
    const unsigned number=read(ram,pilot+pilot_number,2),level=read(ram,pilot+pilot_level,1);
    const bool hidden=(read(ram,pilot+pilot_flags,1)&0xC0)!=0;   // stats printed as ---
    json p={{"index",index},{"number",number},{"name",text(ram,uint16_t(text_pilot_names+number))},{"full_name",text(ram,uint16_t(text_pilot_full_names+number))},{"level",level},{"hidden",hidden}};
    if(art.contains("portraits") && art["portraits"].contains(std::to_string(number)))p["art"]=art["portraits"][std::to_string(number)];
    const uint32_t unit=pilot_unit(ram,ctx,pilot);
    json u=json::object();
    if(unit){u=unit_brief(ram,unit);refresh_display(ram,ctx,unit);}
    json stats={{"melee",read(ram,pilot+pilot_melee,2)},{"ranged",read(ram,pilot+pilot_ranged,2)},{"hit",read(ram,pilot+pilot_hit,2)},{"evade",read(ram,pilot+pilot_evade,2)},
        {"skill",read(ram,pilot+pilot_skill,2)},{"reaction",read(ram,pilot+pilot_reaction,2)}};
    // 801C936C paints 命中 / 回避 red when pilot value + 運動性 exceeds the unit's 限界.
    // 801C936C prints 命中 / 回避 as "value+ 運動性", red when the sum exceeds the 限界.
    json over={{"hit",false},{"evade",false}};
    int mobility=-1;
    if(unit) {
        const int limit=read(ram,display_limit,2);mobility=read(ram,display_mobility,2);
        over["hit"]=limit<int(read(ram,pilot+pilot_hit,2))+mobility;over["evade"]=limit<int(read(ram,pilot+pilot_evade,2))+mobility;
    }
    json spirits=json::array();
    for(unsigned n=0;n<std::min<unsigned>(read(ram,pilot+pilot_spirit_count,1),6);++n)spirits.push_back(text(ram,uint16_t(text_spirits+read(ram,pilot+pilot_spirits+n,1))));
    json skills=json::array();
    const unsigned skill_flags=read(ram,pilot+pilot_skill_flags,1),skill_level=read(ram,pilot+pilot_skill_level,1);
    if(skill_flags&0x04)skills.push_back(text(ram,uint16_t(0x433+skill_level)));
    else if(skill_flags&0x08)skills.push_back(text(ram,uint16_t(0x40F+skill_level)));
    else if(skill_flags&0x10)skills.push_back(text(ram,uint16_t(0x418+skill_level)));
    else if(skill_flags&0x20)skills.push_back(text(ram,uint16_t(0x421+skill_level)));
    else if(skill_flags&0x40)skills.push_back(text(ram,uint16_t(0x42A+skill_level)));
    if((skill_flags&0x01) && read(ram,pilot+pilot_skill2,1))skills.push_back(text(ram,uint16_t(0x43C+read(ram,pilot+pilot_skill2,1))));
    if((skill_flags&0x02) && read(ram,pilot+pilot_skill3,1))skills.push_back(text(ram,uint16_t(0x406+read(ram,pilot+pilot_skill3,1))));
    if(number==4 && call(ram,ctx,resident_func_800A4BB8))skills.push_back(text(ram,text_special_skill));
    std::string terrain;
    for(unsigned n=0;n<4;++n)terrain+=terrain_letter(read(ram,pilot+pilot_terrain+n,1));
    json page={{"screen","pilot"},{"pilot",p},{"unit",u},{"morale",read(ram,pilot+pilot_morale,2)},{"sp",read(ram,pilot+pilot_sp,2)},{"sp_max",read(ram,pilot+pilot_sp_max,2)},
        {"stats",stats},{"over",over},{"mobility",mobility},{"spirits",spirits},{"skills",skills},{"terrain",terrain},{"index",list_index(ram)},{"count",list_size(ram,true)}};
    // Experience to the next level (8008407C of +0x12); none at level 99.
    if(level!=99)page["next"]=call(ram,ctx,resident_func_8008407C,read(ram,pilot+pilot_exp,2))&0xFFFF;
    return page;
}
bool pilot_build(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,resident_func_80085B94,0,1);
    std::lock_guard lock(mutex);
    open(pilot_json(ram,ctx),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}

bool build(uint8_t* ram,recomp_context* ctx,unsigned screen) {
    if(original_screens())return false;
    switch(screen) {
        case 4: return list_build(ram,ctx,false);
        case 5: return list_build(ram,ctx,true);
        case 13: return unit_build(ram,ctx);
        case 14: return weapons_build(ram,ctx);
        case 15: return pilot_build(ram,ctx);
        default: return false;
    }
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned screen,void(*original)(uint8_t*,recomp_context*)) {
    switch(screen) {
        case 4: return list_step(ram,ctx,false,original);
        case 5: return list_step(ram,ctx,true,original);
        case 13: return page_step(ram,ctx,"unit",original);
        case 14: return weapons_step(ram,ctx,original);
        case 15: return page_step(ram,ctx,"pilot",original);
        default: return false;
    }
}
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(active && !idle(ram) && read(ram,next_screen,4)!=read(ram,current_screen,4))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_ABILITY");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original screens.
    std::ifstream source(std::getenv("SRW64_DIALOGUE_DATA"));
    const auto data=json::parse(source,nullptr,false);
    if(!data.is_discarded() && data.contains("battle_assets"))art=data.at("battle_assets");
    log.open(directory/"ability-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.ability_build=build;h.ability_step=step;h.ability_frame=frame;
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
