#pragma once
#include "guest_memory.hpp"
#include "mod_library.hpp"
#include "json/json.hpp"
#include <algorithm>
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

enum class Trigger {turn,defeated,hp_below,battle_after,battle_before,enemies_gone,remaining,persuade,after_opening,after_setup,after_end};
inline std::optional<Trigger> trigger_of(const std::string& name) {
    static const std::map<std::string,Trigger> names{{"turn",Trigger::turn},{"defeated",Trigger::defeated},
        {"hp_below",Trigger::hp_below},{"battle_after",Trigger::battle_after},{"battle_before",Trigger::battle_before},
        {"enemies_gone",Trigger::enemies_gone},{"remaining",Trigger::remaining},{"persuade",Trigger::persuade},
        {"after_opening",Trigger::after_opening},{"after_setup",Trigger::after_setup},{"after_end",Trigger::after_end}};
    const auto found=names.find(name);
    return found==names.end()?std::nullopt:std::optional(found->second);
}
// The original's event type a trigger stands for, which the poll scans in its phases (8009E180).
inline int event_type(Trigger t) {
    switch(t) {
        case Trigger::turn: return 0;
        case Trigger::defeated: return 2;
        case Trigger::hp_below: return 3;
        case Trigger::battle_after: return 4;
        case Trigger::battle_before: return 5;
        case Trigger::enemies_gone: return 6;
        case Trigger::remaining: return 7;
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
        case Trigger::defeated: case Trigger::hp_below: case Trigger::battle_after: return 0x2002;
        case Trigger::battle_before: return 0x2001;
        default: return 0x2000;
    }
}

struct Event {
    std::string package,id;          // key(): package/id
    unsigned scene{};
    Trigger trigger{};
    // turn: turn (0-based, at least), side; defeated, hp_below: who, percent; battle_*: a, b
    // (0 any); enemies_gone: side (4 any), last turn; remaining: third (1) or enemy, at most,
    // side (4 any); persuade: by, target.
    int a{},b{},c{};
    std::map<unsigned,unsigned> vars;              // all must hold (story variables 0..199)
    std::map<unsigned,unsigned> kills;             // pilot -> at least this many kills (our side)
    std::map<unsigned,unsigned> kills_below;       // pilot -> fewer kills than this (our side)
    std::vector<std::string> fired,not_fired;      // other events of the package (ids)
    bool repeat{};
    std::vector<uint16_t> words;                   // instructions and FFFF, text ids final
    std::vector<std::pair<size_t,unsigned>> levels;   // a join's level operand -> as this pilot's
    std::string key() const {return package+"/"+id;}
};
struct Text {
    unsigned speaker{};
    std::map<std::string,std::string> lines;       // locale -> text
};
struct Library {
    std::vector<Event> events;
    std::vector<Text> texts;                       // id text_first + index
    std::vector<std::string> problems;
};

