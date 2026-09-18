#pragma once
#include "recomp.h"
#include "script_inject.hpp"
#include "native_intro.hpp"
#include "state_probe.hpp"
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Opt-in debug facility: substitute a compiled mini stage for one scene. When the
// game registers a scene's scripts (8009DD58 -> 8009DE7C) the host overwrites the
// per-scene event buffer (8019B400) and deployment block (80199400) with the image
// and rebuilds the pointer table the registration walks. The map index chosen by
// 80209D6C is replaced afterwards when the image names one. ROM, catalog and the
// other scenes are untouched; without SRW64_MINI_STAGE nothing runs.
namespace srw64::mini_stage {
using script_trace::read;
using guest::write8;
using guest::write32;
constexpr uint32_t event_block=0x8019B400, event_limit=0x1A00, aux_block=0x80199400, aux_limit=0x2000, max_events=63;

struct Image {
    std::string name;
    std::vector<uint8_t> events, aux;
    std::vector<uint32_t> pointers;
    std::optional<uint32_t> map, slot;
};
struct State {
    std::mutex mutex;
    std::filesystem::path directory;
    std::optional<Image> image;
    std::optional<uint32_t> bound;   // scene index the image is bound to
    bool active{};                   // the bound scene is the one currently registered
    unsigned applied{};
    // Main-menu entry: armed by the hotkey (or SRW64_MINI_STAGE_ARM_VI) while the
    // title menu shows; the host then selects New Game and skips both prologues.
    bool armed{}, in_sequence{};
    uint64_t arm_vi{}, hotkey_vi{};
    unsigned skips{}, start_samples{};
    // Per-command state captures (SRW64_MINI_STAGE_CAPTURE=1): the script PC at
    // the last boundary that was captured, so each command is bracketed once.
    bool capture_commands{};
    uint32_t last_capture_pc{};
    unsigned captures{};
    // Early exit (SRW64_MINI_STAGE_EXIT_AFTER=<opcode>): once that opcode has been
    // reached in the substituted events, the run ends after a grace period instead
    // of burning the rest of the VI budget. A probe only needs the span around the
    // command under test; the grace lets its effect and a few frames after it land.
    uint32_t exit_opcode{};
    uint64_t exit_grace{300}, exit_deadline{};
    bool exit_seen{};
};
inline State& state(){static State s;return s;}

inline void log(const nlohmann::json& fields) {
    auto& s=state();
    if(s.directory.empty())return;
    nlohmann::json row=fields;row["schema"]="srw64.mini-stage-event.v1";row["vi"]=srw64_current_vi();
    std::ofstream(s.directory/"mini-stage-events.jsonl",std::ios::app)<<row.dump()<<'\n';
}

inline std::vector<uint8_t> unhex(const std::string& hex) {
    if(hex.size()%2 || hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("mini stage image: bad hex");
    std::vector<uint8_t> out;
    for(size_t i=0;i<hex.size();i+=2)out.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
    return out;
}

inline Image parse(const nlohmann::json& document) {
    if(document.value("schema","")!="srw64.mini-stage-image.v1")throw std::runtime_error("mini stage image: unsupported schema");
    Image image;
    image.name=document.value("name","mini-stage");
    image.events=unhex(document.at("events_hex").get<std::string>());
    image.aux=unhex(document.at("aux_hex").get<std::string>());
    if(image.events.empty() || image.events.size()>event_limit)throw std::runtime_error("mini stage image: event bytes out of range");
    if(image.aux.size()<4 || image.aux.size()>aux_limit)throw std::runtime_error("mini stage image: deployment bytes out of range");
    for(const auto& event:document.at("events")) {
        const uint32_t offset=event.at("offset").get<uint32_t>(), size=event.at("size").get<uint32_t>();
        if(offset%4 || offset+size>image.events.size())throw std::runtime_error("mini stage image: event outside the block");
        image.pointers.push_back(event_block+offset);
    }
    if(image.pointers.empty() || image.pointers.size()>max_events)throw std::runtime_error("mini stage image: event count out of range");
    if(!document.at("map").is_null())image.map=document.at("map").get<uint32_t>();
    if(!document.at("slot").is_null())image.slot=document.at("slot").get<uint32_t>();
    return image;
}

inline void configure(const std::filesystem::path& directory) {
    const char* path=std::getenv("SRW64_MINI_STAGE");
    if(!path || !*path)return;
    auto& s=state();
    std::lock_guard lock(s.mutex);
    s.directory=directory;
    std::ifstream file(path);
    if(!file)throw std::runtime_error(std::string("mini stage image unreadable: ")+path);
    nlohmann::json document;file>>document;
    s.image=parse(document);
    const char* capture=std::getenv("SRW64_MINI_STAGE_CAPTURE");
    s.capture_commands=capture && std::string_view(capture)=="1";
    if(const char* stop=std::getenv("SRW64_MINI_STAGE_EXIT_AFTER")) {
        s.exit_opcode=uint32_t(std::strtoul(stop,nullptr,16));
        if(s.exit_opcode<0x3D30 || s.exit_opcode>0x3E1F)throw std::runtime_error("SRW64_MINI_STAGE_EXIT_AFTER must be a script opcode in hex");
    }
    if(const char* grace=std::getenv("SRW64_MINI_STAGE_EXIT_GRACE"))s.exit_grace=std::strtoull(grace,nullptr,10);
    log({{"action","loaded"},{"capture_commands",s.capture_commands},{"path",path},{"name",s.image->name},{"events",s.image->pointers.size()},
         {"event_bytes",s.image->events.size()},{"aux_bytes",s.image->aux.size()},
         {"map",s.image->map?nlohmann::json(*s.image->map):nlohmann::json()},
         {"slot",s.image->slot?nlohmann::json(*s.image->slot):nlohmann::json()}});
}

// Game thread, entry of 8009DE7C(load mode, engine, pointer table, deployment
// block). The first registered scene binds the image unless it names a slot.
inline void register_hook(uint8_t* ram,recomp_context* ctx) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.image)return;
    const uint32_t scene=read(ram,0x8010F5F0,1), mode=uint32_t(ctx->r4), table=uint32_t(ctx->r6), original_aux=uint32_t(ctx->r7);
    if(!s.bound)s.bound=s.image->slot?*s.image->slot:scene;
    if(scene!=*s.bound){s.active=false;log({{"action","skipped"},{"scene",scene},{"bound",*s.bound},{"mode",mode}});return;}
    const auto& image=*s.image;
    for(size_t i=0;i<image.events.size();++i)write8(ram,event_block+uint32_t(i),image.events[i]);
    for(size_t i=0;i<image.aux.size();++i)write8(ram,aux_block+uint32_t(i),image.aux[i]);
    for(size_t i=0;i<image.pointers.size();++i)write32(ram,table+uint32_t(i*4),image.pointers[i]);
    write32(ram,table+uint32_t(image.pointers.size()*4),0xFFFFFFFF);
    ctx->r7=int32_t(aux_block); // deployment base the registration stores at engine+0
    s.active=true;++s.applied;
    log({{"action","applied"},{"scene",scene},{"mode",mode},{"pointer_table",table},{"original_aux",original_aux},
         {"events",image.pointers.size()},{"event_bytes",image.events.size()},{"aux_bytes",image.aux.size()},{"applied",s.applied}});
}

