#include "autosave.hpp"
#include "run_files.hpp"
#include "app/sha256.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "save_store.hpp"
#include "script_inject.hpp"
#include "campaign.hpp"
#include "mini_stage.hpp"
#include "app/runtime.hpp"
#include "funcs.h"
#include <algorithm>
#include <atomic>
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
// 80093278 serializes into the buffer at 800FBEF0 before it writes the card, and that buffer
// is also the GAME OVER retry's snapshot: taken as the opening ends (8009EDB8, with the story
// variables backed up beside it by 800A47D4(0)), restored by 800936A0(0) on A at GAME OVER,
// after which 8009DD58(scene, 2) leaves every event unexecuted and the variables as at the
// opening. The original's own 中断 never reaches that retry: it sets 80172F09, and a GAME
// OVER after it returns to the title for コンティニュー. A turn autosave sets nothing, so
// before this the retry restored that turn's map with the opening's events: reinforcements
// already on the map came again, the same named enemy twice on one unit record (shared HP,
// a crash once one was destroyed; a player, 0.4.10). The buffer is put back after our
// write, so a GAME OVER restarts the stage, as without autosaves.
constexpr uint32_t retry_snapshot=0x800FBEF0,retry_snapshot_size=0x3AE0;
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
// The newest autosave this session wrote: what a game closed in the background goes back to
// when it was not at a save point as it left.
std::optional<std::filesystem::path> last_written;
void record(const char* kind,json extra=json::object());

// Going to the background (Android, docs/design/save-slots-autosave.md §12): a system short of
// memory may end the process there, and the game would start again at the boot screens. At the
// next save point (the map standing idle, the intermission's menu) the game is saved apart
// from the autosaves, and a note says what to go back to; coming back removes the note. A note
// found at start-up is offered on the title.
std::atomic<bool> in_background{false};
std::atomic<bool> settled{true};
bool background_open{};           // game thread: in the background since the last frame
bool background_saved{},background_noted{};
unsigned background_frames{};
// Frames the game runs in the background for a save point before it is held (graphics.cpp).
constexpr unsigned background_grace=20;
std::optional<json> offered;      // the note found at start-up, until answered
std::atomic<bool> resume_wanted{false},declined{false};
std::filesystem::path auto_dir(){return save_store::library_directory()/"auto";}
std::filesystem::path note_path(){return auto_dir()/"resume.json";}
std::filesystem::path map_record(){return auto_dir()/"background-map.sus";}
std::filesystem::path intermission_record(){return auto_dir()/"background-inter.rec";}
// Only the main game's own play: not a custom campaign's library, not a mini stage.
bool resumable_play() {
    return save_store::enabled() && save_store::library_directory()==save_store::main_library()
        && !campaign::active() && !mini_stage::active();
}
void write_note(const std::filesystem::path& saved,const json& info) {
    json note=info;note.erase("rng");note.erase("record_sha256");   // beside the record, not here
    note["schema"]="srw64.resume.v1";note["record"]=saved.filename().string();
    note["kind"]=saved.extension()==".sus"?"map":"intermission";
    try{app::atomic_write(note_path(),note.dump(2)+"\n");background_noted=true;}
    catch(const std::exception& error){record("note-error",{{"error",error.what()}});return;}
    record("note",{{"record",note["record"]},{"kind",note["kind"]}});
}
void remove_note() {
    std::error_code error;
    if(std::filesystem::remove(note_path(),error))record("note-removed");
}