// One compiled story file of a package, appended to the library: its text references
// become the library's text ids. Throws with what is wrong; nothing is added then.
inline void add_image(Library& library,const std::string& package,const json& image) {
    if(!image.is_object() || image.value("schema","")!=image_schema)throw std::runtime_error("schema 不是 "+std::string(image_schema));
    const auto& texts=image.at("texts");
    const auto& events=image.at("events");
    if(!texts.is_array() || !events.is_array())throw std::runtime_error("texts 与 events 必须是数组");
    const size_t base=library.texts.size();
    if(base+texts.size()>size_t(0x10000-text_first))throw std::runtime_error("台词太多");
    std::vector<Text> new_texts;
    for(const auto& t:texts) {
        Text text;text.speaker=t.at("speaker").get<unsigned>();
        if(text.speaker>360)throw std::runtime_error("说话人超出 0–360");
        for(const auto& [locale,line]:t.at("lines").items()) {
            const auto s=line.get<std::string>();
            if(s.empty() || s.size()>longest_text*4)throw std::runtime_error("台词为空或太长");
            text.lines[locale]=s;
        }
        if(!text.lines.count("ja"))throw std::runtime_error("每句台词都要有 ja");
        new_texts.push_back(std::move(text));
    }
    std::vector<Event> new_events;
    std::set<std::string> ids;
    for(const auto& e:events) {
        Event event;event.package=package;
        event.id=e.at("id").get<std::string>();
        if(event.id.empty() || !ids.insert(event.id).second)throw std::runtime_error("事件 id 为空或重复："+event.id);
        event.scene=e.at("scene").get<unsigned>();
        if(event.scene>255)throw std::runtime_error(event.id+"：场景号超出 0–255");
        const auto trigger=trigger_of(e.at("trigger").get<std::string>());
        if(!trigger)throw std::runtime_error(event.id+"：不认识的触发 "+e.at("trigger").get<std::string>());
        event.trigger=*trigger;
        const auto params=e.value("params",json::array());
        if(params.size()>3)throw std::runtime_error(event.id+"：params 最多三个");
        int* slots[]={&event.a,&event.b,&event.c};
        for(size_t i=0;i<params.size();++i)*slots[i]=params[i].get<int>();
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
        for(const auto& ref:e.value("levels",json::array())) {
            const size_t at=ref.at(0).get<size_t>();const unsigned pilot=ref.at(1).get<unsigned>();
            if(at<2 || at>=event.words.size() || event.words[at-2]!=0x3D5A || pilot>360)throw std::runtime_error(event.id+"：等级引用不是 3D5A 的第二个操作数");
            event.levels.emplace_back(at,pilot);
        }
        new_events.push_back(std::move(event));
    }
    for(const auto& e:new_events)for(const auto& names:{e.fired,e.not_fired})
        for(const auto& id:names)if(!ids.count(id))throw std::runtime_error(e.id+"：fired/not_fired 引用了不存在的事件 "+id);
    for(auto& t:new_texts)library.texts.push_back(std::move(t));
    for(auto& e:new_events)library.events.push_back(std::move(e));
}
// Every story file of the active packages, bottom first (a package above fires first
// when two fit the same moment).
inline Library collect(const std::vector<mods::Package>& packages) {
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
    return library;
}

