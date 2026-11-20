#include "legacy.hpp"
#include "table_window.hpp"
#include "campaign.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "mod_combine.hpp"
#include "native_dialogue.hpp"
#include "notices.hpp"
#include "rule_fixes.hpp"
#include "save_store.hpp"
#include "story_mod.hpp"
#include "localization/catalog.hpp"
#include "funcs.h"
#include <fstream>
#include <mutex>
#include <optional>

uint64_t srw64_current_vi();

namespace srw64::legacy {
namespace {
namespace fs=std::filesystem;
using guest::read;

constexpr uint32_t transition=0x8015E9C5;   // the intermission's fade: -1 once a screen is idle
constexpr uint16_t text_unit_names=527,text_pilot_names=4382;
constexpr uint64_t check_every_vis=30;

std::mutex mutex;
fs::path run_directory;
std::optional<Record> loaded;
std::ofstream log;
uint64_t last_check=0;

// The player's library keeps the record beside the saves, whichever card is in use; a
// run without one (debug sessions) keeps its own, so testing never touches a player's.
fs::path file() {
    if(save_store::enabled())return save_store::main_library()/"legacy"/"legacy.json";
    return run_directory/"legacy.json";
}
Record& record_locked() {
    if(!loaded) {
        std::ifstream in(file(),std::ios::binary);
        json data=in?json::parse(in,nullptr,false):json();
        loaded=from_json(data.is_object()?data.value("elements",json::object()):json::object());
    }
    return *loaded;
}
void write_locked(const Record& record) {
    const auto path=file();
    std::error_code error;
    fs::create_directories(path.parent_path(),error);
    const auto temporary=path.string()+".tmp";
    {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        if(!out)return;
        out<<json{{"schema","srw64.legacy.v1"},{"elements",to_json(record)}}.dump(1)<<'\n';
    }
    fs::rename(temporary,path,error);
}
void event(const char* kind,json extra) {
    if(!log.is_open())return;
    extra["schema"]="srw64.legacy-event.v1";extra["kind"]=kind;extra["vi"]=srw64_current_vi();
    log<<extra.dump()<<'\n';log.flush();
}

// One original script command, as its handler would run it (800A0B3C, 800A15D0, 800A16BC,
// 800A12CC, 800ACF44). Each call gets a fresh stack frame below the caller's.
void run(uint8_t* ram,recomp_context* ctx,const json& command) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);
    const auto word=[&](size_t i){return int32_t(command.at(i).get<int>());};
    const auto name=command.at(0).get<std::string>();
    if(name=="3D5A") {
        // The handler maps the protagonists' machine numbers (800A3A1C), then registers.
        c.r4=word(3);resident_func_800A3A1C(ram,&c);
        const int32_t unit=int16_t(c.r2);
        c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);
        c.r4=word(1);c.r5=unit;c.r6=word(2);c.r7=word(4);
        resident_func_800AAD28(ram,&c);
    } else if(name=="3D6C") {
        c.r4=word(1);c.r5=word(2);resident_func_800ACA1C(ram,&c);
    } else if(name=="3D70") {
        c.r4=word(1);c.r5=word(2);resident_func_800ACFAC(ram,&c);
    } else if(name=="3D64" && word(1)==500) {
        resident_func_800AB96C(ram,&c);   // アイシャ into マナミ's mothership
    } else if(name=="3D6F") {
        c.r4=word(1);resident_func_800ACF44(ram,&c);   // a story weapon unlocked (packages only)
    }
}
// The names a grant registers, for the banner: pilots first, then machines no pilot came with.
std::string names(const uint8_t* ram,const json& entry) {
    std::string out;
    const auto add=[&](const std::string& n){if(n.empty())return;if(!out.empty())out+="、";out+=n;};
    for(const auto& c:entry.at("commands"))if(c.at(0)=="3D5A" && c.at(1).get<int>()!=nobody)
        add(dialogue::ui_text(ram,table_window::pilot_name(c.at(1).get<int>())));
    for(const auto& c:entry.at("commands"))if(c.at(0)=="3D5A" && c.at(1).get<int>()==nobody && c.at(3).get<int>()!=nobody)
        add(dialogue::ui_text(ram,uint16_t(table_window::unit_name(c.at(3).get<int>()))));
    return out;
}