// Game thread, after 80209D6C wrote the scene's map index into 8010F5EE.
inline void map_hook(uint8_t* ram) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.image || !s.active || !s.image->map)return;
    const uint32_t scene=read(ram,0x8010F5F0,1);
    if(s.bound && scene!=*s.bound)return;
    const auto original=read(ram,0x8010F5EE,1);
    write8(ram,0x8010F5EE,uint8_t(*s.image->map));
    log({{"action","map"},{"scene",scene},{"original",original},{"map",*s.image->map}});
}

// Game thread, after each script poll. With SRW64_MINI_STAGE_CAPTURE=1 the state
// probe takes one snapshot per command boundary of the substituted events, so a
// command that finishes in zero VI and plays nothing still has a before/after
// pair to read its field writes from. The PC is the script engine's own cursor;
// nothing is written back.
inline void poll_hook(uint8_t* ram,uint32_t owner) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.image || !s.active)return;
    const uint32_t pc=read(ram,owner+0x1C,4);
    if(pc<event_block || pc>=event_block+uint32_t(s.image->events.size()))return;
    const uint32_t offset=pc-event_block;
    // The exit watcher latches on exit_seen, so it needs no boundary dedup of its
    // own and must not share the capture's: capture can be switched on at any
    // time and would otherwise lose the boundary the watcher had already seen.
    if(s.exit_opcode && !s.exit_seen && offset+1<s.image->events.size() &&
       ((uint32_t(s.image->events[offset])<<8)|s.image->events[offset+1])==s.exit_opcode) {
        s.exit_seen=true;
        s.exit_deadline=srw64_current_vi()+s.exit_grace;
        log({{"action","exit-armed"},{"opcode",s.exit_opcode},{"offset",offset},{"deadline",s.exit_deadline}});
    }
    if(!s.capture_commands || !srw64::state_probe::enabled() || pc==s.last_capture_pc)return;
    s.last_capture_pc=pc;
    srw64::state_probe::capture(ram,"mini-stage-command",offset);
    ++s.captures;
    log({{"action","capture"},{"offset",offset},{"pc",pc},{"captures",s.captures}});
}

