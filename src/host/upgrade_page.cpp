#include "upgrade_page.hpp"
#include "presentation_settings.hpp"
#include "intermission_menu.hpp"
#include "upgrade_rules.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include "focus_lines.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <mutex>
#include <vector>

uint64_t srw64_current_vi();
namespace srw64::upgrade_page {
namespace {
using namespace guest;
using json=nlohmann::json;
namespace up=srw64::upgrades;
// Intermission overlay state (load_0008F4B0).
constexpr uint32_t next_screen=0x801DECB8,current_screen=0x801DECCC,mode=0x801DECD8,transition=0x8015E9C5,pressed=0x80178A08,funds=0x8010F5F4;
// Machine list: D_801DD210 holds unit slots, seven rows a page; the cursor is a row
// (D_801DEBC8, mirrored in D_801DD53A) on a 1-based page (D_801DD14A); D_801DECD4 remembers
// the unit number to put the cursor back on; D_801DEC5C is the unit the next
// screen shows.
constexpr uint32_t list_count=0x801DD0A0,list_slots=0x801DD210,list_row=0x801DEBC8,list_row_copy=0x801DD53A,list_page=0x801DD14A,
    list_remembered=0x801DECD4,screen_unit=0x801DEC5C;
constexpr unsigned rows_per_page=7;
// Stat screen: cursor D_801DEC58, confirm window state D_801DECD8 (1) and its
// はい/いいえ cursor D_801DD0A2, the text handles the confirmed branch frees.
constexpr uint32_t stat_cursor=0x801DEC58,stat_drawn=0x801DEC50,stat_price=0x801DEBCC,confirm_cursor=0x801DD0A2,confirm_texts=0x801DEBD8,
    display_hp=0x801DEB08,display_mobility=0x801DD14E,display_armor=0x801DEB06,display_limit=0x801DEB04;
constexpr uint32_t list_build_stats=0x801C6BFC,list_build_weapons=0x801C6B80,display_stats=0x801C4DE4;
// Weapon list: 801C7254 fills D_801DEC68 with weapon indexes (count D_801DECBE, pages
// D_801DDA9C, rows on the page D_801DEC56), six rows a page; row D_801DEC58 (copy
// D_801DDA34) on 1-based page D_801DD20C. D_801DEBD2 is the weapon index to put the
// cursor back on; D_801DD20A / D_801DEC5A name the upgraded / unlocked weapon of the
// full-upgrade bonus message (999 none). The confirm screen keeps the price in
// D_801DEBCC and the previewed power in D_801DECD0.
constexpr uint32_t weapon_list_build_fn=0x801C7254,weapon_rows=0x801DEC68,weapon_count=0x801DECBE,weapon_pages=0x801DDA9C,weapon_page_rows=0x801DEC56,
    weapon_cursor=0x801DEC58,weapon_row_copy=0x801DDA34,weapon_page=0x801DD20C,weapon_remembered=0x801DEBD2,bonus_upgraded=0x801DD20A,bonus_unlocked=0x801DEC5A,
    weapon_preview=0x801DECD0;
constexpr unsigned weapon_rows_per_page=6;
// Runtime weapon record (0x24 bytes): +2 number, +4 flags, +6 power, +8/+9 range,
// +0xA hit, +0xB/+0xC ammo (max unconfirmed), +0xD EN, +0xE morale, +0xF skill text,
// +0x10.. terrain ranks, +0x14 critical, +0x15 type, +0x16 level.
constexpr uint16_t button_a=0x8000,button_b=0x4000;
constexpr unsigned sound_confirm=0xB7,sound_cancel=0xB8,sound_move=0xB9;
// Texts: 0xFCE ユニット改造 / 0xFCF 武器改造, 0xFED どの能力を改造しますか?,
// 0xFEE (最大で  段階まで), 0x1019 資金, 0xFEF 費用, 0xFE7.. HP EN 運動性 装甲 限界,
// 0xFEC パイロット, 0x102F これ以上の改造はできません, 0x1030 資金が足りません.
constexpr uint16_t text_stats_title=0xFCE,text_weapons_title=0xFCF,text_question=0xFED,text_cap=0xFEE,text_funds=0x1019,text_price=0xFEF,
    text_stat=0xFE7,text_pilot=0xFEC,text_maxed=0x102F,text_poor=0x1030,text_unit_names=0x20F,text_pilot_names=4382,
    text_ask=0xFE4,text_yes=0xFE5,text_no=0xFE6,text_weapon_names=0xA8B,text_weapon_pure_names=0x55A,
    text_bonus_a=0x10AC,text_bonus_b=0x10AD,text_bonus_c=0x10AE,text_power=0xFF2;
// Text 0xFF1 武器名 .. 0xFFE クリティカル補正 head the weapon list's columns and details.
constexpr uint16_t text_weapon_labels=0xFF1;
json art={{"units",json::object()}};

std::mutex mutex;
json current={{"visible",false}};
uint64_t serial{};
std::string pending,locale;
bool active{};
std::atomic_bool owning{},window_owning{};
uint16_t held{};
// The modern way to upgrade: ←→ set how many levels each stat (or the weapon on its
// confirm screen) is to gain, one confirm then runs the original's paid step that many
// times. `planned` marks a plan the confirm window made from a bare A, undone by いいえ.
std::array<unsigned,up::stat_count> plans{};
unsigned weapon_plan{};
bool planned{};
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.upgrade-page-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",serial}};
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
void fit_plans(uint32_t money);
// Typed funds apply to whichever screen is up; the stat window decisions read the
// new figure live, and a standing plan shrinks to what the new figure covers.
bool set_funds(uint8_t* ram,const std::string& action) {
    uint32_t value;
    if(!intermission_page::parse_funds(action,value))return false;
    write32(ram,funds,value);current["funds"]=value;record("funds",{{"funds",value}});
    fit_plans(value);
    return true;
}

// --- Snapshots -------------------------------------------------------------------

// The unit's shown stats: the original recomputes it and lets 801C4DE4 add the
// equipped parts before every draw.
void refresh_display(uint8_t* ram,recomp_context* ctx,uint32_t unit) {
    call(ram,ctx,resident_func_800A5254,unit,2);
    call(ram,ctx,display_stats,unit);
}
json unit_row(uint8_t* ram,recomp_context* ctx,uint32_t slot) {
    const uint32_t unit=up::unit_at(slot);
    refresh_display(ram,ctx,unit);
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    json row={{"slot",slot},{"number",number},{"name",text(ram,text_unit_names+number)},
        {"hp",read(ram,display_hp,2)},{"en",read(ram,unit+0x0A,2)},{"mobility",read(ram,display_mobility,2)},
        {"armor",read(ram,display_armor,2)},{"limit",read(ram,display_limit,2)}};
    // Pilot: the first crew pointer (+0x38); 801C6F80 prints -------- without one.
    const uint32_t pilot=read(ram,unit+0x38,4);
    row["pilot"]=pilot && valid(pilot,4)?text(ram,text_pilot_names+read(ram,pilot+2,2)):std::string();
    return row;
}
// Gauge cells as the rule draws them: ▶▷ up to the original cap, ●☆ beyond; every
// cell to the raised cap with the rule on, else only as far as reached or planned.
// '>' / '*' are reached levels, '+' / '#' planned ones, '.' / 'o' the rest.
std::string gauge_cells(unsigned original,unsigned allowed,unsigned level,unsigned plan) {
    const unsigned cells=up::breaking()?allowed:std::max(original,level+plan);
    std::string gauge;
    for(unsigned i=0;i<cells;++i)gauge+=i<original?(i<level?'>':i<level+plan?'+':'.'):(i<level?'*':i<level+plan?'#':'o');
    return gauge;
}
json stat_rows(uint8_t* ram,recomp_context* ctx,uint32_t unit) {
    refresh_display(ram,ctx,unit);
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    const unsigned original=up::original_cap(number),allowed=up::allowed_cap(number);
    const uint32_t values[]={read(ram,display_hp,2),read(ram,unit+0x0A,2),read(ram,display_mobility,2),read(ram,display_armor,2),read(ram,display_limit,2)};
    json rows=json::array();
    for(unsigned stat=0;stat<up::stat_count;++stat) {
        const unsigned level=read(ram,unit+up::unit_levels+stat,1);
        json row={{"name",text(ram,text_stat+stat)},{"value",values[stat]},{"level",level},{"cap",allowed},{"original_cap",original}};
        if(level<allowed) {
            row["preview"]=values[stat]+read(ram,up::stat_previews+stat*up::stat_previews_stride+level*2,2);
            row["price"]=read(ram,up::stat_prices+stat*up::stat_prices_stride+level*4,4);
        }
        // Each level still open: the value once it is reached and what it costs.
        json steps=json::array();
        uint32_t value=values[stat];
        for(unsigned l=level;l<allowed && l<up::levels;++l) {
            value+=read(ram,up::stat_previews+stat*up::stat_previews_stride+l*2,2);
            steps.push_back({{"value",value},{"price",read(ram,up::stat_prices+stat*up::stat_prices_stride+l*4,4)}});
        }
        row["steps"]=steps;
        row["gauge"]=gauge_cells(original,allowed,level,0);
        rows.push_back(row);
    }
    return rows;
}
// The plans written into the snapshot: each row's "plan" and planned gauge cells,
// "target" (the value or power the plan reaches) and the page's "plan_cost".
void show_plans() {
    const auto screen=current.value("screen",std::string());
    const auto mark=[](json& row,unsigned plan,const char* key) {
        const auto& steps=row.value("steps",json::array());
        plan=std::min<unsigned>(plan,unsigned(steps.size()));
        uint32_t cost=0;
        for(unsigned n=0;n<plan;++n)cost+=steps[n].value("price",0u);
        row["plan"]=plan;
        row["gauge"]=gauge_cells(row.value("original_cap",0u),row.value("cap",0u),row.value("level",0u),plan);
        if(plan)row["target"]=steps[plan-1].at(key);else row.erase("target");
        return cost;
    };
    if(screen=="stats" && current.contains("rows")) {
        uint32_t cost=0;
        for(unsigned n=0;n<current["rows"].size() && n<up::stat_count;++n)cost+=mark(current["rows"][n],plans[n],"value");
        current["plan_cost"]=cost;
    } else if(screen=="weapon" && current.contains("weapon"))current["plan_cost"]=mark(current["weapon"],weapon_plan,"power");
}
// How many more levels a plan may take: up to the open levels, and as far as the
// funds cover with every other row's plan paid.
uint32_t step_price(const json& row,unsigned n){return row.value("steps",json::array()).at(n).value("price",0u);}
unsigned open_steps(const json& row){return unsigned(row.value("steps",json::array()).size());}
// Funds typed while a plan stands: the plan shrinks, last rows first, to what is left.
void fit_plans(uint32_t money) {
    show_plans();
    const auto screen=current.value("screen",std::string());
    if(screen=="stats")
        for(unsigned n=up::stat_count;n-- && current.value("plan_cost",0u)>money;)
            while(plans[n] && current.value("plan_cost",0u)>money){--plans[n];show_plans();}
    if(screen=="weapon")
        while(weapon_plan>1 && current.value("plan_cost",0u)>money){--weapon_plan;show_plans();}
}

// 800A6104(1, rank): 1 'D', 2 'C', 3 'B', 4 'A', else '-' (verified against the
// original 武器一覧: ダイターンミサイル prints A A A A).
char terrain_letter(unsigned rank){return rank==1?'D':rank==2?'C':rank==3?'B':rank==4?'A':'-';}
void labels(const uint8_t* ram) {
    const bool weapons=current.value("kind",std::string())=="weapons";
    const char* weapon_keys[]={"weapon","power","range","hit","ammo","terrain","air","land","sea","space","morale","en","skill","critical"};
    json wl;
    for(unsigned n=0;n<14;++n)wl[weapon_keys[n]]=text(ram,text_weapon_labels+n);
    wl["icons"]=art.value("weapon_markers",json::object());
    current["weapon_labels"]=wl;
    current["bonus_labels"]={text(ram,text_bonus_a),text(ram,text_bonus_b),text(ram,text_bonus_c)};
    current["title"]=text(ram,weapons?text_weapons_title:text_stats_title);
    current["labels"]={{"funds",text(ram,text_funds)},{"price",text(ram,text_price)},{"pilot",text(ram,text_pilot)},
        {"question",text(ram,text_question)},{"cap",text(ram,text_cap)},{"maxed",text(ram,text_maxed)},{"poor",text(ram,text_poor)},
        {"ask",text(ram,text_ask)},{"yes",text(ram,text_yes)},{"no",text(ram,text_no)},
        {"hp",text(ram,text_stat)},{"en",text(ram,text_stat+1)},{"mobility",text(ram,text_stat+2)},{"armor",text(ram,text_stat+3)},{"limit",text(ram,text_stat+4)}};
    locale=localization::catalog().locale;
}
void open(json page,uint8_t* ram) {
    page["visible"]=true;page["serial"]=++serial;page["funds"]=read(ram,funds,4);
    current=std::move(page);labels(ram);
    active=true;owning=true;pending.clear();
    record("open",{{"screen",current.value("screen",std::string())}});
}

json list_page_json(uint8_t* ram,recomp_context* ctx);
json stats_json(uint8_t* ram,recomp_context* ctx);
json weapon_list_json(uint8_t* ram);
json chosen_weapon(const uint8_t* ram);
json bonus_json(const uint8_t* ram,unsigned upgraded,unsigned unlocked);
// A language switch while a screen is open: its names are rebuilt in the new
// language; what the page itself tracks (window, cursor, serial) stays.
void relocalize(uint8_t* ram,recomp_context* ctx) {
    if(locale==localization::catalog().locale)return;
    const auto screen=current.value("screen",std::string());
    const json next=screen=="list"?list_page_json(ram,ctx):screen=="stats"?stats_json(ram,ctx):screen=="weapons"?weapon_list_json(ram):json::object();
    for(const char* key:{"unit","rows"})if(next.contains(key))current[key]=next[key];
    if(screen=="weapon"){current["weapon"]=chosen_weapon(ram);current["unit"]["name"]=text(ram,text_unit_names+current["unit"].value("number",0u));}
    show_plans();
    if(current.contains("bonus")){const auto& ids=current["bonus"]["numbers"];current["bonus"]=bonus_json(ram,ids[0].get<unsigned>(),ids[1].get<unsigned>());}
    labels(ram);
}

// --- Machine list (screens 2 and 3) ----------------------------------------------

unsigned list_size(const uint8_t* ram){return std::min<unsigned>(read(ram,list_count,2),up::unit_slots);}
void list_select(uint8_t* ram,recomp_context* ctx) {
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    const unsigned index=page*rows_per_page+unsigned(int16_t(read(ram,list_row,2)));
    if(index>=list_size(ram))return;
    const uint32_t slot=read(ram,list_slots+index*2,2);
    write32(ram,screen_unit,slot);
    refresh_display(ram,ctx,up::unit_at(slot));
}
json list_page_json(uint8_t* ram,recomp_context* ctx) {
    const unsigned count=list_size(ram),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1;
    json rows=json::array();
    for(unsigned n=page*rows_per_page;n<count && n<(page+1)*rows_per_page;++n)rows.push_back(unit_row(ram,ctx,read(ram,list_slots+n*2,2)));
    return {{"screen","list"},{"kind",current.value("kind",std::string())},{"page",page},{"pages",(count+rows_per_page-1)/rows_per_page},
        {"cursor",read(ram,list_row,2)},{"rows",rows}};
}
// 801CF388 / 801D03D0 without their drawing: background (its random call included),
// the list, the cursor back on the unit remembered from last time, the selection.
// The original screens when the setting says so: any open page closes first.
bool original_screens() {
    if(settings::native_intermission_ui())return false;
    std::lock_guard lock(mutex);hide("original");return true;
}
bool list_build(uint8_t* ram,recomp_context* ctx,bool weapons) {
    if(original_screens())return false;
    call(ram,ctx,resident_func_80085B94,0,weapons?1:0);
    call(ram,ctx,weapons?list_build_weapons:list_build_stats);
    const unsigned count=list_size(ram),remembered=read(ram,list_remembered,2);
    if(remembered!=999)for(unsigned n=0;n<count;++n)
        if(read(ram,up::unit_at(read(ram,list_slots+n*2,2))+up::unit_number,2)==remembered) {
            write16(ram,list_page,uint16_t(n/rows_per_page+1));write16(ram,list_row,uint16_t(n%rows_per_page));break;
        }
    write16(ram,list_row_copy,uint16_t(read(ram,list_row,2)));
    list_select(ram,ctx);
    std::lock_guard lock(mutex);
    current["kind"]=weapons?"weapons":"stats";
    json page=list_page_json(ram,ctx);page["kind"]=weapons?"weapons":"stats";
    open(std::move(page),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool list_step(uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="list")return false;
    relocalize(ram,ctx);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    if(set_funds(ram,action))return false;
    const unsigned count=list_size(ram),pages=(count+rows_per_page-1)/rows_per_page;
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,list_page,2))))-1,row=unsigned(int16_t(read(ram,list_row,2)));
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*rows_per_page) || value>=rows_per_page || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*rows_per_page-1);}
        write16(ram,list_page,uint16_t(page+1));write16(ram,list_row,uint16_t(row));write16(ram,list_row_copy,uint16_t(row));
        list_select(ram,ctx);
        auto next=list_page_json(ram,ctx);next["kind"]=current["kind"];next["visible"]=true;next["serial"]=current["serial"];next["funds"]=current["funds"];
        for(const char* key:{"title","labels"})next[key]=current[key];
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