// A text record as table 0 holds one: an 8-byte ASCII header (speaker, mode, wait) and a
// page of blanks (glyph 0) about as long as the Japanese line, up to three rows of 14, so
// the original types and waits on it as on its own; the native reader shows the line
// (native_dialogue.cpp, story_record()). An empty page passes without being read.
inline std::vector<uint8_t> text_record(const Text& text) {
    char header[9];std::snprintf(header,sizeof header,"%03u00004",text.speaker);
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
};
// Mirrors of the original handlers (8009E598, 8009E604, 8009E6E4, 8009EA88, 8009E9B4,
// 8009E834, 8009E8B4) without their slot bookkeeping.
inline bool holds(const Event& e,const View& v) {
    switch(e.trigger) {
        case Trigger::turn: return unsigned(e.b)==v.side && v.turn>=unsigned(e.a);
        case Trigger::defeated: return v.hp && int(v.hp(unsigned(e.a)))==0;
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
        case Trigger::remaining: return (unsigned(e.c)==v.side || e.c==4) && (e.a==1?v.third:v.enemies)<=unsigned(e.b);
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
    std::set<std::string> fired;
    std::optional<size_t> running;                      // event index while its words run
    std::map<uint64_t,std::set<std::string>> snapshots; // suspend record hash -> fired
    bool loaded{};
    // The lines, also read by the dialogue reader off the game thread.
    std::mutex texts_mutex;
    std::shared_ptr<const std::vector<Text>> texts=std::make_shared<std::vector<Text>>();
};
inline State& state(){static State value;return value;}
inline bool& disabled(){static bool value{};return value;}   // a custom campaign runs

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
    if(s.loaded && now==s.revision && game==s.game)return;
    s.revision=now;s.game=game;s.loaded=true;
    s.library=collect(mods::installed(game));
    {std::lock_guard lock(s.texts_mutex);s.texts=std::make_shared<std::vector<Text>>(s.library.texts);}
    for(const auto& problem:s.library.problems)std::fprintf(stderr,"SRW64_STORY_MOD_PROBLEM %s\n",problem.c_str());
    log({{"action","loaded"},{"events",s.library.events.size()},{"texts",s.library.texts.size()},{"problems",s.library.problems}});
}
// Any thread.
inline std::optional<Text> text(uint16_t id) {
    auto& s=state();
    std::shared_ptr<const std::vector<Text>> texts;
    {std::lock_guard lock(s.texts_mutex);texts=s.texts;}
    if(id<text_first || size_t(id-text_first)>=texts->size())return std::nullopt;
    return (*texts)[id-text_first];
}
// The line in a language: that one (Traditional Chinese else Simplified), else Japanese.
inline std::optional<std::string> line(uint16_t id,const std::string& locale) {
    const auto t=text(id);
    if(!t)return std::nullopt;
    if(const auto found=t->lines.find(locale);found!=t->lines.end())return found->second;
    if(const auto found=t->lines.find("zh-Hans");locale=="zh-Hant" && found!=t->lines.end())return found->second;
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

inline bool eligible(const Event& e,const std::set<std::string>& fired,const View& v) {
    if(!e.repeat && fired.count(e.key()))return false;
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
inline void registered(const uint8_t* ram,unsigned mode) {
    auto& s=state();
    if(disabled())return;
    refresh();
    s.scene=int(read(ram,scene_byte,1));
    s.running.reset();
    if(mode==0)s.fired.clear();
    log({{"action","registered"},{"scene",s.scene},{"mode",mode},{"fired",s.fired}});
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
inline void began(uint8_t* ram,size_t index) {
    auto& s=state();
    const auto& e=s.library.events[index];
    write16(ram,engine+f_count,uint16_t(read(ram,engine+f_count,2)+1));
    s.fired.insert(e.key());s.running=index;
    log({{"action","started"},{"event",e.key()},{"scene",s.scene},{"trigger",event_type(e.trigger)}});
}
// The first event of this scene that fits, in the triggers `wanted` keeps.
template<class Wanted>
inline std::optional<size_t> choose(const View& v,Wanted&& wanted) {
    auto& s=state();
    for(size_t i=0;i<s.library.events.size();++i) {
        const auto& e=s.library.events[i];
        if(int(e.scene)!=s.scene || !wanted(e))continue;
        if(eligible(e,s.fired,v))return i;
    }
    return std::nullopt;
}

// 8009E3B0 wrapper, after the original scanned types [from, to) and started nothing:
// starts ours the way the original's handler would (`start` calls 8009EE98).
inline bool scan(uint8_t* ram,unsigned from,unsigned to,const View& v,const std::function<void(uint32_t)>& start) {
    auto& s=state();
    if(disabled() || s.scene<0 || read(ram,engine+f_status,2)>=0x2000)return false;
    const auto index=choose(v,[&](const Event& e){return scanned(e.trigger,from,to) && holds(e,v);});
    if(!index)return false;
    const auto& e=s.library.events[*index];
    place(ram,e);
    write16(ram,engine+f_status,start_status(e.trigger));
    start(owner);
    began(ram,*index);
    return true;
}

// 800A4634 wrapper, after the original found no persuasion: one of ours by the
// persuader in +0x9A8, next to its target. Returns the target unit's pilot, as the
// original does (the command's animation faces it), and marks +0x992.
// `unit` is 800A2BF4: the map unit of a pilot, 0 when not on the map.
inline int persuasion(uint8_t* ram,const View& v,const std::function<uint32_t(unsigned)>& unit) {
    auto& s=state();
    if(disabled() || s.scene<0 || read(ram,engine+f_status,2)>=0x2000)return -1;
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
inline bool persuaded(uint8_t* ram,const std::function<void(uint32_t)>& start) {
    auto& s=state();
    const unsigned mark=read(ram,engine+f_persuade,2);
    if(mark<persuade_mark || mark>=persuade_mark+s.library.events.size())return false;
    if(read(ram,engine+f_phase,2)!=6 || read(ram,engine+f_status,2)>=0x2000)return false;
    const size_t index=mark-persuade_mark;
    place(ram,s.library.events[index]);
    write16(ram,engine+f_status,0x2000);
    start(owner);
    write16(ram,engine+f_persuade,0xFFFF);
    began(ram,index);
    return true;
}
// 8009EDB8 wrapper, before the original: in the opening, set-up or end phase (+4 = C1..C3),
// once the original's event of it has run (its pointer cleared, the engine idle), ours
// for that phase go into the pointer one by one; the original starts each as its own.
inline bool special(uint8_t* ram,const View& v) {
    auto& s=state();
    if(disabled() || s.scene<0)return false;
    const unsigned mode=read(ram,engine+f_mode,2);
    if(mode<0xC1 || mode>0xC3)return false;
    const uint32_t pointer=engine+8+(mode-0xC1)*4;
    if(read(ram,pointer,4) || read(ram,engine+f_status,2)!=0x80)return false;
    const Trigger wanted=mode==0xC1?Trigger::after_opening:mode==0xC2?Trigger::after_setup:Trigger::after_end;
    const auto index=choose(v,[&](const Event& e){return e.trigger==wanted;});
    if(!index)return false;
    place(ram,s.library.events[*index]);
    write32(ram,pointer,region);
    began(ram,*index);
    return true;
}
// 8009EFDC wrapper, after each step, with the status it returns (8009E180 stores it in
// +0x97C afterwards): an event of ours that ran to its end.
inline void stepped(uint16_t status) {
    auto& s=state();
    if(!s.running || status!=0x80)return;
    log({{"action","finished"},{"event",s.library.events[*s.running].key()}});
    s.running.reset();
}

// --- Suspend records -----------------------------------------------------------------

// What has fired goes with each suspend record the game writes (80093278, to its buffer or
// to SRAM), found again by the record's bytes when one is read back (800936A0).
inline uint64_t record_hash(const uint8_t* ram) {
    uint64_t h=1469598103934665603ull;
    for(uint32_t i=0;i<suspend_size;++i){h^=read(ram,suspend_buffer+i,1);h*=1099511628211ull;}
    return h;
}
inline fs::path sidecar(uint64_t hash) {
    char name[32];std::snprintf(name,sizeof name,"%016llx.json",static_cast<unsigned long long>(hash));
    const auto& keep=state().keep;
    const auto folder=keep?keep():fs::path();
    return folder.empty()?fs::path():folder/"story-state"/name;
}
inline void serialized(const uint8_t* ram,bool to_sram) {
    auto& s=state();
    if(disabled() || s.scene<0)return;
    const auto hash=record_hash(ram);
    s.snapshots[hash]=s.fired;
    if(s.snapshots.size()>256)s.snapshots.erase(s.snapshots.begin());
    if(const auto file=sidecar(hash);to_sram && !file.empty() && !s.fired.empty()) {
        std::error_code ec;
        fs::create_directories(file.parent_path(),ec);
        std::ofstream(file)<<json({{"schema",state_schema},{"scene",s.scene},{"fired",s.fired}}).dump()<<'\n';
    }
}
inline void restored(const uint8_t* ram,bool from_sram) {
    auto& s=state();
    if(disabled())return;
    const auto hash=record_hash(ram);
    std::string from="none";
    if(const auto found=s.snapshots.find(hash);found!=s.snapshots.end()){s.fired=found->second;from="memory";}
    else if(const auto file=sidecar(hash);!file.empty() && fs::exists(file)) {
        std::ifstream in(file);
        const auto document=json::parse(in,nullptr,false);
        s.fired.clear();
        if(!document.is_discarded() && document.value("schema","")==state_schema)
            for(const auto& key:document.value("fired",json::array()))s.fired.insert(key.get<std::string>());
        from="file";
    } else s.fired.clear();
    log({{"action","restored"},{"from_sram",from_sram},{"found",from},{"fired",s.fired}});
}
}
