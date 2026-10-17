#include "rewind.hpp"
#include "notices.hpp"
#include "save_store.hpp"
#include "localization/catalog.hpp"
#include "funcs.h"
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

uint64_t srw64_current_vi();
namespace srw64::rewind {
namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;
// 80093278(1): the map into the suspend area; 80080188: the next game mode; 80099814(5,1,2):
// the fade the original leaves a screen with (801D22FC, 801C9A04).
constexpr uint32_t serialize=0x80093278,set_mode=0x80080188,fade=0x80099814;
constexpr uint64_t leave_limit=1200;   // VIs: a fade and a restore take well under one second

std::mutex mutex;
Ring ring;
fs::path folder;
std::ofstream log;
std::atomic_bool asked{};
std::atomic<Stage> stage{Stage::idle};
uint64_t next_id{},since{};
size_t target_index{};
fs::path target_record;
unsigned last_state{0xFF},idle_from{0xFF},stretch{};
bool was_idle{};
// The state just restored, until its first quiet idle: that idle is it again and is not kept.
// A restore at the enemy phase's end goes on to the player's turn without one, so only an
// idle in the same phase and turn is skipped.
std::optional<std::pair<unsigned,unsigned>> restored_at;
std::atomic<uint64_t> outcomes{};   // every notice the button led to, for checks
std::string last_outcome;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.rewind-event.v1"},{"kind",kind},{"vi",srw64_current_vi()}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
void notice(const char* key) {
    last_outcome=key;++outcomes;
    notices::post("rewind",localization::catalog().ui(key));
}
fs::path path_of(uint64_t id){char name[32];std::snprintf(name,sizeof name,"%06llu.sus",static_cast<unsigned long long>(id));return folder/name;}
void remove(const std::vector<uint64_t>& ids){for(const auto id:ids){std::error_code e;fs::remove(path_of(id),e);}}
void call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);
    LOOKUP_FUNC(function)(ram,&c);
}
// The map as the suspend format holds it now, serialized into the original's buffer only.
std::vector<uint8_t> current(uint8_t* ram,recomp_context* ctx) {
    call(ram,ctx,serialize,0);
    std::vector<uint8_t> bytes(suspend_size);
    for(unsigned i=0;i<suspend_size;++i)bytes[i]=uint8_t(read(ram,suspend_buffer+i,1));
    return bytes;
}
void clear(const char* why) {
    if(ring.empty())return;
    remove(ring.clear());
    record("cleared",{{"why",why}});
}
void keep(uint8_t* ram,recomp_context* ctx) {
    auto bytes=current(ram,ctx);
    if(ring.matches_newest(bytes))return;
    const unsigned title=read(ram,progress+9,1);
    if(!ring.empty() && ring.entries().back().title!=title)clear("chapter");
    const uint64_t id=++next_id;
    {
        save_store::SuspendWindow window(path_of(id));
        call(ram,ctx,serialize,1);
    }
    std::error_code e;
    if(!fs::exists(path_of(id),e)){record("keep-failed",{{"id",id}});return;}
    Entry entry{id,std::move(bytes),read(ram,progress,1),read(ram,progress+2,2),title};
    record("kept",{{"id",id},{"phase",entry.phase},{"turn",entry.turn},{"state",read(ram,map_state,1)}});
    remove(ring.push(std::move(entry)));
}
// Arms the record of the state at `index` for the next suspend reads.
void arm(size_t index) {
    target_index=index;target_record=path_of(ring.entries()[index].id);
    save_store::arm_suspend(target_record);
    since=srw64_current_vi();
}
void give_up(const char* why) {
    save_store::disarm_suspend();
    stage=Stage::idle;
    record("gave-up",{{"why",why}});
}
}

