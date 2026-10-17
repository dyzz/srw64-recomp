#pragma once
// Hidden elements carried into a later playthrough (docs/design/new-game-plus.md): a port
// of the grant logic in tools/content/stage_status.py. The record (saves/legacy/legacy.json)
// keeps every hidden element any playthrough finished, with the pilot and machine numbers
// it had for it; with the difficulty rule inherit-hidden on, the intermission right after
// (or before) the stage of an element's original join runs that join's commands
// (status.json "grants", from guide/data/inherit-grants.json) when the player lacks it.
// This header reads only a State, so it is tested without a game (tests/native_stage_info.cpp);
// legacy.cpp keeps the file and runs the commands on the game thread.
#include "stage_info.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace srw64::legacy {
using json=nlohmann::json;
using stage_info::Evaluator;
using stage_info::Rules;
using stage_info::State;

inline constexpr int nobody=999;   // 3D5A's "no pilot" / "no machine"

struct Ids {
    std::set<int> pilots,units;
    bool operator==(const Ids&) const=default;
};
using Record=std::map<std::string,Ids>;   // acquire rule id → what the player had for it

inline json to_json(const Record& record) {
    json out=json::object();
    for(const auto& [id,ids]:record)out[id]={{"pilots",ids.pilots},{"units",ids.units}};
    return out;
}
inline Record from_json(const json& data) {
    Record out;
    if(!data.is_object())return out;
    for(const auto& [id,ids]:data.items()) {
        if(!ids.is_object())continue;
        auto& have=out[id];
        for(const auto& p:ids.value("pilots",json::array()))if(p.is_number_integer())have.pilots.insert(p.get<int>());
        for(const auto& u:ids.value("units",json::array()))if(u.is_number_integer())have.units.insert(u.get<int>());
    }
    return out;
}

// What says the player already has an entry's element: its own "owned", else the first
// registered pilot, else that command's machine.
inline json owned(const json& entry) {
    if(entry.contains("owned"))return entry["owned"];
    for(const auto& c:entry.at("commands"))if(c.at(0)=="3D5A")
        return c.at(1).get<int>()!=nobody?json{{"pilot",c.at(1)}}:json{{"unit",c.at(3)}};
    return json{{"pilot",-1}};
}
// A 3D5A swapping from an old machine (mode below 2000, not 500/999) runs only when the
// player has that machine; every other command always runs.
inline bool runnable(const json& command,const std::set<int>& units) {
    if(command.at(0)!="3D5A")return true;
    const int mode=command.at(4).get<int>();
    return mode==500 || mode==nobody || mode>=2000 || units.contains(mode);
}

namespace detail {
inline void ids_of(const json& p,Ids& out) {
    if(p.is_array()){for(const auto& q:p)ids_of(q,out);return;}
    if(!p.is_object())return;
    for(const auto& [key,value]:p.items()) {
        if(key=="pilot" || key=="unit") {
            auto& set=key=="pilot"?out.pilots:out.units;
            if(value.is_array())for(const auto& v:value)set.insert(v.get<int>());
            else set.insert(value.get<int>());
        } else ids_of(value,out);
    }
}
inline const json* entries_of(const Rules& rules,const std::string& id) {
    for(const auto& g:rules.grants)if(g.at("id")==id)return &g.at("entries");
    return nullptr;
}
}

// RECORD with every acquire rule this state finishes added: the rule's own pilot and
// machine numbers and the ones its grants register, those the player has.
inline Record record(Record record,const Rules& rules,const Evaluator& evaluator,const State& state) {
    for(const auto& rule:rules.rules) {
        if(rule.at("kind")!="acquire" || evaluator.evaluate(rule).at("state")!="done")continue;
        Ids wanted;
        detail::ids_of(rule.at("done"),wanted);
        if(const auto* entries=detail::entries_of(rules,rule.at("id")))
            for(const auto& e:*entries)for(const auto& c:e.at("commands"))if(c.at(0)=="3D5A") {
                if(c.at(1).get<int>()!=nobody)wanted.pilots.insert(c.at(1).get<int>());
                if(c.at(3).get<int>()!=nobody)wanted.units.insert(c.at(3).get<int>());
            }
        auto& have=record[rule.at("id").get<std::string>()];
        for(const int p:wanted.pilots)if(state.pilots.contains(p))have.pilots.insert(p);
        for(const int u:wanted.units)if(state.units.contains(u))have.units.insert(u);
    }
    return record;
}

// The (rule id, entry) pairs to run in this intermission: state.cleared is the stage just
// played (8010F5F1), state.scene the next one (8010F5F0). At most one entry per element.
inline std::vector<std::pair<std::string,const json*>> due(const Record& record,const Rules& rules,
                                                          const Evaluator& evaluator,const State& state) {
    std::vector<std::pair<std::string,const json*>> out;
    const auto in=[](const json& entry,const char* key,int scene) {
        if(!entry.contains(key))return false;
        for(const auto& s:entry[key])if(s.get<int>()==scene)return true;
        return false;
    };
    for(const auto& grant:rules.grants) {
        const auto id=grant.at("id").get<std::string>();
        const auto have=record.find(id);
        if(have==record.end())continue;
        const json* rule=nullptr;
        for(const auto& r:rules.rules)if(r.at("id")==id)rule=&r;
        if(!rule || (rule->contains("applies") && !evaluator.test((*rule)["applies"])))continue;
        // The variant is judged on what was recorded, not on this game.
        State recorded;recorded.protagonist=state.protagonist;
        recorded.pilots=have->second.pilots;recorded.units=have->second.units;
        const Evaluator was(rules,recorded);
        for(const auto& entry:grant.at("entries")) {
            if(!in(entry,"after",state.cleared) && !in(entry,"before",state.scene))continue;
            if(entry.contains("applies") && !evaluator.test(entry["applies"]))continue;
            if(entry.contains("variant") && !was.test(entry["variant"]))continue;
            if(evaluator.test(owned(entry)))continue;
            out.emplace_back(id,&entry);
            break;
        }
    }
    return out;
}

// A finished-before element the player has now although its rule says otherwise: the
// page shows it as done (inherited). Gato's rule reads var 37, which a grant leaves.
inline bool inherited(const Record& record,const Rules& rules,const json& rule,const Evaluator& evaluator) {
    const auto id=rule.at("id").get<std::string>();
    if(!record.contains(id))return false;
    const auto* entries=detail::entries_of(rules,id);
    if(!entries)return false;
    for(const auto& e:*entries)if(evaluator.test(owned(e)))return true;
    return false;
}

// The number of elements the grants cover (the page's "N/33").
inline size_t grantable(const Rules& rules){return rules.grants.size();}

// legacy.cpp. The record as last read or written (any thread).
Record current();
// Game thread: add what this state finishes to the record, writing the file when it grew.
void observe(const uint8_t* ram,const Rules& rules,const State& state,const Evaluator& evaluator);
void configure(const std::filesystem::path& run_directory);
}
