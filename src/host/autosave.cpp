#include "autosave.hpp"
#include "app/sha256.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "save_store.hpp"
#include "script_inject.hpp"
#include "funcs.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>

uint64_t srw64_current_vi();
namespace srw64::autosave {
namespace {
using json=nlohmann::json;
using namespace guest;
// D_801DD540: the intermission's exit, 2 for 次のマップへ (801CE19C, 801D8D20).
constexpr uint32_t exit_code=0x801DD540;
// 80092678(slot) writes the intermission's state to a slot; 80093278(1) the map's to
// the suspend area (docs/design/save-slots-autosave.md §1.2-1.3).
constexpr uint32_t write_slot=0x80092678,write_suspend=0x80093278;
// The map's main state (5 idle), the progress block (+2 turn from 0, +9 title, +4
// total turns, +0xC funds) and the random numbers (state_probe's game_rng).
constexpr uint32_t map_state=0x80172EB0,progress=0x8010F5E8,rng=0x800D49D0;
constexpr unsigned rng_size=0x834;
std::mutex mutex;
bool entered{},left{};
std::optional<std::pair<unsigned,unsigned>> last_turn;   // (title, turn) of the last turn autosave
std::optional<std::filesystem::path> loading;
std::optional<std::vector<uint8_t>> pending_rng;
std::ofstream log;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.autosave-event.v1"},{"kind",kind},{"vi",srw64_current_vi()}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
uint32_t call(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);LOOKUP_FUNC(function)(ram,&c);
    return uint32_t(c.r2);
}
std::string now() {
    const auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&t);
#else
    localtime_r(&t,&local);
#endif
    char text[32];std::strftime(text,sizeof text,"%Y-%m-%dT%H:%M:%S",&local);
    return text;
}
json about(const uint8_t* ram,const char* node) {
    return {{"schema","srw64.autosave.v1"},{"node",node},{"time",now()},{"title",read(ram,progress+9,1)},
            {"map_turn",read(ram,progress+2,2)},{"turns",read(ram,progress+4,2)},{"funds",read(ram,progress+0xC,4)}};
}
std::string hex(const std::vector<uint8_t>& bytes) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out;out.reserve(bytes.size()*2);
    for(const auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}
    return out;
}
std::optional<std::vector<uint8_t>> unhex(const std::string& text) {
    if(text.size()%2)return std::nullopt;
    std::vector<uint8_t> out;
    const auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<text.size();i+=2) {
        const int hi=nibble(text[i]),lo=nibble(text[i+1]);
        if(hi<0 || lo<0)return std::nullopt;
        out.push_back(uint8_t(hi<<4|lo));
    }
    return out;
}
std::string digest(const std::filesystem::path& path){try{return app::sha256_file(path);}catch(const std::exception&){return {};}}

// The intermission state in the slot format, as データセーブ writes it.
void intermission(uint8_t* ram,recomp_context* ctx,const char* node) {
    const auto choices=save_store::settings();
    if(!choices.autosave)return;
    auto info=about(ram,node);
    const bool saved=save_store::autosave(false,choices.intermission,info,[&](const std::filesystem::path& path) {
        save_store::Window target(0,path);
        call(ram,ctx,write_slot,0);
    });
    record("intermission",{{"node",node},{"saved",saved}});
}
void entered_intermission(uint8_t*,unsigned how) {
    std::lock_guard lock(mutex);
    // 0: after a map or a story scene; 1-2 a load from the title, 3+ the Controller Pak.
    entered=how==0;left=false;last_turn.reset();
}
void intermission_step(uint8_t* ram,recomp_context* ctx) {
    bool now_entered=false,now_leaving=false;
    {
        std::lock_guard lock(mutex);
        if(entered){entered=false;now_entered=true;}
        if(!left && read(ram,exit_code,1)==2){left=true;now_leaving=true;}
        if(now_leaving)last_turn.reset();
    }
    if(now_entered)intermission(ram,ctx,"entered");
    if(now_leaving)intermission(ram,ctx,"sortie");
}
// The map in the suspend format, once a turn, on the first idle frame of the player's phase.
void map_idle(uint8_t* ram,recomp_context* ctx) {
    const auto choices=save_store::settings();
    if(!choices.autosave || read(ram,map_state,1)!=5 || !script_inject::idle_reason(ram).empty())return;
    const std::pair<unsigned,unsigned> key{read(ram,progress+9,1),read(ram,progress+2,2)};
    {
        std::lock_guard lock(mutex);
        if(last_turn==key)return;
        last_turn=key;
    }
    auto info=about(ram,"turn");
    std::vector<uint8_t> numbers(rng_size);
    for(unsigned i=0;i<rng_size;++i)numbers[i]=uint8_t(read(ram,rng+i,1));
    info["rng"]=hex(numbers);
    const bool saved=save_store::autosave(true,choices.turn,info,[&](const std::filesystem::path& path) {
        {
            save_store::SuspendWindow target(path);
            call(ram,ctx,write_suspend,1);
        }
        info["record_sha256"]=digest(path);
    });
    record("turn",{{"title",key.first},{"map_turn",key.second},{"saved",saved}});
}
// After 800936A0: a turn autosave being loaded has been read; its random numbers go
// back after the map seeds them again (801E00AC, the next 800821B0).
void restored(uint8_t* ram,bool from_sram) {
    if(!from_sram)return;
    std::lock_guard lock(mutex);
    last_turn={read(ram,progress+9,1),read(ram,progress+2,2)};
    const auto armed=save_store::armed_suspend();
    save_store::disarm_suspend();
    if(!armed || !loading || *armed!=*loading){loading.reset();return;}
    auto note=*loading;note.replace_extension(".json");
    std::ifstream file(note);
    const auto info=json::parse(file,nullptr,false);
    loading.reset();
    if(!info.is_object() || info.value("record_sha256",std::string())!=digest(*armed)){record("rng-skipped",{{"record",armed->filename().string()}});return;}
    if(auto numbers=unhex(info.value("rng",std::string()));numbers && numbers->size()==rng_size)pending_rng=std::move(numbers);
    record("restored",{{"record",armed->filename().string()},{"rng",pending_rng.has_value()}});
}
void seeded(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(!pending_rng)return;
    for(unsigned i=0;i<rng_size;++i)write8(ram,rng+i,(*pending_rng)[i]);
    pending_rng.reset();
    record("rng-restored");
}
}

void configure(const std::filesystem::path& directory) {
    if(!save_store::enabled())return;
    log.open(directory/"autosave-events.jsonl");
    auto& h=srw64_game_hooks;
    h.intermission_enter=entered_intermission;h.intermission_after_step=intermission_step;
    h.turn_idle=map_idle;h.tactical_restored=restored;h.rng_seeded=seeded;
}
void prepare_turn_load(const std::filesystem::path& path) {
    std::lock_guard lock(mutex);
    loading=path;pending_rng.reset();
    record("load",{{"record",path.filename().string()}});
}
}
