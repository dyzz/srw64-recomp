#include "link_page.hpp"
#include "link_battler.hpp"
#include "game_hooks.hpp"
#include "presentation_settings.hpp"
#include "funcs.h"
#include <atomic>
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::link_page {
namespace {
// The intermission's next screen (0 the main menu); 80099814(5, 1, 2) fades to it.
constexpr uint32_t next_screen=0x801DECB8;
std::mutex mutex;
Request current;
std::array<std::vector<std::string>,3> portraits;
uint64_t serial{};
bool waiting{},answered{},confirmed{},closed{};
unsigned chosen{};
std::atomic_bool owning{},window_owning{};
uint16_t held{};
std::ofstream log;

unsigned bits(const std::array<bool,3>& flags) {
    unsigned value=0;
    for(unsigned n=0;n<flags.size();++n)if(flags[n])value|=1u<<n;
    return value;
}
void record(const char* kind,const uint8_t* ram,nlohmann::json extra=nlohmann::json::object()) {
    if(!log.is_open())return;
    nlohmann::json row={{"schema","srw64.link-event.v1"},{"kind",kind},{"vi",srw64_current_vi()},{"serial",current.serial},
        {"joined",bits(current.joined)},{"scheduled",bits(current.scheduled)},
        {"next_scene",link::read(ram,link::next_scene,1)},{"story_scene",link::read(ram,link::story_scene,1)}};
    row.update(extra);
    log<<row.dump()<<'\n';log.flush();
}

bool begin(uint8_t* ram) {
    // 場間画面 set to the original: the original リンク screen with an empty block.
    if(!settings::native_intermission_ui())return false;
    std::lock_guard lock(mutex);
    const auto status=link::status(ram);
    current={++serial,true,status.joined,status.scheduled,portraits};
    waiting=true;answered=confirmed=closed=false;chosen=0;owning=true;
    record("open",ram);
    return true;
}
bool step(uint8_t* ram,recomp_context* ctx) {
    std::unique_lock lock(mutex);
    if(closed)return true;   // went back: the fade to the menu is running
    if(!waiting)return false;
    if(!answered)return true;
    waiting=false;current.visible=false;owning=false;
    if(confirmed) {
        const unsigned selection=chosen&~bits(current.joined);
        link::prepare(ram,selection);
        record("confirmed",ram,{{"selection",selection}});
        lock.unlock();
        // The set-up the screen skipped: it reads the block, lets 801D9F80 move the
        // next scene and lists the pilots for the level exchange.
        auto call=*ctx;srw64_original_link_open(ram,&call);
        lock.lock();
        record("linked",ram,{{"selection",selection}});
        return true;
    }
    // As B on the リンク screen (801D7148): cancel sound, the menu next, fade.
    closed=true;
    auto call=*ctx;call.r4=0xB8;resident_func_8007E8A8(ram,&call);
    guest::write32(ram,next_screen,0);
    call=*ctx;call.r4=5;call.r5=1;call.r6=2;resident_func_80099814(ram,&call);
    record("back",ram);
    return true;
}
}

void configure(const std::filesystem::path& directory) {
    if(std::getenv("SRW64_NATIVE_LINK") && std::string(std::getenv("SRW64_NATIVE_LINK"))=="0")return;
    // Pilot portraits come with the name page's assets in the native dialogue data.
    if(const char* path=std::getenv("SRW64_DIALOGUE_DATA")) {
        std::ifstream source(path);
        const auto data=nlohmann::json::parse(source,nullptr,false);
        if(!data.is_discarded() && data.contains("name_entry_assets")) {
            const auto& art=data.at("name_entry_assets");
            for(unsigned n=0;n<portraits.size() && n<art.value("link_faces",nlohmann::json::array()).size();++n)
                for(const auto& face:art.at("link_faces").at(n)) {
                    const auto key=std::to_string(face.get<unsigned>());
                    if(art.at("portraits").contains(key))portraits[n].push_back(art.at("portraits").at(key).at("original").get<std::string>());
                }
        }
    }
    log.open(directory/"link-events.jsonl");
    srw64_game_hooks.link_begin=begin;srw64_game_hooks.link_step=step;
}
Request request() {std::lock_guard lock(mutex);return current;}
void answer(uint64_t id,unsigned selection,bool confirm) {
    std::lock_guard lock(mutex);
    if(!current.visible || id!=current.serial || answered)return;
    answered=true;confirmed=confirm;chosen=selection&7;
}
bool owns_input() {return owning || window_owning;}
void window_claim_input(bool value) {window_owning=value;}
uint16_t input(uint16_t buttons) {held&=buttons;if(owns_input())held|=buttons;return buttons&~held;}
nlohmann::json state() {
    const auto value=request();
    return {{"visible",value.visible},{"serial",value.serial},{"joined",value.joined},{"scheduled",value.scheduled}};
}
}