void configure(const fs::path& run_directory) {
    if(!save_store::enabled())return;
    std::lock_guard lock(mutex);
    folder=run_directory/"rewind";
    std::error_code e;
    fs::remove_all(folder,e);fs::create_directories(folder,e);
    log.open(run_directory/"rewind-events.jsonl");
}
void request() {
    if(!on.load()){std::lock_guard lock(mutex);notice("rewind_notice_off");return;}
    asked=true;
}
void frame(uint8_t* ram) {
    if(folder.empty())return;
    std::lock_guard lock(mutex);
    const unsigned m=read(ram,mode,1);
    const bool on_map=tactical(m) || m==2 || m==0x11 || m==0x16 || m==0x22;
    if(asked.exchange(false)) {
        if(stage!=Stage::idle){}
        else if(!on_map || ring.empty()){notice("rewind_notice_none");record("refused",{{"mode",m},{"kept",ring.entries().size()}});}
        else {stage=Stage::pending;since=srw64_current_vi();record("asked",{{"mode",m}});}
    }
    const Stage now=stage;
    if(now==Stage::idle || now==Stage::pending) {
        // Off the map (the intermission, the title, the story between maps): its states are gone.
        if(!on_map){if(now==Stage::pending){stage=Stage::idle;record("dropped");}clear("left");}
        if(!on.load())clear("off");
        return;
    }
    if(srw64_current_vi()-since>leave_limit)give_up("timeout");
}
void map_frame(uint8_t* ram,recomp_context* ctx) {
    if(folder.empty())return;
    std::lock_guard lock(mutex);
    // A quiet idle stretch: the map idle with no event running. An event (the opening, a
    // turn's) often runs with the map already idle, so the stretch begins when it ends.
    const unsigned state=read(ram,map_state,1);
    const bool idle_now=state==5 && quiet(ram);
    if(!idle_now)stretch=0;
    else{if(!was_idle){idle_from=last_state;stretch=0;}++stretch;}
    was_idle=idle_now;last_state=state;
    const Stage now=stage;
    if(now==Stage::leaving_map || now==Stage::leaving_battle) {
        // 800936A0 has read the record (autosave.cpp's restored hook disarms it): the map is
        // back at that state, and the states after it are the future now.
        if(!save_store::armed_suspend() && tactical(read(ram,mode,1))) {
            remove(ring.keep_through(target_index));
            const auto& back=ring.entries()[target_index];
            stage=Stage::idle;restored_at=std::pair{back.phase,back.turn};
            record("restored",{{"index",target_index},{"kept",ring.entries().size()},{"phase",read(ram,progress,1)},{"turn",read(ram,progress+2,2)}});
            notice("rewind_notice_done");
        }
        return;
    }
    if(!on.load())return;
    const bool battle_screen=srw64_current_vi()-game_speed::menu_vi.load()<4;
    if(now==Stage::pending && leavable(ram,battle_screen)) {
        const auto index=ring.target(ring.matches_newest(current(ram,ctx)));
        if(!index){stage=Stage::idle;notice("rewind_notice_none");record("nothing-earlier");return;}
        arm(*index);
        call(ram,ctx,set_mode,0x11);
        call(ram,ctx,fade,5,1,2);
        stage=Stage::leaving_map;
        record("leaving",{{"from","map"},{"index",*index},{"state",state},{"phase",read(ram,progress,1)}});
        return;
    }
    // A quiet idle that has lasted two frames, not the cursor stopping (state 6 is its motion).
    if(idle_now && stretch==2 && idle_from!=6) {
        const std::pair<unsigned,unsigned> now_at{read(ram,progress,1),read(ram,progress+2,2)};
        const bool same_state=restored_at==now_at;
        restored_at.reset();
        if(same_state)return;
        keep(ram,ctx);
    }
}
void battle_frame(uint8_t* ram,recomp_context* ctx) {
    if(folder.empty())return;
    std::lock_guard lock(mutex);
    const Stage now=stage;
    if(now==Stage::leaving_battle) {
        // The abort branch (801C97F0) reads START held once the fade is over.
        guest::write16(ram,held,uint16_t(read(ram,held,2)|start_button));
        return;
    }
    if(now!=Stage::pending || !battle_abortable(ram))return;
    // The battle began after the newest state was kept: that one is the state before it.
    const auto index=ring.target(false);
    if(!index){stage=Stage::idle;notice("rewind_notice_none");return;}
    arm(*index);
    guest::write8(ram,battle_abort,1);
    call(ram,ctx,fade,5,1,2);
    guest::write16(ram,held,uint16_t(read(ram,held,2)|start_button));
    stage=Stage::leaving_battle;
    record("leaving",{{"from","battle"},{"index",*index},{"battle_state",int8_t(read(ram,battle_state,1))}});
}
uint32_t filter_input(uint32_t buttons) {
    switch(stage.load()) {
    case Stage::leaving_map: return 0;
    case Stage::leaving_battle: return start_button;
    default: return buttons;
    }
}
json state() {
    std::lock_guard lock(mutex);
    auto kept=json::array();
    for(const auto& e:ring.entries())kept.push_back({{"id",e.id},{"phase",e.phase},{"turn",e.turn}});
    static constexpr const char* names[]={"idle","pending","leaving_map","leaving_battle"};
    return {{"on",on.load()},{"available",!folder.empty()},{"stage",names[size_t(stage.load())]},{"kept",kept},
            {"outcomes",outcomes.load()},{"last",last_outcome}};
}
}