// VI thread. True once the watched opcode's grace period has elapsed.
inline bool finished() {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.exit_seen || !s.exit_deadline || srw64_current_vi()<s.exit_deadline)return false;
    s.exit_deadline=0;
    log({{"action","exit"},{"opcode",s.exit_opcode}});
    return true;
}

// Hotkey (window thread): remembered until the VI thread sees the main menu.
inline void hotkey() {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.image)return;
    s.hotkey_vi=srw64_current_vi();
}

// Input filter (VI thread, after the keyboard/script merge). Arming needs the
// title main menu (intro overlay state 3); the host then presses START once to
// choose New Game and skips both text prologues. Protagonist and name pages are
// left to the player or the input script; the entry completes when the first
// scene is registered with the image.
inline uint16_t input(uint16_t buttons) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    if(!s.image)return buttons;
    const auto vi=srw64_current_vi();
    static const uint64_t auto_arm=[]{const char* p=std::getenv("SRW64_MINI_STAGE_ARM_VI");return p?std::strtoull(p,nullptr,10):0;}();
    const int major=srw64::intro::title_major();
    if(!s.armed && !s.applied) {
        const bool from_hotkey=s.hotkey_vi && vi>=s.hotkey_vi, from_env=auto_arm && vi>=auto_arm;
        if(major==3 && (from_hotkey || from_env)) {
            s.armed=true;s.arm_vi=vi;s.hotkey_vi=0;
            log({{"action","armed"},{"source",from_hotkey?"hotkey":"env"}});
        } else if(from_hotkey) {
            log({{"action","hotkey-ignored"},{"title_major",major}});s.hotkey_vi=0;
        }
    }
    if(!s.armed)return buttons;
    // START on the main menu chooses New Game. Hold it across several controller
    // polls: the game latches presses on consecutive reads, and the poll cadence
    // is not tied to the VI counter.
    if(s.start_samples<4){++s.start_samples;return uint16_t(buttons|0x1000);}
    if(major==13) {
        srw64::intro::request_skip();
        if(!s.in_sequence){s.in_sequence=true;++s.skips;log({{"action","skip-requested"},{"sequence",s.skips}});}
    } else s.in_sequence=false;
    if(s.applied){s.armed=false;log({{"action","entered"},{"skips",s.skips},{"since_arm_vis",vi-s.arm_vi}});}
    return buttons;
}
}