void record(const char* kind,json extra) {
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

void call3(uint8_t* ram,recomp_context* ctx,void(*function)(uint8_t*,recomp_context*),uint32_t a0,uint32_t a1=0,uint32_t a2=0) {
    auto c=*ctx;c.r29=int32_t(uint32_t(ctx->r29)-0x200);c.r4=int32_t(a0);c.r5=int32_t(a1);c.r6=int32_t(a2);function(ram,&c);
}
void remember(const std::filesystem::path& path){std::lock_guard lock(mutex);last_written=path;}
json sidecar(std::filesystem::path record_path) {
    record_path.replace_extension(".json");
    std::ifstream file(record_path);
    auto info=json::parse(file,nullptr,false);
    return info.is_object()?info:json::object();
}
// The intermission state in the slot format into `path`, as データセーブ writes it.
void write_intermission(uint8_t* ram,recomp_context* ctx,const std::filesystem::path& path) {
    save_store::Window target(0,path);
    call(ram,ctx,write_slot,0);
}
// The map in the suspend format into `path`; its random numbers and digest go into `info`.
void write_map(uint8_t* ram,recomp_context* ctx,const std::filesystem::path& path,json& info) {
    std::vector<uint8_t> numbers(rng_size);
    for(unsigned i=0;i<rng_size;++i)numbers[i]=uint8_t(read(ram,rng+i,1));
    info["rng"]=hex(numbers);
    {
        // Word aligned, so the raw bytes copy back as they were (see retry_snapshot).
        uint8_t* buffer=ram+(retry_snapshot-0x80000000u);
        const std::vector<uint8_t> retry(buffer,buffer+retry_snapshot_size);
        save_store::SuspendWindow target(path);
        call(ram,ctx,write_suspend,1);
        std::copy(retry.begin(),retry.end(),buffer);
    }
    info["record_sha256"]=digest(path);
}
// The background save at a save point (`map`: the map idle, else the intermission's menu),
// with its .json beside it as an autosave's, and the note pointing at it.
void save_background(uint8_t* ram,recomp_context* ctx,bool map) {
    background_saved=true;settled=true;
    if(!resumable_play())return;
    const auto path=map?map_record():intermission_record();
    auto info=about(ram,map?"background-map":"background-intermission");
    try {
        std::filesystem::create_directories(path.parent_path());
        if(map)write_map(ram,ctx,path,info);else write_intermission(ram,ctx,path);
        if(!app::present(path))throw std::runtime_error("no record written");
        auto beside=path;beside.replace_extension(".json");
        app::atomic_write(beside,info.dump(2)+"\n");
    } catch(const std::exception& error) {
        record("background-error",{{"error",error.what()}});return;
    }
    write_note(path,info);
    record("background",{{"map",map},{"title",info["title"]},{"map_turn",info["map_turn"]}});
}
// The intermission state in the slot format, as データセーブ writes it.
void intermission(uint8_t* ram,recomp_context* ctx,const char* node) {
    const auto choices=save_store::settings();
    if(!choices.autosave)return;
    auto info=about(ram,node);
    const bool saved=save_store::autosave(false,choices.intermission,info,[&](const std::filesystem::path& path) {
        write_intermission(ram,ctx,path);
        remember(path);
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
    else if(background_open && !background_saved)save_background(ram,ctx,false);
}
// The map in the suspend format, once a turn, on the first idle frame of the player's phase.
void map_idle(uint8_t* ram,recomp_context* ctx) {
    if(read(ram,map_state,1)!=5 || !script_inject::idle_reason(ram).empty())return;
    if(background_open && !background_saved){save_background(ram,ctx,true);return;}
    const auto choices=save_store::settings();
    if(!choices.autosave)return;
    const std::pair<unsigned,unsigned> key{read(ram,progress+9,1),read(ram,progress+2,2)};
    {
        std::lock_guard lock(mutex);
        if(last_turn==key)return;
        last_turn=key;
    }
    auto info=about(ram,"turn");
    const bool saved=save_store::autosave(true,choices.turn,info,[&](const std::filesystem::path& path) {
        write_map(ram,ctx,path,info);
        remember(path);
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

// The title overlay: its major and sub state, the frames idle to the attract demo and to the
// opening story (battle_viewer.cpp), the game mode.
constexpr uint32_t title_major=0x801CC3A6,title_sub=0x801CC3A7,title_idle=0x801CC390,title_story_idle=0x801CC3A4,game_mode=0x8015DA02;
bool title_mode(unsigned m){return m==7 || m==0x1D || m==0x1E;}
// Goes back to the noted save from the title's ring: the map through コンティニュー (major 6,
// as the ロード page loads a turn autosave), the intermission as the ロード page's はい does
// (801C7A48: the confirm sound, load mode 0x12 for the game's slot 1, the fade, sub 0xF).
void resume(uint8_t* ram,recomp_context* ctx,const json& note) {
    const auto path=auto_dir()/note.value("record",std::string());
    remove_note();
    if(!app::present(path)){record("resume-missing",{{"record",path.filename().string()}});return;}
    record("resume",{{"record",path.filename().string()}});
    if(path.extension()==".sus") {
        save_store::arm_suspend(path);
        prepare_turn_load(path);
        write8(ram,title_major,6);write8(ram,title_sub,0);
        return;
    }
    save_store::arm_load(path);
    call3(ram,ctx,resident_func_8007E8A8,0xB7);
    call3(ram,ctx,resident_func_80080188,0x12);
    call3(ram,ctx,resident_func_80099814,5,1,2);
    write8(ram,title_major,7);write8(ram,title_sub,0xF);
}
}

void configure(const std::filesystem::path& directory) {
    if(!save_store::enabled())return;
    srw64::open_run_log(log,directory/"autosave-events.jsonl");
    auto& h=srw64_game_hooks;
    h.intermission_enter=entered_intermission;h.intermission_after_step=intermission_step;
    h.turn_idle=map_idle;h.tactical_restored=restored;h.rng_seeded=seeded;
    // A note left by a game closed in the background: offered on the title.
    if(app::present(note_path())) {
        std::ifstream file(note_path());
        auto note=json::parse(file,nullptr,false);
        if(note.is_object() && app::present(auto_dir()/note.value("record",std::string()))) {
            std::lock_guard lock(mutex);offered=std::move(note);
            record("offer",{{"record",(*offered)["record"]}});
        } else remove_note();
    }
}
std::atomic<int64_t> background_since{0};
std::atomic<bool> trim_done{false};
void set_background(bool background) {
    if(background){settled=false;trim_done=false;background_since=std::chrono::steady_clock::now().time_since_epoch().count();}
    in_background=background;
}
bool trim_wanted(){return in_background.load() && settled.load();}
void trimmed(){trim_done=true;}
bool hold() {
    if(!in_background.load())return false;
#ifdef __ANDROID__
    if(settled.load() && trim_done.load())return true;
#else
    if(settled.load())return true;
#endif
    // A game that never reaches the frame hook (a long overlay load) is held all the same.
    const auto since=std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(background_since.load()));
    return std::chrono::steady_clock::now()-since>std::chrono::seconds(2);
}
void frame(uint8_t* ram,recomp_context* ctx) {
    if(!save_store::enabled())return;
    const bool background=in_background.load();
    if(background && !background_open) {
        // Until a save point comes, the note points at the newest autosave of this session.
        background_open=true;background_saved=false;background_noted=false;background_frames=0;
        std::optional<std::filesystem::path> fallback;
        {std::lock_guard lock(mutex);fallback=last_written;}
        if(resumable_play() && fallback && app::present(*fallback))write_note(*fallback,sidecar(*fallback));
        record("background-start",{{"fallback",fallback?fallback->filename().string():std::string()}});
    }
    if(background && !background_saved && ++background_frames>=background_grace)settled=true;
    if(!background && background_open) {
        background_open=false;settled=true;
        if(background_noted){remove_note();background_noted=false;}
        record("foreground",{{"saved",background_saved}});
    }
    // The offer on the title: no attract demo or opening story while it is asked.
    const unsigned mode=read(ram,game_mode,1);
    if(!title_mode(mode))return;
    std::optional<json> note;
    {std::lock_guard lock(mutex);if(offered)note=offered;}
    if(!note)return;
    if(declined.exchange(false)) {
        remove_note();
        std::lock_guard lock(mutex);offered.reset();record("declined");
        return;
    }
    write32(ram,title_idle,0);write16(ram,title_story_idle,0);
    if(read(ram,title_major,1)!=3 || !resume_wanted.exchange(false))return;
    {std::lock_guard lock(mutex);offered.reset();}
    resume(ram,ctx,*note);
}
std::optional<Offer> offer() {
    std::lock_guard lock(mutex);
    if(!offered)return std::nullopt;
    const auto& n=*offered;
    return Offer{n.value("kind",std::string())=="map",n.value("map_turn",0u)+1,n.value("funds",0u),n.value("time",std::string())};
}
void answer(bool go_back) {
    if(go_back)resume_wanted=true;else declined=true;
}
void prepare_turn_load(const std::filesystem::path& path) {
    std::lock_guard lock(mutex);
    loading=path;pending_rng.reset();
    record("load",{{"record",path.filename().string()}});
}
}
