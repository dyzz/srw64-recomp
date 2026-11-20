#pragma once
#include "guest_memory.hpp"
#include "mod_ids.hpp"
#include "mod_library.hpp"
#include "json/json.hpp"
#include "localization/locale_fallbacks.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Story MODs (docs/design/mod-packages.md §7): story/*.json of the enabled packages,
// compiled by tools/mods/story.py, add events to the original stages. They take no
// place in the original's event table: the host decides when one fires, keeps what has
// fired with the suspend records, and hands only its instructions to the original
// script engine, started the way the original starts an event. Their new dialogue is
// served as text records past the ROM's (0xC800 up), shown by the native reader.
namespace srw64::story_mod {
namespace fs=std::filesystem;
using json=nlohmann::json;
using guest::read;
using guest::write16;
using guest::write32;

inline constexpr const char* image_schema="srw64.story-image.v1";
inline constexpr const char* state_schema="srw64.story-state.v1";
// The event engine (8015F950) and its script owner; where an event's words go while it runs.
inline constexpr uint32_t engine=0x8015F950, owner=engine+0x948, region=0x807E0000, region_size=0x10000;
inline constexpr uint32_t scene_byte=0x8010F5F0, suspend_buffer=0x800FBEF0, suspend_size=0x3AE0;
inline constexpr uint32_t header_words=5;   // an event's type and four parameters, before its words
// engine fields: status (0x80 idle, 0x2000+ an event started), poll phase, turn and side,
// unit counts, the persuader, the persuasion picked, the registered-event count.
inline constexpr uint32_t f_mode=0x4, f_status=0x97C, f_count=0x98E, f_persuade=0x992, f_phase=0x9AA,
    f_turn=0x9AC, f_side=0x9AE, f_enemies=0x9B1, f_third=0x9B2, f_battle_a=0x9A0, f_battle_b=0x9A4, f_persuader=0x9A8;
// A host persuasion in +0x992: never a slot number (0..15) nor -1.
inline constexpr uint16_t persuade_mark=0x7000;
// Text records of the packages: ids past table 0's 50,975, served from a virtual ROM
// range past the upgrade gauges' (0x03000000 + 0x10000 x 0x40).
inline constexpr uint16_t text_first=0xC800;
inline constexpr uint32_t text_table=0x01A34980, text_virtual=0x03400000, text_slot=0x400;
inline constexpr size_t longest_text=200;   // code points; the native reader pages it

enum class Trigger {turn,defeated,hp_below,battle_after,battle_before,enemies_gone,remaining,persuade,after_opening,after_setup,after_end,
    downed,group_gone,reach,turns_after};
inline std::optional<Trigger> trigger_of(const std::string& name) {
    static const std::map<std::string,Trigger> names{{"turn",Trigger::turn},{"defeated",Trigger::defeated},
        {"hp_below",Trigger::hp_below},{"battle_after",Trigger::battle_after},{"battle_before",Trigger::battle_before},
        {"enemies_gone",Trigger::enemies_gone},{"remaining",Trigger::remaining},{"persuade",Trigger::persuade},
        {"after_opening",Trigger::after_opening},{"after_setup",Trigger::after_setup},{"after_end",Trigger::after_end},
        {"downed",Trigger::downed},{"group_gone",Trigger::group_gone},{"reach",Trigger::reach},{"turns_after",Trigger::turns_after}};
    const auto found=names.find(name);
    return found==names.end()?std::nullopt:std::optional(found->second);
}
// The original's event type a trigger stands for, which the poll scans in its phases (8009E180).
inline int event_type(Trigger t) {
    switch(t) {
        case Trigger::turn: return 0;
        case Trigger::turns_after: return 1;   // the original's delayed count (3D52)
        case Trigger::defeated: case Trigger::downed: case Trigger::group_gone: return 2;
        case Trigger::hp_below: return 3;
        case Trigger::battle_after: return 4;
        case Trigger::battle_before: return 5;
        case Trigger::enemies_gone: return 6;
        case Trigger::remaining: return 7;
        case Trigger::reach: return 8;
        case Trigger::persuade: return 9;
        case Trigger::after_opening: return 12;
        case Trigger::after_setup: return 13;
        case Trigger::after_end: return 14;
    }
    return -1;
}
// The status the original's handler of that type writes when it starts the event.
inline uint16_t start_status(Trigger t) {
    switch(t) {
        case Trigger::defeated: case Trigger::hp_below: case Trigger::battle_after:
        case Trigger::downed: case Trigger::group_gone: return 0x2002;
        case Trigger::battle_before: return 0x2001;
        default: return 0x2000;
    }
}

// A condition the compiler wrote as a tree (web/src/lib/compiler/conditions.ts): an event's
// `if` beyond its older fields, or a step's `if` (Event::checks), decided by the host.
struct Test {
    enum class Kind {all,any,not_,var,kills,turn,status,hp,count,in_area,route,choice,funds,downed,roster,flag,fired,level,opponent};
    Kind kind{};
    std::vector<int> n;               // the numbers, as the compiler lists them
    std::string name;                 // flag, fired: package-relative
    std::vector<Test> sub;            // all, any, not
    std::vector<unsigned> pilots,units;   // roster
};
enum Status {on_map,defeated,absent};
// A unit on the map as the roster (8015E100) has it: side 0 ours, 1 enemy, 2 third.
struct MapUnit {unsigned side{},slot{},pilot{},unit{},x{},y{};};

struct Event {
    std::string package,id;          // key(): package/id
    // The custom campaign the event belongs to ("" the main story's): a campaign's stages
    // borrow the main story's scene numbers, so an event runs in its own of the two only.
    std::string campaign;
    unsigned scene{};
    Trigger trigger{};
    // turn: turn (0-based, at least), side; defeated, hp_below: who, percent; battle_*: a, b
    // (0 any); enemies_gone: side (4 any), last turn; remaining: third (1) or enemy, at most,
    // side (4 any); persuade: by, target.
    int a{},b{},c{};
    std::vector<int> params;                       // all of them (downed: 4, reach: 6)
    std::string after;                             // turns_after: the event (package-relative id) counted from
    std::optional<Test> test;                      // the `if` keys an older game does not know
    std::vector<std::pair<size_t,Test>> checks;    // a step's condition: its word, 0 or 1 before each step
    std::map<unsigned,unsigned> vars;              // all must hold (story variables 0..199)
    std::map<unsigned,unsigned> kills;             // pilot -> at least this many kills (our side)
    std::map<unsigned,unsigned> kills_below;       // pilot -> fewer kills than this (our side)
    std::vector<std::string> fired,not_fired;      // other events of the package (ids)
    // Across stages: our roster has (or lacks) these pilots and units; the package's
    // long-term flags are (or are not) set. set_flags: set when the event has run to its end.
    std::vector<unsigned> has_pilots,has_units,lacks_pilots,lacks_units;
    std::vector<std::string> flags,not_flags,set_flags;
    bool repeat{};
    std::vector<uint16_t> words;                   // instructions and FFFF, text ids final
    std::vector<std::pair<size_t,unsigned>> levels;   // a join's level operand -> as this pilot's
    // When the event has run to its end: these pilots become the crew of our unit of that
    // number, the first as its pilot (crew()).
    std::vector<std::pair<unsigned,std::vector<unsigned>>> crew;
    // When the event starts, before its commands: a package's combined unit gets our
    // instance (mod_combine.hpp build), so the event's own steps (unlock) reach it.
    std::vector<unsigned> builds;
    std::string key() const {return package+"/"+id;}
};
// A line, a choice's options (one per row, "<BR>" between them, 3D44) or a stage's own
// objective (the map menu's 作戦目的, in place of text 5694 / 5720 there).
enum class TextKind {line,choice,objective,battle};
struct Text {
    unsigned speaker{};
    std::map<std::string,std::string> lines;       // locale -> text
    TextKind kind{};
};
// A campaign stage's own winning and losing text (3D65 127,127 in its opening).
inline constexpr uint16_t objective_win=5567+127, objective_lose=5593+127;
// A hidden element a package adds to a scene's stage information page (stage_info.cpp):
// its title and text by locale, and when it shows as done (our roster has these).
struct Info {
    std::string package;
    unsigned scene{};
    json title,text;
    bool checks{};
    std::vector<unsigned> pilots,units;
};
struct Library {
    std::vector<Event> events;
    std::vector<Info> infos;
    std::vector<Text> texts;                       // id text_first + index
    // A campaign's new lines in its stages' opening, set-up and end events, which the
    // original runs from the scene's own event block (mini_stage.hpp): the line's number
    // goes at this offset of the block when the scene registers (tools/mods/stage.py).
    struct Patch {unsigned scene{};uint32_t at{};uint16_t text{};};
    std::vector<Patch> patches;
    // A step's condition in those events: its place in the block, 0 or 1 before each step.
    struct Check {std::string package;unsigned scene{};uint32_t at{};Test test;};
    std::vector<Check> checks;
    struct Objective {std::string campaign;unsigned scene{};uint16_t win{},lose{};};
    std::vector<Objective> objectives;
    // A battle line (story.ts battleLines): `who` says `text` in a battle of the scene at
    // these moments (80222B14's contexts, a bit each), against `against` (0 anyone).
    struct Quote {std::string package,campaign,id;unsigned scene{},who{},against{},moments{};uint16_t text{};bool once{};std::optional<Test> test;};
    std::vector<Quote> quotes;
    std::vector<std::string> problems;
};
inline constexpr uint32_t event_block=0x8019B400,event_block_size=0x1A00;
inline constexpr uint16_t text_placeholder=0xFFFE;   // story.py TEXT
inline constexpr uint16_t check_placeholder=0xFFFC;  // story.ts CHECK

// A test as the compiler writes it. Units may be a package's new ones by name.
inline Test parse_test(const json& j,const std::string& package,unsigned depth=0) {
    if(depth>16 || !j.is_object() || j.size()!=1)throw std::runtime_error("条件要是只有一个键的对象");
    const std::string key=j.begin().key();
    const json& value=j.begin().value();
    static const std::map<std::string,Test::Kind> kinds{{"all",Test::Kind::all},{"any",Test::Kind::any},{"not",Test::Kind::not_},
        {"var",Test::Kind::var},{"kills",Test::Kind::kills},{"turn",Test::Kind::turn},{"status",Test::Kind::status},{"hp",Test::Kind::hp},
        {"count",Test::Kind::count},{"in_area",Test::Kind::in_area},{"route",Test::Kind::route},{"choice",Test::Kind::choice},
        {"funds",Test::Kind::funds},{"downed",Test::Kind::downed},{"roster",Test::Kind::roster},{"flag",Test::Kind::flag},{"fired",Test::Kind::fired},
        {"level",Test::Kind::level},{"opponent",Test::Kind::opponent}};
    const auto found=kinds.find(key);
    if(found==kinds.end())throw std::runtime_error("不认识的条件 "+key);
    Test t;t.kind=found->second;
    static const std::map<Test::Kind,size_t> sizes{{Test::Kind::var,2},{Test::Kind::kills,3},{Test::Kind::turn,2},{Test::Kind::hp,2},
        {Test::Kind::count,3},{Test::Kind::in_area,6},{Test::Kind::funds,2},{Test::Kind::downed,4},{Test::Kind::level,2},{Test::Kind::opponent,2}};
    switch(t.kind) {
        case Test::Kind::all: case Test::Kind::any:
            if(!value.is_array() || value.empty())throw std::runtime_error(key+" 要是条件的数组");
            for(const auto& v:value)t.sub.push_back(parse_test(v,package,depth+1));
            break;
        case Test::Kind::not_: t.sub.push_back(parse_test(value,package,depth+1));break;
        case Test::Kind::status: {
            static const std::map<std::string,int> states{{"on_map",on_map},{"defeated",defeated},{"absent",absent}};
            const auto st=states.find(value.at(1).get<std::string>());
            if(st==states.end())throw std::runtime_error("status 只能是 on_map、defeated、absent");
            t.n={value.at(0).get<int>(),st->second};
            break;
        }
        case Test::Kind::route: t.n=value.get<std::vector<int>>();break;
        case Test::Kind::choice: t.n={value.get<int>()};break;
        case Test::Kind::flag: case Test::Kind::fired: t.name=value.get<std::string>();break;
        case Test::Kind::roster:
            t.pilots=value.value("pilots",std::vector<unsigned>{});
            for(const auto& u:value.value("units",json::array())) {
                const auto unit=mod_ids::unit(u,package);
                if(!unit)throw std::runtime_error("roster.units："+u.dump()+" 不是机体号，也不是已启用的新机体");
                t.units.push_back(*unit);
            }
            break;
        default:
            t.n=value.get<std::vector<int>>();
            if(t.n.size()!=sizes.at(t.kind))throw std::runtime_error(key+" 的数字个数不对");
    }
    return t;
}

// One compiled story file of a package, appended to the library: its text references
// become the library's text ids. Throws with what is wrong; nothing is added then.
inline void add_image(Library& library,const std::string& package,const json& image,const std::string& campaign="") {
    if(!image.is_object() || image.value("schema","")!=image_schema)throw std::runtime_error("schema 不是 "+std::string(image_schema));
    const auto& texts=image.at("texts");
    const auto& events=image.at("events");
    if(!texts.is_array() || !events.is_array())throw std::runtime_error("texts 与 events 必须是数组");
    const size_t base=library.texts.size();
    // 0xE800 up are the names of new characters, units and weapons (table_window.hpp).
    if(base+texts.size()>size_t(table_window::private_first-text_first))throw std::runtime_error("台词太多");
    std::vector<Text> new_texts;
    for(const auto& t:texts) {
        Text text;
        // A package's new character speaks by name (mod_ids.hpp): its number in the header.
        if(const auto& who=t.at("speaker");who.is_string()) {
            const auto found=mod_ids::pilot(who,package);
            if(!found)throw std::runtime_error("说话人 "+who.dump()+" 不是已启用的新人物");
            text.speaker=*found;
        } else text.speaker=who.get<unsigned>();
        if(text.speaker>360 && !table_window::new_pilot(text.speaker))throw std::runtime_error("说话人超出 0–360");
        for(const auto& [locale,line]:t.at("lines").items()) {
            const auto s=line.get<std::string>();
            if(s.empty() || s.size()>longest_text*4)throw std::runtime_error("台词为空或太长");
            text.lines[locale]=s;
        }
        if(!text.lines.count("ja"))throw std::runtime_error("每句台词都要有 ja");
        const auto kind=t.value("kind","line");
        if(kind=="choice")text.kind=TextKind::choice;
        else if(kind=="objective")text.kind=TextKind::objective;
        else if(kind=="battle")text.kind=TextKind::battle;
        else if(kind!="line")throw std::runtime_error("文字种类只能是 line、choice、objective、battle");
        new_texts.push_back(std::move(text));
    }
    std::vector<Event> new_events;
    std::set<std::string> ids;
    for(const auto& e:events) {
        Event event;event.package=package;event.campaign=campaign;
        event.id=e.at("id").get<std::string>();
        if(event.id.empty() || !ids.insert(event.id).second)throw std::runtime_error("事件 id 为空或重复："+event.id);
        event.scene=e.at("scene").get<unsigned>();
        if(event.scene>255)throw std::runtime_error(event.id+"：场景号超出 0–255");
        const auto trigger=trigger_of(e.at("trigger").get<std::string>());
        if(!trigger)throw std::runtime_error(event.id+"：不认识的触发 "+e.at("trigger").get<std::string>());
        event.trigger=*trigger;
        const auto params=e.value("params",json::array());
        if(params.size()>6)throw std::runtime_error(event.id+"：params 最多六个");
        int* slots[]={&event.a,&event.b,&event.c};
        for(size_t i=0;i<params.size();++i){event.params.push_back(params[i].get<int>());if(i<3)*slots[i]=event.params.back();}
        if(event.trigger==Trigger::reach && params.size()!=6)throw std::runtime_error(event.id+"：reach 要六个参数");
        if(event.trigger==Trigger::downed && params.size()!=4)throw std::runtime_error(event.id+"：downed 要四个参数");
        event.after=e.value("after",std::string());
        if(event.trigger==Trigger::turns_after && (params.size()!=2 || event.after.empty()))throw std::runtime_error(event.id+"：turns_after 要回合数、阶段和 after");
        try {
            if(e.contains("test"))event.test=parse_test(e["test"],package);
            for(const auto& c:e.value("checks",json::array()))event.checks.emplace_back(c.at(0).get<size_t>(),parse_test(c.at(1),package));
        } catch(const std::exception& error){throw std::runtime_error(event.id+"："+error.what());}
        const auto vars=e.value("vars",json::object());
        for(const auto& [var,value]:vars.items()) {
            const unsigned v=unsigned(std::stoul(var)),x=value.get<unsigned>();
            if(v>=200 || x>3)throw std::runtime_error(event.id+"：变量 0–199、值 0–3");
            event.vars[v]=x;
        }
        for(const auto& [name,into]:{std::pair{"kills",&event.kills},{"kills_below",&event.kills_below}}) {
            const auto counts=e.value(name,json::object());
            for(const auto& [pilot,count]:counts.items()) {
                const unsigned p=unsigned(std::stoul(pilot)),n=count.get<unsigned>();
                if(p>360 || !n || n>9999)throw std::runtime_error(event.id+"："+name+" 人物 0–360、击坠数 1–9999");
                (*into)[p]=n;
            }
        }
        // A unit is the ROM's number or a package's new one by name (mod_ids.hpp).
        const auto unit_of=[&](const json& n,const std::string& where) {
            const auto found=mod_ids::unit(n,package);
            if(!found)throw std::runtime_error(event.id+"："+where+" 不是机体号，也不是已启用的新机体 "+n.dump());
            return *found;
        };
        auto numbers=[&](const char* name,const char* kind,unsigned top) {
            std::vector<unsigned> out;
            const auto roster=e.value(name,json::object());
            for(const auto& n:roster.value(kind,json::array())) {
                if(std::string(kind)=="units"){out.push_back(unit_of(n,std::string(name)+".units"));continue;}
                if(n.get<unsigned>()>top)throw std::runtime_error(event.id+"："+name+"."+kind+" 超出范围");
                out.push_back(n.get<unsigned>());
            }
            return out;
        };
        event.has_pilots=numbers("roster","pilots",360);event.has_units=numbers("roster","units",362);
        event.lacks_pilots=numbers("not_roster","pilots",360);event.lacks_units=numbers("not_roster","units",362);
        for(const auto& [name,into]:{std::pair{"flags",&event.flags},{"not_flags",&event.not_flags},{"set_flags",&event.set_flags}})
            for(const auto& f:e.value(name,json::array())) {
                const auto flag=f.get<std::string>();
                if(flag.empty() || flag.size()>32 || flag.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)
                    throw std::runtime_error(event.id+"：标记名只用小写字母、数字、_、-，最多 32 个");
                into->push_back(flag);
            }
        event.fired=e.value("fired",std::vector<std::string>{});
        event.not_fired=e.value("not_fired",std::vector<std::string>{});
        event.repeat=e.value("repeat",false);
        if(event.repeat && event_type(event.trigger)>=12)throw std::runtime_error(event.id+"：开场／配置／结束之后的事件不能重复");
        for(const auto& w:e.at("words"))event.words.push_back(uint16_t(w.get<unsigned>()));
        if(event.words.empty() || event.words.back()!=0xFFFF)throw std::runtime_error(event.id+"：指令要以 FFFF 结束");
        if((header_words+event.words.size())*2>region_size)throw std::runtime_error(event.id+"：指令太长");
        for(const auto& ref:e.value("texts",json::array())) {
            const size_t at=ref.at(0).get<size_t>(),index=ref.at(1).get<size_t>();
            if(at>=event.words.size() || index>=new_texts.size())throw std::runtime_error(event.id+"：台词引用越界");
            event.words[at]=uint16_t(text_first+base+index);
        }
        for(const auto& ref:e.value("numbers",json::array())) {
            const size_t at=ref.at(0).get<size_t>();
            const auto kind=ref.at(2).get<std::string>();
            if(at>=event.words.size() || event.words[at]!=0xFFFD)throw std::runtime_error(event.id+"：新机体／新武器／新人物的引用越界");
            const auto found=kind=="weapon"?mod_ids::weapon(ref.at(1),package):kind=="pilot"?mod_ids::pilot(ref.at(1),package):mod_ids::unit(ref.at(1),package);
            if(!found)throw std::runtime_error(event.id+"：没有启用的新"+(kind=="weapon"?"武器 ":kind=="pilot"?"人物 ":"机体 ")+ref.at(1).dump());
            event.words[at]=uint16_t(*found);
        }
        for(const auto& [at,test]:event.checks)
            if(at<1 || at>=event.words.size() || event.words[at]!=check_placeholder || event.words[at-1]!=0x3E14)
                throw std::runtime_error(event.id+"：条件的位置不是 3E14 的操作数");
        for(const auto& ref:e.value("levels",json::array())) {
            const size_t at=ref.at(0).get<size_t>();const unsigned pilot=ref.at(1).get<unsigned>();
            if(at<2 || at>=event.words.size() || event.words[at-2]!=0x3D5A || pilot>360)throw std::runtime_error(event.id+"：等级引用不是 3D5A 的第二个操作数");
            event.levels.emplace_back(at,pilot);
        }
        for(const auto& b:e.value("builds",json::array()))event.builds.push_back(unit_of(b,"build"));
        for(const auto& c:e.value("crew",json::array())) {
            const unsigned unit=unit_of(c.at("unit"),"crew.unit");
            const auto pilots=c.at("pilots").get<std::vector<unsigned>>();
            if(pilots.empty() || pilots.size()>5)throw std::runtime_error(event.id+"：crew 要写机体号和 1–5 名人物");
            for(const auto pilot:pilots)if(pilot>360)throw std::runtime_error(event.id+"：crew 的人物号越界");
            event.crew.emplace_back(unit,pilots);
        }
        new_events.push_back(std::move(event));
    }
    for(const auto& e:new_events)for(const auto& names:{e.fired,e.not_fired})
        for(const auto& id:names)if(!ids.count(id))throw std::runtime_error(e.id+"：fired/not_fired 引用了不存在的事件 "+id);
    // The stage information entries: a line by locale (ja required), shown on the scene's page.
    std::vector<Info> new_infos;
    for(const auto& s:image.value("stage_info",json::array())) {
        Info info;info.package=package;
        info.scene=s.at("scene").get<unsigned>();
        if(info.scene>255)throw std::runtime_error("stage_info：场景号超出 0–255");
        for(const auto& [field,into]:{std::pair{"title",&info.title},{"text",&info.text}}) {
            const auto& by_locale=s.at(field);
            if(!by_locale.is_object() || !by_locale.contains("ja"))throw std::runtime_error(std::string("stage_info.")+field+"：要写各语言的文字，至少有 ja");
            for(const auto& [locale,line]:by_locale.items())
                if(!line.is_string() || line.get<std::string>().empty() || line.get<std::string>().size()>longest_text*4)
                    throw std::runtime_error(std::string("stage_info.")+field+"."+locale+"：为空或太长");
            *into=by_locale;
        }
        if(s.contains("done")) {
            info.checks=true;
            const auto& done=s["done"];
            for(const auto& p:done.value("pilots",json::array())) {
                if(!p.is_number_unsigned() || p.get<unsigned>()>360)throw std::runtime_error("stage_info.done.pilots：人物 0–360");
                info.pilots.push_back(p.get<unsigned>());
            }
            for(const auto& u:done.value("units",json::array())) {
                const auto found=mod_ids::unit(u,package);
                if(!found)throw std::runtime_error("stage_info.done.units："+u.dump()+" 不是机体号，也不是已启用的新机体");
                info.units.push_back(*found);
            }
        }
        new_infos.push_back(std::move(info));
    }
    std::vector<Library::Patch> new_patches;
    std::vector<Library::Check> new_checks;
    for(const auto& p:image.value("patches",json::array())) {
        if(p.contains("test")) {
            Library::Check check{campaign,p.at("scene").get<unsigned>(),p.at("at").get<uint32_t>(),{}};
            if(campaign.empty() || check.scene>255 || check.at%2 || check.at<2 || check.at+2>event_block_size)
                throw std::runtime_error("patches：条件只有战役的开场／配置／结束事件能用，或位置越界");
            check.test=parse_test(p.at("test"),campaign);
            new_checks.push_back(std::move(check));
            continue;
        }
        Library::Patch patch;
        patch.scene=p.at("scene").get<unsigned>();patch.at=p.at("at").get<uint32_t>();
        const size_t index=p.at("text").get<size_t>();
        if(campaign.empty() || patch.scene>255 || patch.at%2 || patch.at+2>event_block_size || index>=new_texts.size())
            throw std::runtime_error("patches：只有战役的开场／配置／结束事件能用，或引用越界");
        patch.text=uint16_t(text_first+base+index);
        new_patches.push_back(patch);
    }
    for(auto& t:new_texts)library.texts.push_back(std::move(t));
    std::vector<Library::Quote> new_quotes;
    for(const auto& q:image.value("quotes",json::array())) {
        Library::Quote quote{package,campaign,q.at("id").get<std::string>(),q.at("scene").get<unsigned>(),q.at("who").get<unsigned>(),q.value("against",0u),0,0,
            q.value("once",false),std::nullopt};
        const size_t text=q.at("text").get<size_t>();
        if(quote.scene>255 || quote.who>360 || quote.against>360 || text>=new_texts.size() || new_texts[text].kind!=TextKind::battle)
            throw std::runtime_error("quotes "+quote.id+"：场景、人物或台词引用越界");
        for(const auto& m:q.at("moments")){const unsigned n=m.get<unsigned>();if(n>8)throw std::runtime_error("quotes "+quote.id+"：时机 0–8");quote.moments|=1u<<n;}
        quote.text=uint16_t(text_first+base+text);
        if(q.contains("test"))quote.test=parse_test(q["test"],package);
        new_quotes.push_back(std::move(quote));
    }
    for(auto& q:new_quotes)library.quotes.push_back(std::move(q));
    std::vector<Library::Objective> new_objectives;
    for(const auto& o:image.value("objectives",json::array())) {
        const size_t win=o.at("win").get<size_t>(),lose=o.at("lose").get<size_t>();
        if(campaign.empty() || win>=new_texts.size() || lose>=new_texts.size())throw std::runtime_error("objectives：只有战役关卡能写，或文字引用越界");
        new_objectives.push_back({campaign,o.at("scene").get<unsigned>(),uint16_t(text_first+base+win),uint16_t(text_first+base+lose)});
    }
    for(auto& o:new_objectives)library.objectives.push_back(std::move(o));
    for(const auto& p:new_patches)library.patches.push_back(p);
    for(auto& c:new_checks)library.checks.push_back(std::move(c));
    for(auto& e:new_events)library.events.push_back(std::move(e));
    for(auto& i:new_infos)library.infos.push_back(std::move(i));
}
// Every story file of the active packages, bottom first (a package above fires first
// when two fit the same moment).
// With a custom campaign running, the story image beside its campaign image as well
// (campaign/story.json, tools/mods/stage.py): the events of its stages.
inline Library collect(const std::vector<mods::Package>& packages,const std::string& campaign="",const fs::path& campaign_story={}) {
    Library library;
    for(const auto& package:packages) {
        if(!package.active)continue;
        const size_t first=library.events.size();
        const auto folder=package.folder/"story";
        std::error_code ec;
        if(!fs::is_directory(folder,ec))continue;
        std::vector<fs::path> files;
        for(const auto& entry:fs::directory_iterator(folder,ec))if(entry.path().extension()==".json")files.push_back(entry.path());
        std::sort(files.begin(),files.end());
        for(const auto& file:files) {
            const auto where=package.id()+"/story/"+file.filename().string()+"：";
            std::ifstream in(file);
            const auto document=json::parse(in,nullptr,false);
            if(document.is_discarded()){library.problems.push_back(where+"JSON 无效");continue;}
            try {add_image(library,package.id(),document);}
            catch(const std::exception& error){library.problems.push_back(where+error.what());}
        }
        // Top package first; within a package, its own order.
        std::rotate(library.events.begin(),library.events.begin()+first,library.events.end());
    }
    std::error_code ec;
    if(!campaign.empty() && !campaign_story.empty() && fs::is_regular_file(campaign_story,ec)) {
        const auto where=campaign+"/campaign/story.json：";
        std::ifstream in(campaign_story);
        const auto document=json::parse(in,nullptr,false);
        if(document.is_discarded())library.problems.push_back(where+"JSON 无效");
        else if(document.value("campaign","")!=campaign)library.problems.push_back(where+"不是这个战役的");
        else try {add_image(library,campaign,document,campaign);}
        catch(const std::exception& error){library.problems.push_back(where+error.what());}
    }
    return library;
}

// A text record as table 0 holds one: an 8-byte ASCII header (speaker, mode, wait) and a
// page of blanks (glyph 0) about as long as the Japanese line, up to three rows of 14, so
// the original types and waits on it as on its own; the native reader shows the line
// (native_dialogue.cpp, story_record()). An empty page passes without being read.
// The ROM font's glyph of one character (native_dialogue.cpp fills it from its glyph table),
// for the records a choice and an objective need: the original draws their glyphs itself
// in the Japanese original image mode, and the text overlay counts their ink elsewhere.
inline std::function<std::optional<uint16_t>(const std::string&)>& glyph_of(){static std::function<std::optional<uint16_t>(const std::string&)> f;return f;}
inline void encode_japanese(std::vector<uint8_t>& bytes,const std::string& text) {
    const auto push=[&](uint16_t code){bytes.push_back(uint8_t(code>>8));bytes.push_back(uint8_t(code));};
    const uint16_t fallback=glyph_of()?glyph_of()("・").value_or(0x0001):0x0001;
    for(size_t i=0;i<text.size();) {
        size_t n=1;const auto c=uint8_t(text[i]);
        if(c>=0xF0)n=4;else if(c>=0xE0)n=3;else if(c>=0xC0)n=2;
        const auto one=text.substr(i,n);i+=n;
        push(glyph_of()?glyph_of()(one).value_or(fallback):fallback);
    }
}
inline std::vector<uint8_t> text_record(const Text& text) {
    if(text.kind==TextKind::choice || text.kind==TextKind::objective) {
        // As the original's own: a choice is its options a row each, each after a blank (the
        // cursor bar's room), header 02500004; an objective one row, header 00040000.
        const char* header=text.kind==TextKind::choice?"02500004":"00040000";
        std::vector<uint8_t> bytes(header,header+8);
        const auto& ja=text.lines.at("ja");
        size_t from=0;
        for(bool first=true;;first=false) {
            const auto at=ja.find("<BR>",from);
            if(!first){bytes.push_back(0xFF);bytes.push_back(0xFE);}
            if(text.kind==TextKind::choice){bytes.push_back(0);bytes.push_back(0);}
            encode_japanese(bytes,ja.substr(from,at==std::string::npos?std::string::npos:at-from));
            if(at==std::string::npos)break;
            from=at+4;
        }
        bytes.push_back(0xFF);bytes.push_back(0xFF);
        return bytes;
    }
    // A battle quote's header is the original's 20032 kind (record 5813 on).
    char header[9];std::snprintf(header,sizeof header,text.kind==TextKind::battle?"%03u20032":"%03u00004",text.speaker);
    std::vector<uint8_t> bytes(header,header+8);
    const auto& ja=text.lines.at("ja");
    size_t length=0;
    for(const auto c:ja)if((uint8_t(c)&0xC0)!=0x80)++length;   // code points
    const size_t blanks=std::clamp<size_t>(length,1,42);
    for(size_t i=0;i<blanks;++i) {
        if(i && i%14==0){bytes.push_back(0xFF);bytes.push_back(0xFE);}   // <BR>
        bytes.push_back(0);bytes.push_back(0);
    }
    bytes.push_back(0xFF);bytes.push_back(0xFF);
    return bytes;
}

// 8008C510 wrapper: table 0's descriptor (offset from the table, size) of a package line.
inline bool descriptor(uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size);
// 8007F704 wrapper: serves what descriptor() handed out.
inline bool read_text(uint32_t rom,uint8_t* ram,uint32_t destination,uint32_t size);

// --- What the original's triggers read ----------------------------------------------

// Story variable n (0..199): two bits each at 8015E818 (800A496C).
inline unsigned variable(const uint8_t* ram,unsigned n) {
    return (read(ram,0x8015E818+2*(n/8),2)>>(2*(n%8)))&3;
}
// Our side's pilot records (100 x 0x4C at 80172F40, the first of the three sides'): +0 in
// use, +2 the pilot, +0x14 kills (pilot_kills.hpp; an enemy's +0x14 counts dummies instead).
inline constexpr uint32_t pilot_records=0x80172F40, pilot_record_size=0x4C, pilot_record_count=100, pilot_kills=0x14;
inline std::optional<uint32_t> pilot_record(const uint8_t* ram,unsigned pilot) {
    for(uint32_t i=0;i<pilot_record_count;++i) {
        const uint32_t r=pilot_records+i*pilot_record_size;
        if(read(ram,r,1) && read(ram,r+2,2)==pilot)return r;
    }
    return std::nullopt;
}
inline std::optional<unsigned> kills(const uint8_t* ram,unsigned pilot) {
    const auto r=pilot_record(ram,pilot);
    return r?std::optional<unsigned>(read(ram,*r+pilot_kills,2)):std::nullopt;
}
// Our roster's unit instances (140 x 0x54 at 8016A210): +0 in use, +2 the unit.
inline constexpr uint32_t unit_instances=0x8016A210, unit_instance_size=0x54, unit_instance_count=140;
inline bool has_unit(const uint8_t* ram,unsigned unit) {
    for(uint32_t i=0;i<unit_instance_count;++i) {
        const uint32_t u=unit_instances+i*unit_instance_size;
        if(read(ram,u,1) && read(ram,u+2,2)==unit)return true;
    }
    return false;
}
// A unit's crew lives on its instance: +0x34 how many, then a pointer per pilot record from
// +0x38; each pilot's record says it has a unit (+0x37) and which (+0x38). The original sets
// them only when it makes an instance, from the crew table D_800CA418, which has no room for a
// new unit (docs/design/final-dancouga-mod.md §4.10.1), so an event's crew is written here once
// the event has run. The first pilot is on the unit already (its 3D5A put them there); the
// others take that pilot's link, leaving any unit they were on.
inline constexpr uint32_t unit_crew=0x34, unit_crew_pilots=0x38, pilot_has_unit=0x37, pilot_unit=0x38, crew_most=5;
inline bool set_crew(uint8_t* ram,unsigned unit,const std::vector<unsigned>& pilots) {
    std::optional<uint32_t> instance;
    for(uint32_t i=0;i<unit_instance_count && !instance;++i) {
        const uint32_t u=unit_instances+i*unit_instance_size;
        if(read(ram,u,1) && read(ram,u+2,2)==unit)instance=u;
    }
    if(!instance || pilots.empty() || pilots.size()>crew_most)return false;
    std::vector<uint32_t> records;
    for(const auto pilot:pilots) {
        const auto r=pilot_record(ram,pilot);
        if(!r)return false;
        records.push_back(*r);
    }
    const uint32_t has=read(ram,records[0]+pilot_has_unit,1),link=read(ram,records[0]+pilot_unit,4);
    for(const uint32_t r:records) {
        const uint32_t old=read(ram,r+pilot_unit,4);
        if(old!=*instance && old>=unit_instances && old<unit_instances+unit_instance_count*unit_instance_size &&
           (old-unit_instances)%unit_instance_size==0) {
            std::vector<uint32_t> keep;
            const unsigned n=std::min<unsigned>(read(ram,old+unit_crew,1),crew_most);
            for(unsigned k=0;k<n;++k)if(const uint32_t q=read(ram,old+unit_crew_pilots+k*4,4);q!=r)keep.push_back(q);
            for(unsigned k=0;k<crew_most;++k)guest::write32(ram,old+unit_crew_pilots+k*4,k<keep.size()?keep[k]:0);
            guest::write8(ram,old+unit_crew,uint8_t(keep.size()));
        }
        guest::write8(ram,r+pilot_has_unit,uint8_t(has));
        guest::write32(ram,r+pilot_unit,link);
    }
    for(unsigned k=0;k<crew_most;++k)guest::write32(ram,*instance+unit_crew_pilots+k*4,k<records.size()?records[k]:0);
    guest::write8(ram,*instance+unit_crew,uint8_t(records.size()));
    return true;
}
// A join "like" a pilot: 3D5A registers at the stage's base level (8010F5F3) plus its
// second operand (800AAD28), so the operand is that pilot's level (record +5) less the base.
inline constexpr uint32_t base_level=0x8010F5F3, pilot_level=0x05;
inline uint16_t level_offset(const uint8_t* ram,unsigned pilot) {
    const auto r=pilot_record(ram,pilot);
    if(!r)return 0;
    return uint16_t(int(read(ram,*r+pilot_level,1))-int(read(ram,base_level,1)));
}
// The game state a trigger looks at, read by the hooks (and filled by hand in tests).
struct View {
    unsigned phase{},turn{},side{},enemies{},third{};
    unsigned battle_a{},battle_b{};                   // the combatants' pilots, 0 when none
    std::function<unsigned(unsigned)> variable;       // 800A496C
    std::function<double(unsigned)> hp;               // 800A293C: percent, 0 defeated, 103 gone
    std::function<std::optional<unsigned>(unsigned)> kills;   // our pilot's kills; none when not ours
    std::function<bool(unsigned)> has_pilot,has_unit;          // on our roster
    std::vector<MapUnit> units;                       // on the map now
    // The group of a unit as the scene's deployment records (engine+0) place it, -1 none.
    std::function<int(const MapUnit&)> group_of;
    std::function<std::optional<unsigned>(unsigned)> level;   // a pilot's level (record +5), on the map or ours
    unsigned funds{};                                 // 8010F5F4
    int route=-1;                                     // 801602EE: 0 Brad, 1 Manami, 2 Ark, 3 Selain
    int choice=0;                                     // 801602E4: the last option picked, 1..3
};
// Defeats the host has seen this stage (observe(), after State): of a side, a group, a
// pilot (-1 any of each); whether a unit of a group stands on the map now.
inline unsigned downs(int side,int group,int pilot);
inline bool group_on_map(int group);
// The turn (0-based) an event of this package started in, if it has (began() keeps it).
inline std::optional<unsigned> started_turn(const std::string& key);
inline bool inside(const MapUnit& u,int pilot,int side,const int* area) {
    return (pilot<0 || int(u.pilot)==pilot) && (side<0 || int(u.side)==side) &&
           int(u.x)>=area[0] && int(u.y)>=area[1] && int(u.x)<=area[2] && int(u.y)<=area[3];
}
inline bool on_the_map(const View& v,unsigned pilot) {
    return std::any_of(v.units.begin(),v.units.end(),[&](const MapUnit& u){return u.pilot==pilot;});
}
inline unsigned side_count(const View& v,unsigned side) {
    return unsigned(std::count_if(v.units.begin(),v.units.end(),[&](const MapUnit& u){return u.side==side;}));
}
// Mirrors of the original handlers (8009E598, 8009E604, 8009E6E4, 8009EA88, 8009E9B4,
// 8009E834, 8009E8B4) without their slot bookkeeping; downed, group_gone and reach are the
// host's own (level-editor.md §5.3).
inline bool holds(const Event& e,const View& v) {
    switch(e.trigger) {
        case Trigger::turn: return unsigned(e.b)==v.side && v.turn>=unsigned(e.a);
        case Trigger::defeated:
            // by: the other side of the battle (+0x9A0/+0x9A4, as 3E16 reads it).
            return v.hp && int(v.hp(unsigned(e.a)))==0 && (e.b<=0 || unsigned(e.b)==v.battle_a || unsigned(e.b)==v.battle_b);
        case Trigger::downed: return e.params.size()==4 && downs(e.params[0],e.params[1],e.params[2])>=unsigned(e.params[3]);
        case Trigger::group_gone: return downs(-1,e.a,-1)>0 && !group_on_map(e.a);
        case Trigger::turns_after: {
            const auto from=started_turn(e.package+"/"+e.after);
            return from && unsigned(e.b)==v.side && v.turn>=*from+unsigned(e.a);
        }
        case Trigger::reach:
            return e.params.size()==6 && std::any_of(v.units.begin(),v.units.end(),[&](const MapUnit& u){return inside(u,e.params[0],e.params[1],&e.params[2]);});
        case Trigger::hp_below: {
            if(!v.hp)return false;
            const double hp=v.hp(unsigned(e.a));
            return hp>0 && hp<=double(e.b);
        }
        case Trigger::battle_after: case Trigger::battle_before: {
            if(!v.battle_a || !v.battle_b)return false;
            auto in=[&](int who){return who==0 || unsigned(who)==v.battle_a || unsigned(who)==v.battle_b;};
            return in(e.a) && in(e.b);
        }
        case Trigger::enemies_gone: return (unsigned(e.a)==v.side || e.a==4) && v.turn<=unsigned(e.b) && v.enemies==0;
        case Trigger::remaining: return (unsigned(e.c)==v.side || e.c==4) && (e.a==2?side_count(v,0):e.a==1?v.third:v.enemies)<=unsigned(e.b);
        default: return false;
    }
}
// The poll's scan of types [from, to) in its current phase (8009E180 picks them).
inline bool scanned(Trigger t,unsigned from,unsigned to) {
    const int type=event_type(t);
    return type>=int(from) && type<int(to) && type!=9;
}

// --- Runtime -------------------------------------------------------------------------

struct State {
    Library library;
    unsigned revision=~0u;
    mods::Version game{-1,-1,-1};
    fs::path output;                                    // run directory: the log
    std::function<fs::path()> keep;                     // where suspend sidecars go (empty: nowhere)
    int scene=-1;
    std::string campaign;                               // the custom campaign running ("" none)
    fs::path campaign_story,loaded_story;               // its story image; the one the library holds
    std::set<std::string> fired;
    std::optional<size_t> running;                      // event index while its words run
    std::vector<unsigned> builds_due;                   // combined units to build (take_builds)
    std::set<std::string> flags;                        // long-term: package/flag, kept with every save
    // The map as the last look saw it (observe()): each roster slot's unit and its group.
    struct Seen {bool used{};unsigned pilot{},unit{};int group=-1;};
    std::array<Seen,90> seen{};
    bool primed{};                                      // false: the next look only records
    struct Saved {std::set<std::string> fired,flags;};
    std::map<uint64_t,Saved> snapshots;                 // save record hash -> what went with it
    bool loaded{};
    // A copy for the debug interface's status, which reads it off the game thread.
    std::mutex status_mutex;
    json status=json::object();
    // The lines, also read by the dialogue reader off the game thread.
    std::mutex texts_mutex;
    std::shared_ptr<const std::vector<Text>> texts=std::make_shared<std::vector<Text>>();
    // The stage information entries, read by the page off the game thread.
    std::mutex infos_mutex;
    std::shared_ptr<const std::vector<Info>> infos=std::make_shared<std::vector<Info>>();
};
inline State& state(){static State value;return value;}
// A custom campaign runs: the main story's stage information entries are not its own.
inline bool& disabled(){static bool value{};return value;}
// Game thread, before a scene's registration: the campaign running and its story image.
inline void set_campaign(const std::string& id,const fs::path& story) {
    auto& s=state();
    s.campaign=id;s.campaign_story=id.empty()?fs::path():story;
    disabled()=!id.empty();
}
inline bool here(const Event& e){const auto& s=state();return int(e.scene)==s.scene && e.campaign==s.campaign;}

// Game thread, 8008C9C0 (a text into a body slot): the map menu's 作戦目的 asks for 5694 /
// 5720 when the stage's opening wrote 3D65 127,127; a campaign stage with its own objective
// gets its text there instead. Otherwise the id as it is.
inline uint16_t objective_text(const uint8_t* ram,uint16_t id) {
    if(id!=objective_win && id!=objective_lose)return id;
    const auto& s=state();
    if(s.campaign.empty())return id;
    const unsigned scene=read(ram,scene_byte,1);
    for(const auto& o:s.library.objectives)
        if(o.campaign==s.campaign && o.scene==scene)return id==objective_win?o.win:o.lose;
    return id;
}

// Game thread, after a change: the copy the debug interface reads.
inline void publish() {
    auto& s=state();
    json events=json::array();
    for(const auto& e:s.library.events)if(here(e))events.push_back(e.key());
    json status={{"scene",s.scene},{"events",s.library.events.size()},{"texts",s.library.texts.size()},
        {"problems",s.library.problems},{"this_scene",events},{"fired",s.fired},{"flags",s.flags},
        {"running",s.running?json(s.library.events[*s.running].key()):json()}};
    std::lock_guard lock(s.status_mutex);s.status=std::move(status);
}
// Any thread.
inline json snapshot() {
    auto& s=state();
    std::lock_guard lock(s.status_mutex);return s.status;
}
inline void log(const json& fields) {
    auto& s=state();
    std::fprintf(stderr,"SRW64_STORY_MOD %s\n",fields.dump().c_str());
    if(s.output.empty())return;
    json row=fields;row["schema"]="srw64.story-mod-event.v1";
    std::ofstream(s.output/"story-mod-events.jsonl",std::ios::app)<<row.dump()<<'\n';
}
inline void configure(const fs::path& output,std::function<fs::path()> keep) {
    auto& s=state();s.output=output;s.keep=std::move(keep);
}
// Game thread, at a scene's registration: again when the packages change.
inline void refresh() {
    auto& s=state();
    const auto now=mods::revision();
    const auto game=mods::game_version();
    if(s.loaded && now==s.revision && game==s.game && s.campaign_story==s.loaded_story)return;
    s.revision=now;s.game=game;s.loaded=true;s.loaded_story=s.campaign_story;
    s.library=collect(mods::installed(game),s.campaign,s.campaign_story);
    {std::lock_guard lock(s.texts_mutex);s.texts=std::make_shared<std::vector<Text>>(s.library.texts);}
    {std::lock_guard lock(s.infos_mutex);s.infos=std::make_shared<std::vector<Info>>(s.library.infos);}
    for(const auto& problem:s.library.problems)std::fprintf(stderr,"SRW64_STORY_MOD_PROBLEM %s\n",problem.c_str());
    log({{"action","loaded"},{"events",s.library.events.size()},{"texts",s.library.texts.size()},{"problems",s.library.problems}});
}
// Any thread: the packages' stage information entries (empty while a custom campaign runs,
// which has its own pages).
inline std::shared_ptr<const std::vector<Info>> infos() {
    auto& s=state();
    if(disabled())return std::make_shared<std::vector<Info>>();
    std::lock_guard lock(s.infos_mutex);return s.infos;
}
// The page's status key of entry n of infos().
inline std::string info_ref(const Info& info,size_t n){return "mod:"+info.package+":"+std::to_string(n);}
// Any thread.
inline std::optional<Text> text(uint16_t id) {
    auto& s=state();
    std::shared_ptr<const std::vector<Text>> texts;
    {std::lock_guard lock(s.texts_mutex);texts=s.texts;}
    if(id<text_first || size_t(id-text_first)>=texts->size())return std::nullopt;
    return (*texts)[id-text_first];
}
// The line in a language by localization::fallbacks (every line has ja).
inline std::optional<std::string> line(uint16_t id,const std::string& locale) {
    const auto t=text(id);
    if(!t)return std::nullopt;
    for(const auto& key:localization::fallbacks(locale))
        if(const auto found=t->lines.find(key);found!=t->lines.end())return found->second;
    return t->lines.at("ja");
}

inline bool descriptor(uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size) {
    const auto t=table?std::nullopt:text(id);
    if(!t)return false;
    size=uint32_t(text_record(*t).size());
    offset=text_virtual+uint32_t(id-text_first)*text_slot-text_table;
    return true;
}
inline bool read_text(uint32_t rom,uint8_t* ram,uint32_t destination,uint32_t size) {
    if(rom<text_virtual || rom>=text_virtual+0x10000u*text_slot)return false;
    const uint32_t index=(rom-text_virtual)/text_slot,within=(rom-text_virtual)%text_slot;
    const auto t=text(uint16_t(text_first+index));
    if(!t)throw std::runtime_error("story MOD: virtual text read of an unknown record");
    const auto bytes=text_record(*t);
    // Callers may read part of it (8008CE54 reads the header for the speaker).
    if(within+size>bytes.size() || !guest::valid(destination|0x80000000u,size))throw std::runtime_error("story MOD: invalid virtual text read");
    for(uint32_t i=0;i<size;++i)guest::write8(ram,destination+i,bytes[within+i]);
    return true;
}

// --- Defeats and conditions ------------------------------------------------------------

// A unit that leaves the map while no event runs was defeated (an event's 3D46 takes them
// off while it runs). Each defeat is kept in the fired set as "#down/side/group/pilot/n", so
// it goes with the suspend records as the fired events do and clears with a new stage.
inline constexpr const char* down_prefix="#down/";
inline unsigned downs(int side,int group,int pilot) {
    unsigned n=0;
    for(const auto& key:state().fired) {
        if(key.rfind(down_prefix,0))continue;
        int s=-1,g=-1,p=-1,k=0;
        if(std::sscanf(key.c_str()+6,"%d/%d/%d/%d",&s,&g,&p,&k)!=4)continue;
        if((side<0 || s==side) && (group<0 || g==group) && (pilot<0 || p==pilot))++n;
    }
    return n;
}
inline std::optional<unsigned> started_turn(const std::string& key) {
    const auto prefix="#at/"+key+"/";
    for(const auto& k:state().fired)
        if(!k.rfind(prefix,0))return unsigned(std::strtoul(k.c_str()+prefix.size(),nullptr,10));
    return std::nullopt;
}
inline bool group_on_map(int group) {
    return std::any_of(state().seen.begin(),state().seen.end(),[&](const State::Seen& u){return u.used && u.group==group;});
}
// Game thread, while no event runs (in_event false: before the poll's scans) and after each
// step of one (true): who is on the map now; what left it since.
inline void observe(const View& v,bool in_event) {
    auto& s=state();
    std::array<State::Seen,90> now{};
    for(const auto& u:v.units)if(u.side<3 && u.slot<30)now[u.side*30+u.slot]={true,u.pilot,u.unit,-1};
    for(size_t i=0;i<now.size();++i) {
        const auto& was=s.seen[i];
        auto& is=now[i];
        const bool same=was.used && is.used && was.pilot==is.pilot && was.unit==is.unit;
        if(same){is.group=was.group;continue;}
        if(was.used && s.primed && !in_event) {
            const auto key=std::string(down_prefix)+std::to_string(i/30)+"/"+std::to_string(was.group)+"/"+std::to_string(was.pilot)+"/"+
                std::to_string(downs(-1,-1,-1));
            s.fired.insert(key);
            log({{"action","down"},{"side",i/30},{"group",was.group},{"pilot",was.pilot}});
        }
        if(is.used && v.group_of)
            for(const auto& m:v.units)if(m.side==i/30 && m.slot==i%30)is.group=v.group_of(m);
    }
    s.seen=now;s.primed=true;
}
inline bool evaluate(const Test& t,const std::string& package,const View& v) {
    using K=Test::Kind;
    const auto& n=t.n;
    switch(t.kind) {
        case K::all: return std::all_of(t.sub.begin(),t.sub.end(),[&](const Test& x){return evaluate(x,package,v);});
        case K::any: return std::any_of(t.sub.begin(),t.sub.end(),[&](const Test& x){return evaluate(x,package,v);});
        case K::not_: return !evaluate(t.sub.at(0),package,v);
        case K::var: return v.variable && int(v.variable(unsigned(n[0])))==n[1];
        case K::kills: {
            const auto has=v.kills?v.kills(unsigned(n[0])):std::nullopt;
            return has && int(*has)>=n[1] && int(*has)<=n[2];
        }
        case K::turn: return int(v.turn)>=n[0] && int(v.turn)<=n[1];
        case K::status: {
            const bool here=on_the_map(v,unsigned(n[0]));
            return n[1]==on_map?here:n[1]==absent?!here:downs(-1,-1,n[0])>0 && !here;
        }
        case K::hp: {
            if(!v.hp || !on_the_map(v,unsigned(n[0])))return false;
            const double hp=v.hp(unsigned(n[0]));
            return hp>0 && hp<double(n[1]);
        }
        case K::count: {const int c=int(side_count(v,unsigned(n[0])));return c>=n[1] && c<=n[2];}
        case K::in_area:
            return std::any_of(v.units.begin(),v.units.end(),[&](const MapUnit& u){return inside(u,n[0],n[1],&n[2]);});
        case K::route: return std::find(n.begin(),n.end(),v.route)!=n.end();
        case K::choice: return v.choice==n[0];
        case K::funds: return int64_t(v.funds)>=n[0] && int64_t(v.funds)<=n[1];
        case K::downed: return downs(n[0],n[1],n[2])>=unsigned(n[3]);
        case K::roster:
            return std::all_of(t.pilots.begin(),t.pilots.end(),[&](unsigned p){return v.has_pilot && v.has_pilot(p);}) &&
                   std::all_of(t.units.begin(),t.units.end(),[&](unsigned u){return v.has_unit && v.has_unit(u);});
        case K::level: {
            const auto a=v.level?v.level(unsigned(n[0])):std::nullopt,b=v.level?v.level(unsigned(n[1])):std::nullopt;
            return a && b && *a>=*b;
        }
        case K::opponent:
            return (v.battle_a==unsigned(n[0]) && v.battle_b==unsigned(n[1])) || (v.battle_b==unsigned(n[0]) && v.battle_a==unsigned(n[1]));
        case K::flag: return state().flags.count(package+"/"+t.name)>0;
        case K::fired: return state().fired.count(package+"/"+t.name)>0;
    }
    return false;
}
// A step's conditions, written where its 3E14 reads them: the running event's in the
// region, a campaign stage's three events' in the scene's block. Called when an event
// starts and after each step, so a condition is decided at most a step before it is read.
inline void decide(uint8_t* ram,const View& v) {
    auto& s=state();
    if(s.running)for(const auto& [at,test]:s.library.events[*s.running].checks)
        write16(ram,region+uint32_t((header_words+at)*2),evaluate(test,s.library.events[*s.running].package,v)?1:0);
    if(!s.campaign.empty())for(const auto& c:s.library.checks)
        if(int(c.scene)==s.scene && c.package==s.campaign)write16(ram,event_block+c.at,evaluate(c.test,c.package,v)?1:0);
}

// Battle overlay, 80222B14 (a generic quote drawn into a slot): the voice part of the
// speaking side (combatant record +2) and the context. A battle line of this scene whose
// speaker has that voice (D_800CA9C4[pilot]) and whose opponent, if it names one, is the
// other side of the battle (engine+0x9A0 / +0x9A4) takes the slot's place.
inline constexpr uint32_t voice_table=0x800CA9C4;
inline std::optional<uint16_t> battle_quote(const uint8_t* ram,unsigned voice,unsigned context,const View& v) {
    auto& s=state();
    if(s.scene<0 || context>8)return std::nullopt;
    for(const auto& q:s.library.quotes) {
        if(int(q.scene)!=s.scene || q.campaign!=s.campaign || !(q.moments&(1u<<context)))continue;
        if(int16_t(read(ram,voice_table+q.who*2,2))!=int(voice))continue;
        if(q.against) {
            const unsigned other=v.battle_a==q.who?v.battle_b:v.battle_b==q.who?v.battle_a:0;
            if(other!=q.against)continue;
        }
        const auto key="#quote/"+q.package+"/"+q.id;
        if(q.once && s.fired.count(key))continue;
        if(q.test && !evaluate(*q.test,q.package,v))continue;
        if(q.once)s.fired.insert(key);
        log({{"action","quote"},{"quote",q.package+"/"+q.id},{"text",q.text},{"context",context}});
        return q.text;
    }
    return std::nullopt;
}

inline bool eligible(const Event& e,const std::set<std::string>& fired,const View& v) {
    if(e.test && !evaluate(*e.test,e.package,v))return false;
    if(!e.repeat && fired.count(e.key()))return false;
    const auto& flags=state().flags;
    for(const auto& f:e.flags)if(!flags.count(e.package+"/"+f))return false;
    for(const auto& f:e.not_flags)if(flags.count(e.package+"/"+f))return false;
    for(const auto p:e.has_pilots)if(!v.has_pilot || !v.has_pilot(p))return false;
    for(const auto u:e.has_units)if(!v.has_unit || !v.has_unit(u))return false;
    for(const auto p:e.lacks_pilots)if(!v.has_pilot || v.has_pilot(p))return false;
    for(const auto u:e.lacks_units)if(!v.has_unit || v.has_unit(u))return false;
    for(const auto& id:e.fired)if(!fired.count(e.package+"/"+id))return false;
    for(const auto& id:e.not_fired)if(fired.count(e.package+"/"+id))return false;
    for(const auto& [var,value]:e.vars)if(!v.variable || v.variable(var)!=value)return false;
    for(const auto& [pilot,count]:e.kills) {
        const auto has=v.kills?v.kills(pilot):std::nullopt;
        if(!has || *has<count)return false;
    }
    for(const auto& [pilot,count]:e.kills_below) {
        const auto has=v.kills?v.kills(pilot):std::nullopt;
        if(!has || *has>=count)return false;
    }
    return true;
}

// A scene's events registered (8009DE7C, after the original): mode 0 is a new stage,
// anything else the same stage again after a suspend or a screen of its own.
inline void registered(uint8_t* ram,unsigned mode,const View* v=nullptr) {
    auto& s=state();
    refresh();
    s.scene=int(read(ram,scene_byte,1));
    s.running.reset();
    s.seen={};s.primed=false;
    if(mode==0)s.fired.clear();
    // A campaign stage's event block was just written (mini_stage.hpp register_hook): its
    // new lines get their numbers before any of its events runs.
    unsigned patched=0;
    if(!s.campaign.empty())for(const auto& p:s.library.patches)
        if(int(p.scene)==s.scene && read(ram,event_block+p.at,2)==text_placeholder){write16(ram,event_block+p.at,p.text);++patched;}
    if(v)decide(ram,*v);
    log({{"action","registered"},{"scene",s.scene},{"campaign",s.campaign},{"mode",mode},{"patched",patched},{"fired",s.fired},{"flags",s.flags}});
    publish();
}

// Puts an event where it runs, as the ROM lays one out: a five-word header (its type, four
// parameters), which 8009EE98 steps over, then its words.
inline void place(uint8_t* ram,const Event& e) {
    for(uint32_t i=0;i<region_size;i+=2)write16(ram,region+i,0);
    write16(ram,region,uint16_t(event_type(e.trigger)));
    for(size_t i=0;i<e.words.size();++i)write16(ram,region+uint32_t((header_words+i)*2),e.words[i]);
    for(const auto& [at,pilot]:e.levels)write16(ram,region+uint32_t((header_words+at)*2),level_offset(ram,pilot));
}
// The event engine counts finished events off +0x98E (8009F020); one of ours was never
// registered, so it is counted in before it runs.
// The combined units the events just started want built (Event::builds); the caller, which
// has a context to call the game with, builds them (game_hooks.cpp).
inline std::vector<unsigned> take_builds() {
    auto& s=state();
    std::vector<unsigned> out;out.swap(s.builds_due);
    return out;
}
inline void began(uint8_t* ram,size_t index,const View* v) {
    auto& s=state();
    const auto& e=s.library.events[index];
    write16(ram,engine+f_count,uint16_t(read(ram,engine+f_count,2)+1));
    s.fired.insert(e.key());s.running=index;
    // When it started, for turns_after (kept with the suspend records as the rest of fired).
    if(!started_turn(e.key()))s.fired.insert("#at/"+e.key()+"/"+std::to_string(read(ram,engine+f_turn,2)));
    if(v)decide(ram,*v);
    for(const auto unit:e.builds)s.builds_due.push_back(unit);
    log({{"action","started"},{"event",e.key()},{"scene",s.scene},{"trigger",event_type(e.trigger)}});
    publish();
}
// The first event of this scene that fits, in the triggers `wanted` keeps.
template<class Wanted>
inline std::optional<size_t> choose(const View& v,Wanted&& wanted) {
    auto& s=state();
    for(size_t i=0;i<s.library.events.size();++i) {
        const auto& e=s.library.events[i];
        if(!here(e) || !wanted(e))continue;
        if(eligible(e,s.fired,v))return i;
    }
    return std::nullopt;
}

// 8009E3B0 wrapper, after the original scanned types [from, to) and started nothing:
// starts ours the way the original's handler would (`start` calls 8009EE98).
inline bool scan(uint8_t* ram,unsigned from,unsigned to,const View& v,const std::function<void(uint32_t)>& start) {
    auto& s=state();
    if(s.scene<0 || read(ram,engine+f_status,2)>=0x2000)return false;
    const auto index=choose(v,[&](const Event& e){return scanned(e.trigger,from,to) && holds(e,v);});
    if(!index)return false;
    const auto& e=s.library.events[*index];
    place(ram,e);
    write16(ram,engine+f_status,start_status(e.trigger));
    start(owner);
    began(ram,*index,&v);
    return true;
}

// 800A4634 wrapper, after the original found no persuasion: one of ours by the
// persuader in +0x9A8, next to its target. Returns the target unit's pilot, as the
// original does (the command's animation faces it), and marks +0x992.
// `unit` is 800A2BF4: the map unit of a pilot, 0 when not on the map.
inline int persuasion(uint8_t* ram,const View& v,const std::function<uint32_t(unsigned)>& unit) {
    auto& s=state();
    if(s.scene<0 || read(ram,engine+f_status,2)>=0x2000)return -1;
    const unsigned by=read(ram,engine+f_persuader,2);
    const auto index=choose(v,[&](const Event& e) {
        if(e.trigger!=Trigger::persuade || unsigned(e.a)!=by)return false;
        const uint32_t self=unit(by),other=unit(unsigned(e.b));
        if(!self || !other)return false;
        const int dx=int(read(ram,self+4,1))-int(read(ram,other+4,1)),dy=int(read(ram,self+5,1))-int(read(ram,other+5,1));
        return std::abs(dx)+std::abs(dy)==1;
    });
    if(!index)return -1;
    const uint32_t other=unit(unsigned(s.library.events[*index].b));
    write16(ram,engine+f_persuade,uint16_t(persuade_mark+*index));
    return int(int16_t(read(ram,read(ram,read(ram,other+0xC,4)+0x38,4)+2,2)));
}
// 8009E3B0 wrapper, before the original: in the persuasion phase our mark in +0x992 is
// started here, as 8009EC3C starts a slot's; the original then sees an event running
// and returns. Its handler would read the mark as a slot number.
inline bool persuaded(uint8_t* ram,const std::function<void(uint32_t)>& start,const View* v=nullptr) {
    auto& s=state();
    const unsigned mark=read(ram,engine+f_persuade,2);
    if(mark<persuade_mark || mark>=persuade_mark+s.library.events.size())return false;
    if(read(ram,engine+f_phase,2)!=6 || read(ram,engine+f_status,2)>=0x2000)return false;
    const size_t index=mark-persuade_mark;
    place(ram,s.library.events[index]);
    write16(ram,engine+f_status,0x2000);
    start(owner);
    write16(ram,engine+f_persuade,0xFFFF);
    began(ram,index,v);
    return true;
}
// 8009EDB8 wrapper, before the original: in the opening, set-up or end phase (+4 = C1..C3),
// once the original's event of it has run (its pointer cleared, the engine idle), ours
// for that phase go into the pointer one by one; the original starts each as its own.
inline bool special(uint8_t* ram,const View& v) {
    auto& s=state();
    if(s.scene<0)return false;
    const unsigned mode=read(ram,engine+f_mode,2);
    if(mode<0xC1 || mode>0xC3)return false;
    const uint32_t pointer=engine+8+(mode-0xC1)*4;
    if(read(ram,pointer,4) || read(ram,engine+f_status,2)!=0x80)return false;
    const Trigger wanted=mode==0xC1?Trigger::after_opening:mode==0xC2?Trigger::after_setup:Trigger::after_end;
    const auto index=choose(v,[&](const Event& e){return e.trigger==wanted;});
    if(!index)return false;
    place(ram,s.library.events[*index]);
    write32(ram,pointer,region);
    began(ram,*index,&v);
    return true;
}
// 8009EFDC wrapper, after each step, with the status it returns (8009E180 stores it in
// +0x97C afterwards): an event of ours that ran to its end.
inline void stepped(uint16_t status,uint8_t* ram=nullptr,const View* v=nullptr) {
    auto& s=state();
    if(v)observe(*v,true);
    if(ram && v)decide(ram,*v);
    if(!s.running || status!=0x80)return;
    const auto& e=s.library.events[*s.running];
    for(const auto& [unit,pilots]:e.crew) {
        const bool done=ram && set_crew(ram,unit,pilots);
        log({{"action","crew"},{"event",e.key()},{"unit",unit},{"pilots",pilots},{"done",done}});
    }
    for(const auto& f:e.set_flags)s.flags.insert(e.package+"/"+f);
    log({{"action","finished"},{"event",e.key()},{"flags",s.flags}});
    s.running.reset();
    publish();
}

// --- Save records ---------------------------------------------------------------------

// What has fired in the stage and the long-term flags go with each record the game
// serializes: a suspend record (80093278, 0x3AE0 at 800FBEF0, to its buffer or to SRAM)
// or a slot record (800924D8, 0x1F00 at 801C2600, in the intermission), and are found
// again by the record's bytes when one is restored (800936A0, 800927A4). A slot record's
// first word (bit 15 and checksum) is written after it, so its hash starts past it.
enum class Record {suspend,slot};
inline constexpr uint32_t slot_buffer=0x801C2600, slot_size=0x1F00;
inline uint64_t record_hash(const uint8_t* ram,Record kind=Record::suspend) {
    const uint32_t from=kind==Record::slot?slot_buffer+2:suspend_buffer;
    const uint32_t size=kind==Record::slot?slot_size-2:suspend_size;
    uint64_t h=kind==Record::slot?0x9E3779B97F4A7C15ull:1469598103934665603ull;
    for(uint32_t i=0;i<size;++i){h^=read(ram,from+i,1);h*=1099511628211ull;}
    return h;
}
inline fs::path sidecar(uint64_t hash) {
    char name[32];std::snprintf(name,sizeof name,"%016llx.json",static_cast<unsigned long long>(hash));
    const auto& keep=state().keep;
    const auto folder=keep?keep():fs::path();
    return folder.empty()?fs::path():folder/"story-state"/name;
}
inline void serialized(const uint8_t* ram,bool to_sram,Record kind=Record::suspend) {
    auto& s=state();
    State::Saved saved{kind==Record::slot?std::set<std::string>{}:s.fired,s.flags};
    const auto hash=record_hash(ram,kind);
    s.snapshots[hash]=saved;
    if(s.snapshots.size()>256)s.snapshots.erase(s.snapshots.begin());
    // The intermission serializes only to write a slot, so its sidecar always goes to disk.
    const bool lasting=to_sram || kind==Record::slot;
    if(const auto file=sidecar(hash);lasting && !file.empty() && !(saved.fired.empty() && saved.flags.empty())) {
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        std::ofstream(file)<<json({{"schema",state_schema},{"scene",s.scene},{"fired",saved.fired},{"flags",saved.flags}}).dump()<<'\n';
    }
}
inline void restored(const uint8_t* ram,bool from_sram,Record kind=Record::suspend) {
    auto& s=state();
    const auto hash=record_hash(ram,kind);
    std::string from="none";
    s.fired.clear();s.flags.clear();
    if(const auto found=s.snapshots.find(hash);found!=s.snapshots.end()){s.fired=found->second.fired;s.flags=found->second.flags;from="memory";}
    else if(const auto file=sidecar(hash);!file.empty() && fs::exists(file)) {
        std::ifstream in(file);
        const auto document=json::parse(in,nullptr,false);
        if(!document.is_discarded() && document.value("schema","")==state_schema) {
            for(const auto& key:document.value("fired",json::array()))s.fired.insert(key.get<std::string>());
            for(const auto& key:document.value("flags",json::array()))s.flags.insert(key.get<std::string>());
        }
        from="file";
    }
    if(kind==Record::slot)s.fired.clear();
    s.primed=false;
    log({{"action","restored"},{"record",kind==Record::slot?"slot":"suspend"},{"from_sram",from_sram},{"found",from},
         {"fired",s.fired},{"flags",s.flags}});
    publish();
}
// A new game (800A4790 sets every story variable to 3): nothing has fired, no flags.
inline void new_game() {
    auto& s=state();
    s.fired.clear();s.flags.clear();s.running.reset();
    log({{"action","new_game"}});
    publish();
}
}