// The entries with an id of these story packages' (story_mod::infos(), enabled ones only).
std::vector<Package> packages_of(const std::vector<story_mod::Info>& infos) {
    std::vector<Package> out;
    for(size_t n=0;n<infos.size();++n) {
        const auto& info=infos[n];
        if(info.id.empty() || !info.checks)continue;
        Package p{package_key(info.package,info.id),story_mod::info_ref(info,n),{},{},{},{},n};
        for(const auto q:info.pilots)p.pilots.push_back(int(q));
        for(const auto u:info.units)p.units.push_back(int(u));
        if(info.inherit){p.after=info.inherit->after;p.before=info.inherit->before;}
        out.push_back(std::move(p));
    }
    return out;
}

// Before every step of the インターミッション menu, once the screen is idle.
void intermission(uint8_t* ram,recomp_context* ctx) {
    if(campaign::active() || int8_t(read(ram,transition,1))!=-1)return;
    const uint64_t vi=srw64_current_vi();
    if(last_check && vi<last_check+check_every_vis)return;
    last_check=vi;
    const auto* rules=stage_info::rules();
    if(!rules || rules->grants.empty())return;
    const auto state=stage_info::read_state(ram);
    if(state.protagonist.empty())return;
    const Evaluator evaluator(*rules,state);
    observe(ram,*rules,state,evaluator);
    if(!srw64::rules::enabled(srw64::rules::inherit_hidden))return;
    for(const auto& [id,entry]:due(current(),*rules,evaluator,state)) {
        json done=json::array(),skipped=json::array();
        for(const auto& command:entry->at("commands")) {
            // Read afresh: an earlier command of this grant may have made the machine.
            if(!runnable(command,stage_info::read_state(ram).units)){skipped.push_back(command);continue;}
            run(ram,ctx,command);done.push_back(command);
        }
        event("grant",{{"id",id},{"cleared",state.cleared},{"next",state.scene},{"commands",done},{"skipped",skipped}});
        if(done.empty())continue;
        auto text=localization::catalog().ui("legacy_joined");
        if(const auto at=text.find("{name}");at!=std::string::npos)text.replace(at,6,names(ram,*entry));
        notices::post("legacy",text);
    }
    // The story packages' own (§8): built, joined, unlocked and crewed as their events would.
    // Entries and grants from one snapshot of the enabled packages, so an index cannot name
    // another package's entry; one whose package is gone is not in it.
    const auto infos=story_mod::infos();
    const auto all=packages_of(*infos);
    for(const auto* p:due(current(),all,state)) {
        const auto& info=(*infos)[p->index];
        if(!info.inherit || package_key(info.package,info.id)!=p->key)continue;
        const auto& grant=*info.inherit;
        for(const unsigned unit:grant.builds)mod_combine::build(ram,ctx,unit,resident_func_800A6E68,resident_func_800A5254);
        json done=json::array(),skipped=json::array();
        for(size_t k=0;k<grant.commands.size();++k) {
            auto command=grant.commands[k];
            for(const auto& [at,pilot]:grant.like)if(at==k)command[2]=int16_t(story_mod::level_offset(ram,pilot));
            if(!runnable(command,stage_info::read_state(ram).units)){skipped.push_back(command);continue;}
            run(ram,ctx,command);done.push_back(command);
        }
        for(const auto& [unit,pilots]:grant.crew)story_mod::set_crew(ram,unit,pilots);
        event("grant",{{"id",p->key},{"cleared",state.cleared},{"next",state.scene},{"commands",done},{"skipped",skipped}});
        if(done.empty() && grant.builds.empty())continue;
        auto text=localization::catalog().ui("legacy_joined");
        if(const auto at=text.find("{name}");at!=std::string::npos)text.replace(at,6,names(ram,{{"commands",done}}));
        notices::post("legacy",text);
    }
}
}

Record current() {
    std::lock_guard lock(mutex);
    return record_locked();
}
std::vector<Package> packages(){return packages_of(*story_mod::infos());}
void observe(const uint8_t*,const Rules& rules,const State& state,const Evaluator& evaluator) {
    if(campaign::active() || state.protagonist.empty() || rules.grants.empty())return;
    std::lock_guard lock(mutex);
    auto& now=record_locked();
    auto next=record(record(now,rules,evaluator,state),packages(),state);
    if(next==now)return;
    for(const auto& [id,ids]:next)if(!now.contains(id))event("finished",{{"id",id},{"scene",state.scene}});
    now=std::move(next);
    write_locked(now);
}
void configure(const fs::path& directory) {
    run_directory=directory;
    log.open(directory/"legacy-events.jsonl");
    srw64_game_hooks.legacy_intermission=intermission;
}
}