// --- Five stats (screen 10) -------------------------------------------------------

json stats_json(uint8_t* ram,recomp_context* ctx) {
    const uint32_t slot=read(ram,screen_unit,4),unit=up::unit_at(slot);
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    return {{"screen","stats"},{"kind","stats"},{"unit",{{"slot",slot},{"number",number},{"name",text(ram,text_unit_names+number)},{"art",art["units"].value(std::to_string(number),json::object())}}},
        {"cap",up::allowed_cap(number)},{"cursor",read(ram,stat_cursor,2)},{"rows",stat_rows(ram,ctx,unit)},{"window",""}};
}
// 801CF680 without its drawing: dimmed background, fresh stats, the state resets.
bool stats_build(uint8_t* ram,recomp_context* ctx) {
    if(original_screens())return false;
    call(ram,ctx,resident_func_80085B94,0,1);
    focus_lines::show(ram,ctx);
    // D_801DEC50 is the row whose price the original step last drew; keeping it equal
    // to the cursor stops the step from drawing the price itself.
    write8(ram,0x801DDA30,2);write32(ram,mode,0);write32(ram,stat_drawn,read(ram,stat_cursor,2));write8(ram,0x801DECC1,0);write8(ram,0x801DECC0,0);
    std::lock_guard lock(mutex);
    plans={};planned=false;
    open(stats_json(ram,ctx),ram);show_plans();
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
// Runs the original paid step (one A edge on its confirm window) for one more level
// of `stat`. Every step after the first in a frame finds the fade the last one began:
// marking it idle lets the step run, and 80099814 then frees that fade and starts its
// own (its replace path), so a whole plan still ends in one fade and one re-entry.
void pay_stat_level(uint8_t* ram,recomp_context* ctx,unsigned stat,uint32_t price,bool first) {
    write16(ram,stat_cursor,uint16_t(stat));write32(ram,stat_drawn,stat);write32(ram,stat_price,price);
    write32(ram,mode,1);write16(ram,confirm_cursor,0);
    for(unsigned n=0;n<4;++n)write8(ram,confirm_texts+n,0);
    if(!first)write8(ram,transition,0xFF);
    feed(ram,ctx,srw64_original_upgrade_stats_step,button_a);
}
bool stats_step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="stats")return false;
    relocalize(ram,ctx);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    if(set_funds(ram,action))return false;
    const auto window=current.value("window",std::string());
    const auto& rows=current.at("rows");
    const bool any_plan=std::any_of(plans.begin(),plans.end(),[](unsigned n){return n>0;});
    if(action.starts_with("move:") && window.empty()) {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>=up::stat_count || index==current.value("cursor",0u))return false;
        write16(ram,stat_cursor,uint16_t(index));write32(ram,stat_drawn,index);current["cursor"]=index;
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if((action.starts_with("more:") || action.starts_with("less:")) && window.empty()) {
        // ←→ on a row: one level more or less in its plan, the cursor moving to it.
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>=up::stat_count)return false;
        write16(ram,stat_cursor,uint16_t(index));write32(ram,stat_drawn,index);current["cursor"]=index;
        const auto& row=rows.at(index);
        bool changed=false;planned=false;
        if(action[0]=='m') {
            if(plans[index]<open_steps(row) && current.value("plan_cost",0u)+step_price(row,plans[index])<=read(ram,funds,4)){++plans[index];changed=true;}
        } else if(plans[index]){--plans[index];changed=true;}
        show_plans();
        record("plan",{{"index",index},{"plan",plans[index]},{"cost",current["plan_cost"]}});
        lock.unlock();sound(ram,ctx,changed?sound_move:sound_cancel);return false;
    }
    if(action.starts_with("choose:") && window.empty()) {
        // A: with a plan standing, the はい/いいえ window for all of it. Without one, as
        // 801CF988 decides for its row: below the cap and affordable plans one level and
        // opens the window, otherwise the message and the buzzer.
        const unsigned index=unsigned(std::atoi(action.c_str()+7));
        if(index>=up::stat_count)return false;
        write16(ram,stat_cursor,uint16_t(index));write32(ram,stat_drawn,index);current["cursor"]=index;
        const auto& row=rows.at(index);
        const bool can=any_plan || (row.contains("price") && read(ram,funds,4)>=row.at("price").get<uint32_t>());
        if(can && !any_plan){plans[index]=1;planned=true;show_plans();}
        current["window"]=can?"confirm":row.contains("price")?"poor":"maxed";
        record("window",{{"index",index},{"window",current["window"]}});
        lock.unlock();sound(ram,ctx,can?sound_confirm:sound_cancel);return false;
    }
    if((action=="cancel" || action=="dismiss") && !window.empty()) {
        if(planned){plans={};planned=false;show_plans();}
        current["window"]="";
        lock.unlock();sound(ram,ctx,sound_cancel);return false;
    }
    if(action=="confirm" && window=="confirm") {
        // The window's はい branch, once for every planned level: it pays the price the
        // step computed when it drew it (D_801DEBCC, set here instead), adds the preview,
        // raises the level, propagates to the unit's forms, runs the EW check, and
        // re-enters the screen. Prices and funds are read again before each level; an
        // EW swap (next screen no longer 10) ends the plan where it stands.
        const uint32_t unit=up::unit_at(read(ram,screen_unit,4));
        const unsigned allowed=up::allowed_cap(uint16_t(read(ram,unit+up::unit_number,2))),cursor=current.value("cursor",0u);
        const auto chosen=plans;
        lock.unlock();
        unsigned paid=0;
        for(unsigned stat=0;stat<up::stat_count;++stat)
            for(unsigned n=0;n<chosen[stat];++n) {
                const unsigned level=read(ram,unit+up::unit_levels+stat,1);
                if(level>=allowed || level>=up::levels)break;
                const uint32_t price=read(ram,up::stat_prices+stat*up::stat_prices_stride+level*4,4);
                if(read(ram,funds,4)<price || (paid && read(ram,next_screen,4)!=10))goto done;
                pay_stat_level(ram,ctx,stat,price,paid==0);++paid;
            }
        done:
        // The re-entered screen keeps the player's row.
        if(read(ram,next_screen,4)==10){write16(ram,stat_cursor,uint16_t(cursor));write32(ram,stat_drawn,cursor);}
        const bool left=!idle(ram);
        lock.lock();record("confirm",{{"plans",chosen},{"paid",paid},{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");else{write32(ram,mode,0);current["window"]="";planned=false;}
        return true;
    }
    if(action=="back" && window.empty()) {
        // B first drops a standing plan, then leaves.
        if(any_plan) {
            plans={};show_plans();record("plan-clear");
            lock.unlock();sound(ram,ctx,sound_cancel);return false;
        }
        lock.unlock();
        const bool left=feed(ram,ctx,srw64_original_upgrade_stats_step,button_b);
        lock.lock();record("back",{{"left",left}});
        if(left)hide("close");
        return true;
    }
    return false;
}
// --- Weapon list (screen 11) and confirm (screen 12) -------------------------------

uint32_t weapon_at(const uint8_t* ram,uint32_t unit,unsigned index) {
    const uint32_t list=read(ram,unit+up::unit_weapon_list,4);
    return list+index*up::weapon_size;
}
}
// The menu string is "prefix + pure name + suffix" (weapon_traits.py): 格／射 in
// front, P／B／MAP behind; the original prints the markers as round icons. Some pure
// names already end in MAP, then the menu inserts B before it.
void weapon_markers(json& row,const std::string& pure) {
    const std::string menu=row.value("name",std::string());
    const char* prefixes[]={"格","射",""};
    struct Suffix{const char* text;std::vector<std::string> tokens;};
    const Suffix suffixes[]={{"",{}},{"P",{"P"}},{"B",{"B"}},{"PB",{"P","B"}},{"MAP",{"MAP"}},{"BMAP",{"B","MAP"}},{"PBMAP",{"P","B","MAP"}}};
    std::vector<std::pair<std::string,bool>> candidates={{pure,false}};
    if(pure.size()>3 && pure.compare(pure.size()-3,3,"MAP")==0)candidates.insert(candidates.begin(),{pure.substr(0,pure.size()-3),true});
    for(const auto& [display,requires_map]:candidates)for(const char* prefix:prefixes)for(const auto& suffix:suffixes) {
        if(requires_map && std::find(suffix.tokens.begin(),suffix.tokens.end(),"MAP")==suffix.tokens.end())continue;
        if(!display.empty() && menu==std::string(prefix)+display+suffix.text) {
            json markers=json::array();
            if(*prefix)markers.push_back(prefix);
            for(const auto& token:suffix.tokens)markers.push_back(token);
            row["display_name"]=display;row["markers"]=markers;
            return;
        }
    }
    row["display_name"]=menu;row["markers"]=json::array();
}
namespace {
json weapon_row(const uint8_t* ram,uint32_t unit,unsigned index) {
    const uint32_t w=weapon_at(ram,unit,index);
    const uint16_t number=uint16_t(read(ram,w+2,2));
    const unsigned original=up::original_cap(uint16_t(read(ram,unit+up::unit_number,2))),allowed=up::allowed_cap(uint16_t(read(ram,unit+up::unit_number,2)));
    const unsigned level=read(ram,w+0x16,1),type=read(ram,w+0x15,1);
    json row={{"index",index},{"number",number},{"name",text(ram,text_weapon_names+number)},{"power",read(ram,w+6,2)},
        {"range_min",read(ram,w+8,1)},{"range_max",read(ram,w+9,1)},{"hit",int8_t(read(ram,w+0xA,1))},{"critical",int8_t(read(ram,w+0x14,1))},
        {"en",read(ram,w+0xD,1)},{"morale",read(ram,w+0xE,1)},{"skill",read(ram,w+0xF,1)},{"level",level},{"type",type},{"cap",allowed},{"original_cap",original}};
    const int ammo=int8_t(read(ram,w+0xC,1));
    if(ammo>=0){row["ammo"]=ammo;row["ammo_max"]=read(ram,w+0xB,1);}
    std::string terrain;
    for(unsigned n=0;n<4;++n)terrain+=terrain_letter(read(ram,w+0x10+n,1));
    row["terrain"]=terrain;
    if(row["skill"].get<unsigned>()>=2)row["skill_name"]=text(ram,uint16_t(row["skill"].get<unsigned>()));
    row["gauge"]=gauge_cells(original,allowed,level,0);
    if(type>=1 && type<=4 && level<allowed && level<up::levels) {
        row["price"]=read(ram,up::weapon_prices[type-1]+level*4,4);
        row["preview"]=read(ram,w+6,2)+read(ram,up::weapon_previews[type-1]+level*2,2);
        json steps=json::array();
        uint32_t power=read(ram,w+6,2);
        for(unsigned l=level;l<allowed && l<up::levels;++l) {
            power+=read(ram,up::weapon_previews[type-1]+l*2,2);
            steps.push_back({{"power",power},{"price",read(ram,up::weapon_prices[type-1]+l*4,4)}});
        }
        row["steps"]=steps;
    } else if(type==0)row["price"]=0;
    weapon_markers(row,text(ram,uint16_t(text_weapon_pure_names+number)));
    return row;
}
// The full-upgrade message names both weapons without their markers.
json bonus_json(const uint8_t* ram,unsigned upgraded,unsigned unlocked) {
    return {{"upgraded",text(ram,uint16_t(text_weapon_pure_names+upgraded))},{"unlocked",text(ram,uint16_t(text_weapon_pure_names+unlocked))},
        {"numbers",{upgraded,unlocked}}};
}
json weapon_list_json(uint8_t* ram) {
    const uint32_t slot=read(ram,screen_unit,4),unit=up::unit_at(slot);
    const unsigned count=read(ram,weapon_count,2),page=unsigned(std::max<int16_t>(1,int16_t(read(ram,weapon_page,2))))-1;
    const uint32_t pilot=read(ram,unit+0x38,4);
    json rows=json::array();
    for(unsigned n=page*weapon_rows_per_page;n<count && n<(page+1)*weapon_rows_per_page;++n)rows.push_back(weapon_row(ram,unit,read(ram,weapon_rows+n*2,2)));
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    return {{"screen","weapons"},{"kind","weapons"},{"unit",{{"slot",slot},{"number",number},{"name",text(ram,text_unit_names+number)},
        {"en",read(ram,unit+0x08,2)},{"morale",pilot && valid(pilot,0x24)?int(read(ram,pilot+0x20,2)):-1}}},
        {"page",page},{"pages",(count+weapon_rows_per_page-1)/weapon_rows_per_page},{"cursor",read(ram,weapon_cursor,2)},{"rows",rows},{"window",""}};
}
// 801D0600 without its drawing: background, the list, the bonus message after a
// full upgrade, the cursor back on the weapon upgraded last.
bool weapon_list_build(uint8_t* ram,recomp_context* ctx) {
    if(original_screens())return false;
    call(ram,ctx,resident_func_80085B94,0,1);
    call(ram,ctx,weapon_list_build_fn);
    const unsigned unlocked=read(ram,bonus_unlocked,2),upgraded=read(ram,bonus_upgraded,2);
    const bool bonus=unlocked!=999;
    write32(ram,mode,bonus?1:0);
    const unsigned remembered=read(ram,weapon_remembered,2),count=read(ram,weapon_count,2);
    if(remembered!=999)for(unsigned n=0;n<count;++n)if(read(ram,weapon_rows+n*2,2)==remembered) {
        write16(ram,weapon_page,uint16_t(n/weapon_rows_per_page+1));write16(ram,weapon_cursor,uint16_t(n%weapon_rows_per_page));break;
    }
    write16(ram,bonus_upgraded,999);write16(ram,bonus_unlocked,999);
    write16(ram,weapon_row_copy,uint16_t(read(ram,weapon_cursor,2)));
    std::lock_guard lock(mutex);
    auto page=weapon_list_json(ram);
    if(bonus){page["window"]="bonus";page["bonus"]=bonus_json(ram,upgraded,unlocked);}
    open(std::move(page),ram);
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool weapon_list_step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="weapons")return false;
    relocalize(ram,ctx);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    if(set_funds(ram,action))return false;
    const auto window=current.value("window",std::string());
    const unsigned count=read(ram,weapon_count,2),pages=(count+weapon_rows_per_page-1)/weapon_rows_per_page;
    unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,weapon_page,2))))-1,row=unsigned(int16_t(read(ram,weapon_cursor,2)));
    if(action=="dismiss" && window=="bonus") {
        // As A on the message (801D09C0): it only frees what it drew.
        write32(ram,mode,0);current["window"]="";
        lock.unlock();sound(ram,ctx,sound_cancel);return false;
    }
    if(!window.empty())return false;
    if(action.starts_with("move:") || action.starts_with("page:")) {
        const unsigned value=unsigned(std::atoi(action.c_str()+5));
        if(action[0]=='m'){if(value>=count-std::min(count,page*weapon_rows_per_page) || value>=weapon_rows_per_page || value==row)return false;row=value;}
        else{if(value>=pages || value==page)return false;page=value;row=std::min(row,count-page*weapon_rows_per_page-1);}
        write16(ram,weapon_page,uint16_t(page+1));write16(ram,weapon_cursor,uint16_t(row));write16(ram,weapon_row_copy,uint16_t(row));
        auto next=weapon_list_json(ram);next["visible"]=true;next["serial"]=current["serial"];next["funds"]=current["funds"];
        for(const char* key:{"title","labels","weapon_labels","bonus_labels"})next[key]=current[key];
        current=std::move(next);
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if(action=="choose" || action=="back") {
        lock.unlock();
        const bool left=feed(ram,ctx,srw64_original_weapon_screen_step,action=="choose"?button_a:button_b);
        lock.lock();record(action.c_str(),{{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    return false;
}
json chosen_weapon(const uint8_t* ram) {
    const uint32_t unit=up::unit_at(read(ram,screen_unit,4));
    const unsigned page=unsigned(std::max<int16_t>(1,int16_t(read(ram,weapon_page,2))))-1;
    const unsigned n=page*weapon_rows_per_page+unsigned(int16_t(read(ram,weapon_row_copy,2)));
    return weapon_row(ram,unit,read(ram,weapon_rows+n*2,2));
}
// 801D0C7C without its drawing: background, the price and previewed power the
// confirmed branch applies; the page shows はい/いいえ or これ以上の改造はできません.
bool weapon_confirm_build(uint8_t* ram,recomp_context* ctx) {
    if(original_screens())return false;
    call(ram,ctx,resident_func_80085B94,0,1);
    const uint32_t slot=read(ram,screen_unit,4),unit=up::unit_at(slot);
    json row=chosen_weapon(ram);
    write32(ram,stat_price,row.value("price",0u));write16(ram,weapon_preview,uint16_t(row.value("preview",row.value("power",0u))));
    write32(ram,mode,0);write8(ram,0x801DECC0,0);
    const uint16_t number=uint16_t(read(ram,unit+up::unit_number,2));
    std::lock_guard lock(mutex);
    const bool maxed=!row.contains("preview");
    const uint32_t pilot=read(ram,unit+0x38,4);
    open({{"screen","weapon"},{"kind","weapons"},{"unit",{{"slot",slot},{"number",number},{"name",text(ram,text_unit_names+number)},
        {"en",read(ram,unit+0x08,2)},{"morale",pilot && valid(pilot,0x24)?int(read(ram,pilot+0x20,2)):-1}}},
        {"weapon",row},{"cursor",0},{"window",maxed?"maxed":"confirm"}},ram);
    weapon_plan=maxed?0:1;show_plans();
    call(ram,ctx,resident_func_80099814,4,2,0);
    return true;
}
bool weapon_confirm_step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(!active || current.value("screen",std::string())!="weapon")return false;
    relocalize(ram,ctx);
    if(pending.empty() || !idle(ram))return false;
    const auto action=std::move(pending);pending.clear();
    if(set_funds(ram,action))return false;
    const auto window=current.value("window",std::string());
    if(action.starts_with("move:") && window=="confirm") {
        const unsigned index=unsigned(std::atoi(action.c_str()+5));
        if(index>1 || index==current.value("cursor",0u))return false;
        write16(ram,confirm_cursor,uint16_t(index));current["cursor"]=index;
        lock.unlock();sound(ram,ctx,sound_move);return false;
    }
    if((action=="more" || action=="less") && window=="confirm") {
        // ←→: one level more or less, from one up to the open levels the funds cover.
        const auto& w=current.at("weapon");
        bool changed=false;
        if(action=="more") {
            if(weapon_plan<open_steps(w) && current.value("plan_cost",0u)+step_price(w,weapon_plan)<=read(ram,funds,4)){++weapon_plan;changed=true;}
        } else if(weapon_plan>1){--weapon_plan;changed=true;}
        show_plans();
        record("plan",{{"plan",weapon_plan},{"cost",current["plan_cost"]}});
        lock.unlock();sound(ram,ctx,changed?sound_move:sound_cancel);return false;
    }
    if(action=="confirm" && window=="confirm") {
        const auto& w=current.at("weapon");
        if(read(ram,funds,4)<w.value("price",0u)){current["window"]="poor";lock.unlock();sound(ram,ctx,sound_cancel);return false;}
        // はい, once for every planned level: the original pays, writes the previewed
        // power, raises the level, syncs the twin weapon, unlocks the full-upgrade bonus
        // and re-enters this confirm screen (next screen 12) for the same weapon, as the
        // stat screen re-enters. Price and power are set again before each level (as the
        // re-entered screen would); the bonus (next screen 11) ends the plan. Every level
        // after the first finds the last one's fade, handled as in pay_stat_level.
        const uint32_t unit=up::unit_at(read(ram,screen_unit,4));
        const uint32_t record_at=weapon_at(ram,unit,w.value("index",0u));
        const unsigned allowed=up::allowed_cap(uint16_t(read(ram,unit+up::unit_number,2))),plan=std::max(1u,weapon_plan);
        lock.unlock();
        unsigned paid=0;
        for(;paid<plan;++paid) {
            const unsigned level=read(ram,record_at+0x16,1),type=read(ram,record_at+0x15,1);
            if(type<1 || type>4 || level>=allowed || level>=up::levels)break;
            const uint32_t price=read(ram,up::weapon_prices[type-1]+level*4,4);
            if(read(ram,funds,4)<price || (paid && read(ram,next_screen,4)!=12))break;
            write32(ram,stat_price,price);write16(ram,weapon_preview,uint16_t(read(ram,record_at+6,2)+read(ram,up::weapon_previews[type-1]+level*2,2)));
            write32(ram,mode,0);write16(ram,confirm_cursor,0);
            if(paid)write8(ram,transition,0xFF);
            feed(ram,ctx,srw64_original_upgrade_weapon_step,button_a);
        }
        const bool left=!idle(ram);
        lock.lock();record("weapon-confirm",{{"plan",plan},{"paid",paid},{"left",left},{"next",read(ram,next_screen,4)}});
        if(left)hide("close");
        return true;
    }
    if(action=="cancel" || action=="dismiss" || action=="back") {
        // いいえ, and closing either message: B takes the original back to the list.
        lock.unlock();
        const bool left=feed(ram,ctx,srw64_original_upgrade_weapon_step,button_b);
        lock.lock();record("weapon-back",{{"left",left}});
        if(left)hide("close");
        return true;
    }
    return false;
}
void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    // Any other screen taking over (the EW swap, the menu) closes the page.
    if(active && !idle(ram) && read(ram,next_screen,4)!=read(ram,current_screen,4))hide("left");
}
}

void configure(const std::filesystem::path& directory) {
    if(const char* v=std::getenv("SRW64_NATIVE_UPGRADE");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return;
    std::ifstream source(std::getenv("SRW64_DIALOGUE_DATA"));
    const auto data=json::parse(source,nullptr,false);
    if(!data.is_discarded() && data.contains("battle_assets"))art=data.at("battle_assets");
    log.open(directory/"upgrade-page-events.jsonl");
    auto& h=srw64_game_hooks;
    h.upgrade_list_build=[](uint8_t* ram,recomp_context* ctx,bool weapons){return list_build(ram,ctx,weapons);};
    h.upgrade_list_step=[](uint8_t* ram,recomp_context* ctx,void(*step)(uint8_t*,recomp_context*)){return list_step(ram,ctx,step);};
    h.upgrade_stats_build=stats_build;h.upgrade_stats_step=stats_step;
    h.upgrade_stats_view=[](uint8_t*){return active;};
    h.weapon_list_build=weapon_list_build;h.weapon_list_step=weapon_list_step;
    h.weapon_confirm_build=weapon_confirm_build;h.weapon_confirm_step=weapon_confirm_step;
    h.upgrade_frame=frame;
}
json weapon_row_json(const uint8_t* ram,uint32_t unit,unsigned index){return weapon_row(ram,unit,index);}
json weapon_labels_json(const uint8_t* ram) {
    const char* weapon_keys[]={"weapon","power","range","hit","ammo","terrain","air","land","sea","space","morale","en","skill","critical"};
    json wl;
    for(unsigned n=0;n<14;++n)wl[weapon_keys[n]]=text(ram,text_weapon_labels+n);
    // The 格／射／P／B／MAP icons cut from the ROM font (battle_assets.py).
    wl["icons"]=art.value("weapon_markers",json::object());
    return wl;
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
