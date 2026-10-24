#include "quick_load.hpp"
#include "battle_page.hpp"
#include "guest_memory.hpp"
#include "notices.hpp"
#include "save_store.hpp"
#include "sched_rate.hpp"
#include "localization/catalog.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

uint64_t srw64_current_vi();
namespace srw64::quick_load {
namespace {
using guest::read;
using json=nlohmann::json;
// The game's mode (80080188 writes it: 1 the logo and title, 2 the battle animation, 3 /
// 0xB / 0x11 / 0x16 / 0x22 the tactical map), the map's UI state (0 while it has none up:
// 801DFBD0 tests the combination only otherwise) and the progress block (+2 turn, +9 title).
constexpr uint32_t mode=0x8015DA02,map_state=0x80172EB0,progress=0x8010F5E8;
// The battle animation (battle_animation_probe.hpp): its state, its own fade request (-1
// none) and the abort flag. Its actors are built from state 2, and from 21 it winds down.
constexpr uint32_t battle_state=0x80250000,battle_fade=0x80250205,battle_abort=0x80225810;
// The suspend area on the card and the progress block in its record (save_page.cpp).
constexpr uint32_t suspend_offset=0x3E10,record_progress=0x26C;
constexpr uint32_t button_z=0x2000,button_start=0x1000;
// The intermission's exit code (D_801DD540: 1 the reset pair, 2 次のマップへ; intermission_page.cpp).
constexpr uint32_t intermission_exit=0x801DD540;
constexpr uint64_t limit=1200;   // VIs: a fade and a restore take well under one second

enum class Stage : uint8_t {idle,press_z,press_both,hold_start,loading,release};
enum class Kind : uint8_t {load,title};
std::mutex mutex;
std::atomic<Stage> stage{Stage::idle};
std::atomic_bool asked{},can{},asked_title{},can_title{};
std::atomic<Kind> kind{Kind::load};
std::atomic<unsigned> title_now{0xFFFF};
uint64_t since{};
unsigned frames{};
std::ofstream log;
std::string last;

void record(const char* kind,json extra=json::object()) {
    if(!log.is_open())return;
    json row={{"schema","srw64.quick-load-event.v1"},{"kind",kind},{"vi",srw64_current_vi()}};
    row.update(extra);log<<row.dump()<<'\n';log.flush();
}
void notice(const std::string& key,unsigned turn=0) {
    auto text=localization::catalog().ui(key);
    if(const auto at=text.find("{n}");at!=std::string::npos)text.replace(at,3,std::to_string(turn));
    last=key;
    notices::post("quick-load",text);
}
bool tactical(unsigned m){return m==3 || m==0xB || m==0x11 || m==0x16 || m==0x22;}
// Where the original tests Z + START for the reset (original-controls.md §2): the world
// map (801C28E4), the intermission's screens (801D8D20), the sell screen (801C3788); the
// tactical map and the battle animation as for the load.
// The intermission runs as mode 4 after a clear and as 0x12-0x15 after a load from the title.
bool title_screen(unsigned m){return m==4 || (m>=0x12 && m<=0x15) || m==12 || m==13 || m==0x21;}
bool usable_in(const uint8_t* ram) {
    const unsigned m=read(ram,mode,1);
    if(tactical(m))return read(ram,map_state,1)!=0;
    if(m!=2)return false;
    const int state=int8_t(read(ram,battle_state,1));
    return state>=2 && state<=20 && int8_t(read(ram,battle_fade,1))==-1 && read(ram,battle_abort,1)==0;
}
// The suspend record's first bytes: the library's card, or the runtime's save file.
std::vector<uint8_t> suspend_head() {
    constexpr uint32_t length=record_progress+0x10;
    if(save_store::enabled())return save_store::cartridge_bytes(suspend_offset,length);
    std::vector<uint8_t> bytes(length);
    std::ifstream file(ultramodern::get_save_file_path(),std::ios::binary);
    if(!file.seekg(suspend_offset) || !file.read(reinterpret_cast<char*>(bytes.data()),length))return {};
    return bytes;
}
void set(Stage next){stage=next;frames=0;}
bool usable_title_in(const uint8_t* ram){return title_screen(read(ram,mode,1)) || usable_in(ram);}
}

void configure(const std::filesystem::path& run_directory) {
    std::lock_guard lock(mutex);
    log.open(run_directory/"quick-load-events.jsonl");
}
bool usable(){return can.load();}
bool usable_title(){return can_title.load();}
Saved saved() {
    const auto bytes=suspend_head();
    Saved s;
    if(bytes.size()<record_progress+0x10 || !(bytes[0]&0x80))return s;
    const auto u16=[&](size_t i){return unsigned(bytes[i]<<8|bytes[i+1]);};
    const size_t at=record_progress;
    s.exists=true;
    s.this_stage=bytes[at+9]==title_now.load();
    s.turn=u16(at+2)+1;
    s.funds=uint32_t(u16(at+0xC))<<16|u16(at+0xE);
    return s;
}
void request(){asked=true;}
void request_title(){asked_title=true;}

void frame(uint8_t* ram) {
    std::lock_guard lock(mutex);
    const unsigned m=read(ram,mode,1);
    const bool on_map=tactical(m) || m==2;
    if(on_map && m!=2)title_now=read(ram,progress+9,1);
    can=stage.load()==Stage::idle && usable_in(ram);
    can_title=stage.load()==Stage::idle && usable_title_in(ram);
    const uint64_t vi=srw64_current_vi();
    if(asked.exchange(false) && stage.load()==Stage::idle) {
        const auto s=saved();
        if(!usable_in(ram)){notice("quick_load_unavailable");record("refused",{{"mode",m}});}
        else if(!s.exists){notice("quick_load_none");record("refused",{{"why","none"}});}
        else if(!s.this_stage){notice("quick_load_other_stage");record("refused",{{"why","stage"}});}
        else{since=vi;kind=Kind::load;set(Stage::press_z);record("asked",{{"mode",m},{"turn",s.turn}});}
        return;
    }
    if(asked_title.exchange(false) && stage.load()==Stage::idle) {
        if(!usable_title_in(ram)){notice("title_return_unavailable");record("refused",{{"kind","title"},{"mode",m}});}
        else{since=vi;kind=Kind::title;set(Stage::press_z);record("asked",{{"kind","title"},{"mode",m}});}
        return;
    }
    ++frames;   // frame boundaries: at 120 Hz (sched_rate.hpp) two to a 60 Hz VI
    switch(stage.load()) {
    case Stage::idle: return;
    // Z alone for a frame, then START with it: 80085EF8 wants START's press edge with Z held.
    case Stage::press_z: if(frames>=2*sched_rate::factor())set(Stage::press_both); break;
    // Taken: the map asks for mode 1 and fades (801DFE0C); the battle animation sets its
    // abort flag and fades (801C9A18), and only while no fade runs.
    case Stage::press_both:
        // The intermission keeps its mode through the fade: its dispatcher (801D8D20) writes
        // exit code 1 for the pair and only resets at the fade's end, where a START still
        // held would load the suspend save instead; so its exit code is the taking there.
        if(m==1 || (m==2 && read(ram,battle_abort,1)==1) || (kind.load()==Kind::title && title_screen(m) && read(ram,intermission_exit,1)==1)) {
            // The title: everything let go now, so the fade ends at the logo (mode 1) and
            // not at a load; the game needs nothing more from the host.
            set(kind.load()==Kind::title?Stage::release:Stage::hold_start);record("taken",{{"mode",m},{"kind",kind.load()==Kind::title?"title":"load"}});
            // The map is leaving: a pre-battle page over it goes now, not with a step that no
            // longer runs (battle_page.cpp has its own nets as well; 0.4.9 left it up).
            battle_page::abandon(kind.load()==Kind::title?"title-return":"quick-load");
        }
        else if(frames>=8*sched_rate::factor()) {   // not taken (a fade, a busy frame): again
            record("retry",{{"mode",m},{"held",read(ram,0x8015D9FA,2)},{"pressed",read(ram,0x8015CAF0,2)},{"battle_state",int8_t(read(ram,battle_state,1))}});
            set(Stage::press_z);
        }
        break;
    // START held until the fade ends: then 0x11 when the card has the record, else the title (7).
    case Stage::hold_start:
        if(m==0x11){set(Stage::loading);record("loading");}
        else if(m==7){set(Stage::idle);notice("quick_load_failed");record("failed",{{"mode",m}});}
        break;
    // Back on the map at the record's state, its UI up again.
    // The title: the combination let go for a few frames, then the player's input again.
    case Stage::release:
        if(frames>=10*sched_rate::factor()){set(Stage::idle);record("released",{{"mode",m}});}
        break;
    case Stage::loading:
        if(tactical(m) && read(ram,map_state,1)!=0) {
            const unsigned turn=read(ram,progress+2,2)+1;
            set(Stage::idle);notice("quick_load_done",turn);record("loaded",{{"turn",turn}});
        }
        break;
    }
    if(stage.load()!=Stage::idle && vi-since>limit) {
        record("timeout",{{"mode",m},{"abort",read(ram,battle_abort,1)},{"battle_state",int8_t(read(ram,battle_state,1))}});
        set(Stage::idle);notice(kind.load()==Kind::title?"title_return_failed":"quick_load_failed");
    }
}
uint32_t filter_input(uint32_t buttons) {
    switch(stage.load()) {
    case Stage::press_z: return button_z;
    case Stage::press_both: return button_z|button_start;
    case Stage::hold_start: return button_start;
    case Stage::loading: return 0;
    case Stage::release: return 0;
    default: return buttons;
    }
}
json state() {
    std::lock_guard lock(mutex);
    static constexpr const char* names[]={"idle","press_z","press_both","hold_start","loading","release"};
    const auto s=saved();
    return {{"stage",names[size_t(stage.load())]},{"usable",can.load()},{"usable_title",can_title.load()},
            {"kind",kind.load()==Kind::title?"title":"load"},{"last",last},
            {"saved",{{"exists",s.exists},{"this_stage",s.this_stage},{"turn",s.turn},{"funds",s.funds}}}};
}
}
