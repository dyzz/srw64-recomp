#pragma once
#include "script_trace.hpp"
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Opt-in debug facility: run a caller-supplied event script through the
// original script engine while the tactical map is idle. The script lives in a
// scratch region the game never touches, is started exactly like a special
// slot event (8009EDB8 -> 8009EE98) and is reclaimed when the engine reaches
// FFFF. Nothing here patches ROM bytes, catalog data or the game's own events.
namespace srw64::script_inject {
using script_trace::read;
inline bool enabled() {
    static const bool value=[] {const char* p=std::getenv("SRW64_SCRIPT_INJECT");return p && std::string_view(p)=="1";}();
    return value;
}
constexpr uint32_t engine=0x8015F950, owner=engine+0x948, scratch=0x807F0000, scratch_size=0x10000;
constexpr uint32_t header_words=5, max_words=scratch_size/2;
constexpr uint64_t defer_limit=1800; // VIs a queued request may wait for an idle map

struct Request {uint64_t sequence{},at_vi{};std::vector<uint16_t> words;};
struct Injection {uint64_t sequence{},applied_vi{},last_progress_vi{};uint32_t entry{},end{},last_pc{};uint16_t saved_count{};};
struct State {
    std::mutex mutex;
    std::filesystem::path directory;
    std::optional<Request> pending;
    std::optional<Injection> active;
    uint64_t last_sequence{},completed{},completed_sequence{};   // completed_sequence: the last one to finish
    std::string last_defer;
};
inline State& state(){static State s;return s;}

using guest::write8;
using guest::write16;
using guest::write32;

inline void log(const nlohmann::json& fields) {
    auto& s=state();
    if(s.directory.empty())return;
    nlohmann::json row=fields;row["schema"]="srw64.script-inject-event.v1";row["vi"]=srw64_current_vi();
    std::ofstream(s.directory/"script-inject-events.jsonl",std::ios::app)<<row.dump()<<'\n';
}

// "SRWJ1 <sequence> <at_vi> <hex words>": header (type + four parameters),
// instructions, and a final FFFF. The reader never consumes a partial line.
inline std::optional<Request> parse(const std::string& line,std::string& error) {
    std::string magic,hex,extra;uint64_t sequence{},at_vi{};
    std::istringstream in(line);
    if(!(in>>magic>>sequence>>at_vi>>hex) || (in>>extra)){error="malformed";return std::nullopt;}
    if(magic!="SRWJ1"){error="magic";return std::nullopt;}
    if(hex.size()%4 || hex.size()<(header_words+1)*4 || hex.size()>max_words*4 ||
       hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos){error="words";return std::nullopt;}
    Request r;r.sequence=sequence;r.at_vi=at_vi;
    for(size_t i=0;i<hex.size();i+=4)r.words.push_back(uint16_t(std::stoul(hex.substr(i,4),nullptr,16)));
    if(r.words.back()!=0xFFFF){error="terminator";return std::nullopt;}
    if(r.words[0]>14){error="event-type";return std::nullopt;}
    return r;
}

// VI thread: called every few VIs like control.txt.
inline void poll_file(const std::filesystem::path& directory) {
    if(!enabled())return;
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(s.directory.empty())s.directory=directory;
    std::ifstream file(directory/"script-inject.txt");
    std::string line;
    if(!file || !std::getline(file,line))return;
    std::string error;
    auto request=parse(line,error);
    uint64_t sequence=0;
    {std::istringstream probe(line);std::string magic;probe>>magic>>sequence;}
    if(sequence<=s.last_sequence)return; // already handled or stale
    s.last_sequence=sequence;
    if(!request){log({{"action","rejected"},{"sequence",sequence},{"reason",error}});return;}
    if(s.pending || s.active){log({{"action","rejected"},{"sequence",sequence},{"reason","busy"}});return;}
    s.pending=*request;s.last_defer.clear();
    log({{"action","queued"},{"sequence",sequence},{"at_vi",request->at_vi},{"words",request->words.size()}});
}

inline std::string idle_reason(const uint8_t* ram) {
    if(read(ram,engine+4,2)!=0xC0)return "engine-phase";
    if(read(ram,engine+0x97C,2)!=0x80)return "event-running";
    if(read(ram,engine+0x9AA,2)!=0)return "poll-phase";
    if(read(ram,0x8010F5E8,1)!=1)return "not-player-phase"; // phases are 1-based: 1 player, 2 enemy
    if(read(ram,0x8010F6B0,2)!=0)return "defeat-sequence";
    const auto mode=read(ram,0x8015DA02,1);
    if(mode!=3 && mode!=0xB)return "not-tactical-map";
    for(uint32_t i=0;i<scratch_size;i+=4)if(read(ram,scratch+i,4)!=0)return "scratch-in-use";
    return {};
}

inline nlohmann::json snapshot_json(const uint8_t* ram) {
    const auto s=script_trace::snapshot(ram,engine,owner);
    return {{"pc",s.pc},{"state",s.state},{"engine_phase",s.engine_phase},{"scene",s.scene},{"turn",s.turn},
            {"side",s.side},{"route",s.route},{"acc",s.acc},{"units",{s.units[0],s.units[1],s.units[2]}},
            {"registered_events",read(ram,engine+0x98E,2)}};
}

// Game thread: frame boundary. Applies a due request when the map is idle.
inline void service(uint8_t* ram) {
    if(!enabled())return;
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.pending || s.active)return;
    const auto vi=srw64_current_vi();
    if(vi<s.pending->at_vi)return;
    const auto reason=idle_reason(ram);
    if(!reason.empty()) {
        if(reason!=s.last_defer){s.last_defer=reason;log({{"action","deferred"},{"sequence",s.pending->sequence},{"reason",reason}});}
        if(vi>s.pending->at_vi+defer_limit){log({{"action","rejected"},{"sequence",s.pending->sequence},{"reason","idle-timeout:"+reason}});s.pending.reset();}
        return;
    }
    const auto& words=s.pending->words;
    for(size_t i=0;i<words.size();++i)write16(ram,scratch+uint32_t(i*2),words[i]);
    Injection active;
    active.sequence=s.pending->sequence;active.applied_vi=vi;active.last_progress_vi=vi;
    active.entry=scratch;active.end=scratch+uint32_t(words.size()*2);active.last_pc=scratch+header_words*2;
    active.saved_count=uint16_t(read(ram,engine+0x98E,2));
    // Mirror 8009EE98: PC after the five header words, cleared command state,
    // no handler, remembered position cleared; then start like 8009EDB8.
    write32(ram,owner+0x1C,active.last_pc);
    for(uint32_t off=0x22;off<=0x2E;off+=2)write16(ram,owner+off,0);
    write32(ram,owner+0x30,0);write8(ram,owner+8,0xFF);write8(ram,owner+9,0xFF);
    write16(ram,engine+0x990,0);write16(ram,engine+0x97C,0x2000);
    std::string hex;char buffer[8];
    for(auto w:words){std::snprintf(buffer,sizeof buffer,"%04X",w);hex+=buffer;}
    log({{"action","applied"},{"sequence",active.sequence},{"entry",active.entry},{"end",active.end},
         {"words_hex",hex},{"before",snapshot_json(ram)}});
    s.active=active;s.pending.reset();
}

