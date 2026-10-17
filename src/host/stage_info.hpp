#pragma once
// The stage info page (docs/design/stage-info.md): the current stage's hidden elements
// with how far each has got, and its enemies' parts and retreats. Each stage's text is
// its metadata (content/stages/scene-NNN.json, or a custom campaign's), the rules and
// the scene graph content/stages/status.json, exported from the guide
// (tools/content/export_stage_info.py). This header is the
// evaluation, a port of tools/content/stage_status.py: it reads only a State, so it is
// tested without a game (tests/native_stage_info.cpp). stage_info.cpp fills the State
// from RDRAM on the game thread and hands the window thread a snapshot.
#include "json/json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace srw64::stage_info {
using json=nlohmann::json;

inline constexpr std::array<const char*,4> protagonists{"ark","serein","brad","manami"};

struct State {
    std::string protagonist;            // "ark" … "manami", empty before the name page
    int scene=-1;                       // 8010F5F0: this stage on the map, the next one in the intermission
    int cleared=-1;                     // 8010F5F1: the stage last cleared (the intermission's own)
    std::array<uint8_t,200> vars{};     // the 2-bit script variables (8015E818)
    std::set<int> pilots,units;         // the player's pilot and machine numbers
    std::map<int,int> kills;            // pilot number → shot down
    State(){vars.fill(3);}
};

// The rules and the scene graph of status.json, with the grants that carry finished
// elements into a later playthrough (legacy.hpp; a campaign's status has none).
struct Rules {
    json rules=json::array(),edge_guards=json::array(),grants=json::array();
    std::map<std::string,std::map<int,std::vector<int>>> graph;
    static Rules parse(const json& status) {
        Rules r;
        r.rules=status.at("rules");r.edge_guards=status.value("edge_guards",json::array());
        r.grants=status.value("grants",json::array());
        for(const auto& [who,edges]:status.at("graph").items())
            for(const auto& [from,to]:edges.items())r.graph[who][std::stoi(from)]=to.get<std::vector<int>>();
        return r;
    }
};

class Evaluator {
public:
    Evaluator(const Rules& rules,const State& state):state_(state) {
        // An edge a guard closes (the side branches' way back to the other route).
        std::set<std::pair<int,int>> closed;
        for(const auto& g:rules.edge_guards)if(!test(g.at("when")))
            for(const auto& from:g.at("from"))closed.emplace(from.get<int>(),g.at("to").get<int>());
        const auto graph=rules.graph.find(state.protagonist);
        if(state.scene<0)return;
        std::vector<int> todo{state.scene};reach_.insert(state.scene);
        while(!todo.empty()) {
            const int here=todo.back();todo.pop_back();
            if(graph==rules.graph.end())break;
            const auto next=graph->second.find(here);
            if(next==graph->second.end())continue;
            for(const int to:next->second)if(!reach_.contains(to) && !closed.contains({here,to})){reach_.insert(to);todo.push_back(to);}
        }
    }
    bool reachable(int scene) const {return reach_.contains(scene);}

    bool test(const json& p) const {
        const auto ids=[](const json& v){return v.is_array()?v.get<std::vector<int>>():std::vector<int>{v.get<int>()};};
        if(p.contains("all")){for(const auto& q:p["all"])if(!test(q))return false;return true;}
        if(p.contains("any")){for(const auto& q:p["any"])if(test(q))return true;return false;}
        if(p.contains("not"))return !test(p["not"]);
        if(p.contains("var")) {
            const unsigned n=p["var"].get<unsigned>();
            const int value=n<state_.vars.size()?state_.vars[n]:3;
            if(p.contains("eq"))return value==p["eq"].get<int>();
            for(const auto& v:p["in"])if(v.get<int>()==value)return true;
            return false;
        }
        if(p.contains("pilot")){for(const int id:ids(p["pilot"]))if(state_.pilots.contains(id))return true;return false;}
        if(p.contains("unit")){for(const int id:ids(p["unit"]))if(state_.units.contains(id))return true;return false;}
        if(p.contains("kills"))return kills(p["kills"].get<int>())>=p["ge"].get<int>();
        if(p.contains("protagonist")){for(const auto& who:p["protagonist"])if(who==state_.protagonist)return true;return false;}
        if(p.contains("reach"))return reachable(p["reach"].get<int>());
        if(p.contains("scene")){for(const auto& s:p["scene"])if(s.get<int>()==state_.scene)return true;return false;}
        return false;
    }

