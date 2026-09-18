#include "native_intro.hpp"
#include "intro_controls.hpp"
#include "game_hooks.hpp"
#include "state_probe.hpp"
#include "funcs.h"
#include "json/json.hpp"
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::intro {
namespace {
using json=nlohmann::json;
std::recursive_mutex mutex;
Controls controls;
std::filesystem::path output;
std::ofstream log;
bool loaded{};
uint64_t last_report{};
int last_major=-1,last_group=-1,last_page=-1,last_phase=-1;
json latest;       // The last step's state, for the debug interface.
unsigned skips{};
constexpr unsigned lengths[]={11,6,6,5,5};

void record(const char* kind,json fields) {
    fields["schema"]="srw64.native-intro-event.v1";
    fields["vi"]=srw64_current_vi();fields["kind"]=kind;
    log<<fields.dump()<<'\n';log.flush();
}
void step(uint8_t* rdram,recomp_context* ctx) {
    std::lock_guard lock(mutex);
    if(!loaded)return;
    const unsigned major=MEM_BU(0,int32_t(0x801CC3A6)), sub=MEM_BU(0,int32_t(0x801CC3A7));
    const unsigned group=MEM_BU(0,int32_t(0x801CC398)), page=MEM_BU(0,int32_t(0x801CC395));
    const unsigned phase=MEM_HU(0,int32_t(0x801CC36A));
    const bool active=major==13 && sub<=1 && group<5 && page<lengths[group];
    if(last_major==13 && major!=13)srw64::state_probe::capture(rdram,"intro-exited",group);
    controls.scene(active);
    json state={{"major",major},{"substate",sub},{"group",group},{"page",page},
        {"phase",phase},{"active",active},{"scene",MEM_BU(0,int32_t(0x8015DA02))}};
    if(last_major!=int(major) || (active && (last_group!=int(group) ||
        last_page!=int(page) || last_phase!=int(phase)))) {
        record("state",state);last_major=major;last_group=group;last_page=page;last_phase=phase;
    }
    if(controls.take(sub==1)) {
        // Same cleanup and completion as 801CA8C8..801CA93C (last page).
        // The original finish routine chooses the next mode and owns the fade,
        // music stop and eventual overlay transition. Do not advance scripts.
        recomp_context call=*ctx;
        call.r4=2+(page&1);
        resident_func_8008B888(rdram,&call);
        load_0010DA50_func_801CA5B4(rdram,&call);
        MEM_B(0,int32_t(0x801CC395))=lengths[group];
        MEM_B(0,int32_t(0x801CC3A7))=2;
        record("skip",{{"group",group},{"page",page},{"phase",phase},
            {"next_scene",MEM_BU(0,int32_t(0x8015DA02))}});
        state["active"]=false;state["substate"]=2;state["page"]=lengths[group];
        ++skips;
    }
    const auto vi=srw64_current_vi();
    latest=state;latest["vi"]=vi;
    if(vi>=last_report+30) {
        state["schema"]="srw64.native-intro-state.v1";state["vi"]=vi;
        std::ofstream(output/"intro-state.tmp")<<state.dump(2)<<'\n';
        std::filesystem::rename(output/"intro-state.tmp",output/"intro-state.json");
        last_report=vi;
    }
}
}
json state() {
    std::lock_guard lock(mutex);
    return {{"loaded",loaded},{"title_major",loaded?last_major:-1},{"skips",skips},
            {"step",loaded?latest:json(nullptr)}};
}
int title_major() {
    std::lock_guard lock(mutex);
    return loaded?last_major:-1;
}
void request_skip() {
    std::lock_guard lock(mutex);
    if(controls.active)controls.pending=true;
}
void configure(const std::filesystem::path& directory) {
    output=directory;log.open(output/"intro-events.jsonl");
    srw64_game_hooks.intro_step=step;
}
uint16_t input(uint16_t buttons) {
    std::lock_guard lock(mutex);return controls.input(buttons);
}
void overlay_loaded(uint32_t rom,uint32_t ram,uint32_t size) {
    std::lock_guard lock(mutex);
    if(uint64_t(ram)<0x801CC150ULL && uint64_t(ram)+size>0x801C4500ULL) {
        loaded=rom==0x10DA50 && ram==0x801C4500 && size==0x7C50;
        controls.scene(false);last_major=-1;
        record("overlay",{{"rom",rom},{"loaded",loaded}});
    }
}
}