// Game thread: after every original 8009EFDC poll. Detects completion, keeps
// the registered-event counter and returns the scratch region to zero.
inline void observe(uint8_t* ram) {
    if(!enabled())return;
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.active)return;
    auto& a=*s.active;
    const auto vi=srw64_current_vi();
    const auto pc=read(ram,owner+0x1C,4), st=read(ram,owner+0x24,2), status=read(ram,engine+0x97C,2);
    if(pc!=a.last_pc){a.last_pc=pc;a.last_progress_vi=vi;}
    const bool finished=(pc==0 && st==0x80) || status==0x80;
    if(finished) {
        write16(ram,engine+0x98E,a.saved_count);
        for(uint32_t i=0;i<uint32_t(a.end-a.entry);i+=2)write16(ram,a.entry+i,0);
        log({{"action","complete"},{"sequence",a.sequence},{"elapsed_vis",vi-a.applied_vi},{"after",snapshot_json(ram)}});
        ++s.completed;s.completed_sequence=a.sequence;s.active.reset();
        return;
    }
    if(pc<a.entry || pc>a.end) {
        log({{"action","escaped"},{"sequence",a.sequence},{"pc",pc},{"note","PC left the injected range; original event flow took over"}});
        s.active.reset();return;
    }
    if(vi-a.last_progress_vi==3600)log({{"action","stalled"},{"sequence",a.sequence},{"pc",pc},{"state",st},{"handler",read(ram,owner+0x30,4)}});
}
}
