#include "cheats.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "native_dialogue.hpp"
#include "funcs.h"
#include "json/json.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

uint64_t srw64_current_vi();

namespace srw64::cheats {
namespace {
using namespace guest;
using json=nlohmann::json;

std::atomic<unsigned> switches{};
std::mutex mutex;
std::vector<Pilot> listed;
std::vector<std::pair<unsigned,unsigned>> requests;
std::atomic<uint64_t> menu_vi{};
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.cheat-event.v1"},{"kind",kind},{"vi",srw64_current_vi()}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
bool listed_pilot(const uint8_t* ram,uint32_t pilot) {
    const auto state=read(ram,pilot,1);
    return state && !(state&0x80);
}
std::vector<Pilot> list(const uint8_t* ram) {
    std::vector<Pilot> out;
    for(unsigned i=0;i<pilot_count;++i) {
        const uint32_t pilot=pilots_table+i*pilot_size;
        if(!listed_pilot(ram,pilot))continue;
        const unsigned number=read(ram,pilot+2,2);
        out.push_back({i,number,read(ram,pilot+5,1),dialogue::ui_text(ram,uint16_t(text_pilot_names+number))});
    }
    return out;
}

void frame(uint8_t* ram) {
    if(const unsigned on=switches.load(std::memory_order_relaxed))hold(ram,on);
}
// Runs before every step of the インターミッション menu, original or native.
void intermission(uint8_t* ram,recomp_context* ctx) {
    menu_vi=srw64_current_vi();
    std::vector<std::pair<unsigned,unsigned>> todo;
    {
        std::lock_guard lock(mutex);
        if(int8_t(read(ram,transition,1))==-1)todo.swap(requests);
    }
    for(const auto& [index,level]:todo) {
        const uint32_t pilot=pilots_table+index*pilot_size;
        if(index>=pilot_count || !listed_pilot(ram,pilot)){record("level-skipped",{{"index",index}});continue;}
        const unsigned before=read(ram,pilot+5,1);
        write_level(ram,pilot,level);
        // As 800AC220 and loading a save do: the whole record from its base and level,
        // with SP refilled (800A7F8C, a1 = 1).
        auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(pilot);c.r5=1;
        resident_func_800A7F8C(ram,&c);
        record("level",{{"index",index},{"number",read(ram,pilot+2,2)},{"from",before},{"to",read(ram,pilot+5,1)}});
    }
    auto now=list(ram);
    std::lock_guard lock(mutex);
    listed=std::move(now);
}
}

unsigned active(){return switches.load();}
void set_active(unsigned value) {
    unsigned all=0;
    for(const auto& entry:catalog)all|=entry.bit;
    value&=all;
    if(switches.exchange(value)!=value)record("switches",{{"switches",value}});
}
std::vector<Pilot> pilots() {
    if(srw64_current_vi()-menu_vi.load()>menu_grace_vis)return {};
    std::lock_guard lock(mutex);
    return listed;
}
void request_level(unsigned index,unsigned level) {
    std::lock_guard lock(mutex);
    std::erase_if(requests,[&](const auto& r){return r.first==index;});
    requests.emplace_back(index,std::clamp(level,1u,level_max));
    for(auto& pilot:listed)if(pilot.index==index)pilot.level=std::clamp(level,1u,level_max);
}

void configure(const std::filesystem::path& directory) {
    log.open(directory/"cheat-events.jsonl");
    srw64_game_hooks.cheats_frame=frame;
    srw64_game_hooks.cheats_intermission=intermission;
    if(const char* names=std::getenv("SRW64_CHEATS")) {
        unsigned value=0;
        std::stringstream list(names);
        for(std::string id;std::getline(list,id,',');)
            for(const auto& entry:catalog)if(id==entry.id)value|=entry.bit;
        set_active(value);
    }
}
}
