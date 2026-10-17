#include "stage_info.hpp"
#include "campaign.hpp"
#include "legacy.hpp"
#include "guest_memory.hpp"
#include <filesystem>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();

namespace srw64::stage_info {
namespace {
using guest::read;

// Where the story's stage metadata lives: content/stages, shipped beside the program
// (launch.cpp sets SRW64_STAGE_META); a development run points at the source tree.
// UTF-8 like the other folders the launcher hands over (the Windows build's manifest
// makes it the process code page).
std::filesystem::path folder() {
    const char* dir=std::getenv("SRW64_STAGE_META");
    return dir && *dir?std::filesystem::path(dir):std::filesystem::path{};
}
json read_json(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return json();
    auto data=json::parse(in,nullptr,false);
    return data.is_discarded()?json():data;
}
std::optional<Rules> parse_rules(const json& data) {
    if(!data.is_object() || data.value("schema","")!="srw64.stage-status.v1")return std::nullopt;
    try{return Rules::parse(data);}catch(const std::exception&){return std::nullopt;}
}
// The status rules in play: the story's (status.json) or the running campaign's, kept
// per campaign. Game thread only.
const Rules* rules_in_play() {
    static const auto story=parse_rules(read_json(folder()/"status.json"));
    if(const auto id=campaign::id();!id.empty()) {
        static std::string loaded_for;
        static std::optional<Rules> theirs;
        if(id!=loaded_for){loaded_for=id;theirs=parse_rules(campaign::status());}
        return theirs?&*theirs:nullptr;
    }
    return story?&*story:nullptr;
}

// Guest addresses (docs/design/stage-info.md §4).
constexpr uint32_t mode=0x8015DA02,scene=0x8010F5F0,cleared=0x8010F5F1,engine=0x8015F950,route_marker=engine+0x99E,campaign_vars=0x8015E818;
constexpr uint32_t pilots=0x80172F40,pilot_size=0x4C,pilot_slots=100,pilot_number=0x02,pilot_kills=0x14;
constexpr uint32_t units=0x8016A210,unit_size=0x54,unit_slots=140,unit_number=0x02;

// The tactical map and its battles, the world map before a stage, the intermission.
bool in_stage(unsigned m) {
    switch(m) {
    case 2: case 3: case 0xB: case 12: case 13: case 0x11: case 0x16: case 0x1A: case 0x1C: case 0x22: return true;
    default: return in_intermission(m);
    }
}

State read_state_from(const uint8_t* ram) {
    State s;
    s.scene=int(read(ram,scene,1));
    s.cleared=int(read(ram,cleared,1));
    const unsigned marker=read(ram,route_marker,2);
    if(marker>=0x3DD1 && marker<=0x3DD4)s.protagonist=protagonists[marker-0x3DD1];
    for(unsigned i=0;i<s.vars.size();++i)s.vars[i]=uint8_t((read(ram,campaign_vars+2*(i/8),2)>>(2*(i%8)))&3);
    for(uint32_t slot=0;slot<pilot_slots;++slot) {
        const uint32_t p=pilots+slot*pilot_size;
        const unsigned flags=read(ram,p,1);
        if(!flags || (flags&0x80))continue;   // empty, or one the menus leave out
        const int id=int(int16_t(read(ram,p+pilot_number,2)));
        s.pilots.insert(id);
        s.kills[id]=std::max(s.kills[id],int(read(ram,p+pilot_kills,2)));
    }
    for(uint32_t slot=0;slot<unit_slots;++slot) {
        const uint32_t u=units+slot*unit_size;
        if(read(ram,u,1))s.units.insert(int(int16_t(read(ram,u+unit_number,2))));
    }
    return s;
}

std::mutex lock;
json published={{"available",false}};
std::atomic_bool wanted{false};
std::atomic<uint64_t> opening{0};
uint64_t last_vi=~uint64_t{};
}

// The intermission: reached from a stage it is mode 4; from the title's Load it stays in
// the load mode, 0x12 + slot (docs/gameplay/original-controls.md §3).
bool in_intermission(unsigned m){return m==4 || (m>=0x12 && m<=0x15);}
const Rules* rules(){return rules_in_play();}
State read_state(const uint8_t* ram){return read_state_from(ram);}

json snapshot(){std::lock_guard guard(lock);return published;}
uint64_t want(bool on) {
    // Each opening reads afresh: the next frame samples, whatever the last one was.
    if(on && !wanted.exchange(true)){++opening;last_vi=~uint64_t{};}
    if(!on)wanted=false;
    return opening;
}

json meta(int scene) {
    if(scene<0)return nullptr;
    // A campaign's stages carry their own; a scene it does not borrow has none.
    if(!campaign::id().empty())return campaign::stage_meta(uint32_t(scene));
    static std::mutex cache_lock;
    static std::map<int,json> cache;
    std::lock_guard guard(cache_lock);
    auto found=cache.find(scene);
    if(found==cache.end()) {
        char name[32];std::snprintf(name,sizeof name,"scene-%03d.json",scene);
        auto data=read_json(folder()/name);
        if(!data.is_object() || data.value("schema","")!="srw64.stage-meta.v1")data=nullptr;
        found=cache.emplace(scene,std::move(data)).first;
    }
    return found->second;
}

void sample(const uint8_t* ram) {
    if(!wanted || !ram)return;
    // A few times a second is plenty for a page that is read, not played.
    const uint64_t vi=srw64_current_vi();
    if(last_vi!=~uint64_t{} && vi<last_vi+10)return;
    last_vi=vi;
    json next={{"available",false}};
    const auto* r=rules_in_play();
    if(r && in_stage(read(ram,mode,1))) {
        const auto state=read_state_from(ram);
        // Before the name page (a mini stage entered from the title) there is no
        // protagonist: the stage's text without statuses or filtering.
        json status=json::object(),ever=json::array();
        size_t ever_granted=0;
        if(!state.protagonist.empty()) {
            const Evaluator evaluator(*r,state);
            // What this game finishes joins the cross-playthrough record (legacy.hpp).
            legacy::observe(ram,*r,state,evaluator);
            const auto record=legacy::current();
            for(const auto& rule:r->rules) {
                auto result=evaluator.evaluate(rule);
                // A grant leaves the variables, so a rule that reads them (Gato) would say
                // missed: the player has it, from an earlier playthrough.
                if(result.at("state")!="done" && rule.at("kind")=="acquire" && legacy::inherited(record,*r,rule,evaluator))
                    result={{"state","done"},{"inherited",true}};
                status[rule.at("id").get<std::string>()]=std::move(result);
            }
            for(const auto& g:r->grants)if(record.contains(g.at("id").get<std::string>())){ever.push_back(g.at("id"));++ever_granted;}
        }
        next={{"available",true},{"scene",state.scene},{"protagonist",state.protagonist},{"route",route(state)},
              {"map",!in_intermission(read(ram,mode,1))},{"status",std::move(status)},
              {"legacy",{{"ever",std::move(ever)},{"count",ever_granted},{"of",legacy::grantable(*r)}}}};
    }
    next["opening"]=opening.load();
    std::lock_guard guard(lock);
    published=std::move(next);
}
}