    // {"state": "done" | "step" | "not_started" | "missed" | "na" | "info" | "count" |
    // "chosen" | "pending", "step", "of", "value", "label", "counter": {"value", "of"}}
    json evaluate(const json& rule) const {
        const auto kind=rule.at("kind").get<std::string>();
        if(kind=="info")return {{"state","info"}};
        if(rule.contains("applies") && !test(rule["applies"]))return {{"state","na"}};
        if(kind=="count") {
            int have=0;
            for(const auto& p:rule["count"])have+=test(p);
            const int total=int(rule["count"].size());
            return {{"state",have==total?"done":"count"},{"value",have},{"of",total}};
        }
        if(kind=="choice") {
            for(const auto& option:rule["options"])if(test(option["when"]))return {{"state","chosen"},{"label",option["label"]}};
            if(!any_reachable(rule["at"]))return rule.contains("otherwise")?json{{"state","chosen"},{"label",rule["otherwise"]}}:json{{"state","missed"}};
            json pending={{"state","pending"}};
            if(rule.contains("counter"))pending["counter"]=counter(rule["counter"]);
            return pending;
        }
        if(test(rule.at("done")))return {{"state","done"}};
        // The furthest path among the routes still open (all of them before the split).
        const json* best=nullptr;int progress=-1;
        const auto consider=[&](const json& steps) {
            int reached=0;
            for(size_t i=0;i<steps.size();++i)if(test(steps[i]["when"]))reached=int(i)+1;
            if(reached>progress){progress=reached;best=&steps;}
        };
        if(rule.contains("paths")){for(const auto& path:rule["paths"])if(!path.contains("applies") || test(path["applies"]))consider(path["steps"]);}
        else consider(rule["steps"]);
        if(!best)return {{"state","na"}};
        if(progress==int(best->size()))return {{"state","done"}};
        const auto& step=(*best)[size_t(progress)];
        json result={{"step",progress},{"of",best->size()}};
        if(step.contains("counter"))result["counter"]=counter(step["counter"]);
        result["state"]=!any_reachable(step["at"])?"missed":progress?"step":"not_started";
        return result;
    }

private:
    int kills(int pilot) const {const auto k=state_.kills.find(pilot);return k==state_.kills.end()?0:k->second;}
    json counter(const json& c) const {return {{"value",kills(c["kills"].get<int>())},{"of",c["of"]}};}
    bool any_reachable(const json& scenes) const {for(const auto& s:scenes)if(reachable(s.get<int>()))return true;return false;}
    const State& state_;
    std::set<int> reach_;
};

// The route the player is on as far as the variables say: "oz" (dawn operation's first
// answer, var 10), "cp" (complete pacifism, var 11), "ia", or "" before the split.
inline std::string route(const State& s) {
    if(s.vars[10]==1)return "oz";
    if(s.vars[11]==1)return "cp";
    if(s.vars[10]==0)return "ia";
    return "";
}

// Window thread: the last snapshot, {"available": bool, "scene", "protagonist", "route",
// "status": {id: evaluate()}, "opening"}. want(true) starts the game thread sampling and
// returns this opening's number; a snapshot whose "opening" is older was read before the
// page opened and must not be shown (it may be another stage's).
json snapshot();
uint64_t want(bool on);
// A stage's metadata (srw64.stage-meta.v1, docs/design/stage-info.md §3): the story's
// content/stages/scene-NNN.json, or in a custom campaign the one its stage carries.
// Null when the stage has none. Window thread.
json meta(int scene);
// A metadata text, {locale: text} or a plain string, in LOCALE: else Japanese, English,
// then whichever it has.
inline std::string text(const json& value,const std::string& locale) {
    if(value.is_string())return value.get<std::string>();
    if(!value.is_object())return {};
    for(const auto& key:{locale,std::string("ja"),std::string("en")})
        if(value.contains(key) && value.at(key).is_string())return value.at(key).get<std::string>();
    for(const auto& [key,v]:value.items())if(v.is_string())return v.get<std::string>();
    return {};
}
// Game thread, each time the game reads the controller.
void sample(const uint8_t* ram);
// Game thread: the rules in play (the story's or the running campaign's), null when
// there are none, and the State they are judged on.
const Rules* rules();
State read_state(const uint8_t* ram);
// The intermission (mode 4, or 0x12-0x15 after the title's Load).
bool in_intermission(unsigned mode);
}
