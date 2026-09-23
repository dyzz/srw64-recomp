#include <SDL.h>
#include "presentation_settings.hpp"
#include "app/runtime.hpp"
#include "native_dialogue.hpp"
#include "modal_input.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace srw64::settings {
namespace {
std::atomic_bool applying{},awaiting_release{};
ModalInputRelease release_gate;
uint64_t applying_request{};
std::string applying_locale,last_error;
std::filesystem::path destination,output;
std::atomic_bool native_battle{true},native_intermission{true};
void persist(const std::filesystem::path& path,const std::string& locale) {
    srw64::app::atomic_write(path,nlohmann::json({{"schema","srw64.presentation-settings.v1"},{"locale",locale},
        {"battle_ui",native_battle?"native":"original"},{"intermission_ui",native_intermission?"native":"original"}}).dump(2)+"\n");
}
void apply(const std::string& locale) {
    if(applying || release_gate.pending() || destination.empty())return;
    try {
        applying_locale=locale;
        applying_request=dialogue::request_locale(applying_locale);
        release_gate.hold();awaiting_release=true;applying=true;last_error.clear();
    } catch(const std::exception& error) {last_error=error.what();}
}
}
void request_locale(const std::string& locale){apply(locale);}
bool native_battle_ui(){return native_battle.load();}
void set_native_battle_ui(bool native) {
    native_battle=native;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool native_intermission_ui(){return native_intermission.load();}
void set_native_intermission_ui(bool native) {
    native_intermission=native;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool owns_input(){return applying.load() || awaiting_release.load() || release_gate.pending();}
uint32_t filter_input(uint32_t input){return release_gate.filter(input,applying,awaiting_release);}
void release_input_when(bool all_keys_released){if(!applying && all_keys_released)awaiting_release=false;}
bool failed(){return !last_error.empty();} // Main-window thread only.
void update() {
    if(!applying)return;
    const auto status=dialogue::locale_status();if(status.completed<applying_request)return;
    last_error=status.error;
    if(last_error.empty())try {persist(destination,applying_locale);}catch(const std::exception& error){last_error=error.what();}
    std::ofstream(output/"settings-result.json")<<nlohmann::json({{"schema","srw64.settings-result.v1"},
        {"request",applying_request},{"locale",localization::catalog().locale},{"requested_locale",applying_locale},
        {"saved",last_error.empty()},{"error",last_error}}).dump(2)<<'\n';
    if(!last_error.empty())std::fprintf(stderr,"SRW64_LANGUAGE_ERROR %s\n",last_error.c_str());
    applying=false;
}
void window_init(SDL_Window* window,const std::filesystem::path& directory) {
    output=directory;
    if(const auto* path=std::getenv("SRW64_PRESENTATION_SETTINGS"))destination=path;
    if(!destination.empty()) {
        std::ifstream file(destination);
        const auto saved=nlohmann::json::parse(file,nullptr,false);
        if(saved.is_object()){native_battle=saved.value("battle_ui","native")!="original";native_intermission=saved.value("intermission_ui","native")!="original";}
    }
}

void control(SDL_Window*,const std::filesystem::path& directory) {
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;
    static uint64_t sequence{};
    std::ifstream file(directory/"language-control.json");
    const auto command=nlohmann::json::parse(file,nullptr,false);
    if(!command.is_object() || command.value("schema","")!="srw64.language-hotkey.v1" ||
       command.value("sequence",uint64_t{})<=sequence)return;
    sequence=command.at("sequence");
    SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_F7;
    event.key.keysym.mod=command.value("command_modifier",false)?KMOD_CTRL:KMOD_NONE;
    event.key.repeat=command.value("repeat",false);SDL_PushEvent(&event);
    event.type=SDL_KEYUP;SDL_PushEvent(&event);
    std::ofstream(directory/"language-key.json")<<nlohmann::json({{"schema","srw64.language-key.v1"},
        {"sequence",sequence},{"request",dialogue::locale_status().request},{"key","F7"},{"backend","SDL"}}).dump(2)<<'\n';
}

void shutdown(){}
}
