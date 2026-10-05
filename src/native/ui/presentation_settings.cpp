#include <SDL.h>
#include "presentation_settings.hpp"
#include "steam_deck.hpp"
#include "game_frame.hpp"
#include "app/runtime.hpp"
#include "native_dialogue.hpp"
#include "modal_input.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include "input_bindings.hpp"
#include "cheats.hpp"
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
std::string applying_locale,last_error,page;
std::filesystem::path destination,output,input_destination;
// input.json (SRW64_INPUT_SETTINGS): the actions whose bindings differ from the defaults,
// each device's inputs by SDL name ("Z", "Return"; "a", "leftshoulder", "righty-").
void save_bindings(const input::Bindings& bindings) {
    if(input_destination.empty())return;
    const auto defaults=input::default_bindings();
    nlohmann::json keyboard=nlohmann::json::object(),controller=nlohmann::json::object();
    for(size_t i=0;i<input::action_count;++i) {
        const auto id=std::string(input::actions[i].id);
        if(bindings.keys[i]!=defaults.keys[i]){auto& row=keyboard[id]=nlohmann::json::array();for(int key:bindings.keys[i])row.push_back(key_name(key));}
        if(bindings.pads[i]!=defaults.pads[i]){auto& row=controller[id]=nlohmann::json::array();for(const auto& p:bindings.pads[i])row.push_back(pad_input_name(p));}
    }
    try{srw64::app::atomic_write(input_destination,nlohmann::json({{"schema","srw64.input-bindings.v1"},{"keyboard",keyboard},{"controller",controller}}).dump(2)+"\n");}
    catch(const std::exception& error){last_error=error.what();}
}
input::Bindings load_bindings(const nlohmann::json& saved) {
    auto bindings=input::default_bindings();
    if(!saved.is_object() || saved.value("schema","")!="srw64.input-bindings.v1")return bindings;
    for(size_t i=0;i<input::action_count;++i) {
        const auto id=std::string(input::actions[i].id);
        // Names this build does not know are dropped; an action keeps an empty list if the player cleared it.
        if(saved.contains("keyboard") && saved["keyboard"].contains(id) && saved["keyboard"][id].is_array()) {
            bindings.keys[i].clear();
            for(const auto& name:saved["keyboard"][id])if(name.is_string())if(const int key=key_from_name(name.get<std::string>());key>0)bindings.keys[i].push_back(key);
        }
        if(saved.contains("controller") && saved["controller"].contains(id) && saved["controller"][id].is_array()) {
            bindings.pads[i].clear();
            for(const auto& name:saved["controller"][id])if(name.is_string())if(const auto p=pad_input_from_name(name.get<std::string>()))bindings.pads[i].push_back(*p);
        }
    }
    return bindings;
}
std::atomic<BattleUi> battle{BattleUi::Native};
#ifdef __ANDROID__
// Android is being tuned on the phone (docs/design/android-port.md): the readout starts on.
constexpr bool fps_default=true;
#else
constexpr bool fps_default=false;
#endif
std::atomic_bool native_intermission{true},native_name_entry{true},native_title{true},fps_shown{fps_default};
std::atomic<int> size_choice{-1};  // UiSize, or -1 until the player chooses
void persist(const std::filesystem::path& path,const std::string& locale) {
    auto saved=nlohmann::json({{"schema","srw64.presentation-settings.v1"},{"locale",locale},
        {"battle_ui",battle_ui_name(battle)},{"intermission_ui",native_intermission?"native":"original"},{"name_entry_ui",native_name_entry?"native":"original"},
        {"title_ui",native_title?"native":"original"},{"settings_page",page},{"aspect",frame::wide?"auto":"4:3"},{"show_fps",fps_shown.load()}});
    if(size_choice>=0)saved["ui_size"]=ui_size_name(UiSize(size_choice.load()));
    auto cheat_ids=nlohmann::json::array();
    for(const auto& entry:srw64::cheats::catalog)if(srw64::cheats::active()&entry.bit)cheat_ids.push_back(entry.id);
    if(!cheat_ids.empty())saved["cheats"]=cheat_ids;
    srw64::app::atomic_write(path,saved.dump(2)+"\n");
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
BattleUi battle_ui(){return battle.load();}
const char* battle_ui_name(BattleUi ui){return ui==BattleUi::HD?"hd":ui==BattleUi::Original?"original":"native";}
BattleUi battle_ui_from(std::string_view name){return name=="hd"?BattleUi::HD:name=="original"?BattleUi::Original:BattleUi::Native;}
void set_battle_ui(BattleUi ui) {
    battle=ui;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
UiSize ui_size() {
    const int choice=size_choice.load();
#ifdef __ANDROID__
    // A phone or handheld screen, like the Deck's (docs/design/android-port.md).
    return choice>=0?UiSize(choice):UiSize::Largest;
#else
    return choice>=0?UiSize(choice):on_steam_deck()?UiSize::Largest:UiSize::Standard;
#endif
}
const char* ui_size_name(UiSize size){return size==UiSize::Largest?"largest":size==UiSize::Large?"large":"standard";}
float ui_scale(UiSize size){return size==UiSize::Largest?1.5f:size==UiSize::Large?1.25f:1.f;}
void set_ui_size(UiSize size) {
    size_choice=int(size);
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool wide_picture(){return frame::wide.load();}
void set_wide_picture(bool wide) {
    frame::wide=wide;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool show_fps(){return fps_shown.load();}
void set_show_fps(bool show) {
    fps_shown=show;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
unsigned cheats(){return srw64::cheats::active();}
void set_cheats(unsigned switches) {
    srw64::cheats::set_active(switches);
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool native_intermission_ui(){return native_intermission.load();}
void set_native_intermission_ui(bool native) {
    native_intermission=native;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool native_name_entry_ui(){return native_name_entry.load();}
void set_native_name_entry_ui(bool native) {
    native_name_entry=native;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
bool native_title_ui(){return native_title.load();}
void set_native_title_ui(bool native) {
    native_title=native;
    if(destination.empty())return;
    try {persist(destination,localization::catalog().locale);}catch(const std::exception& error){last_error=error.what();}
}
std::string settings_page(){return page;}
void set_settings_page(const std::string& value) {
    page=value;
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
        if(saved.is_object()){battle=battle_ui_from(saved.value("battle_ui","native"));native_intermission=saved.value("intermission_ui","native")!="original";native_name_entry=saved.value("name_entry_ui","native")!="original";native_title=saved.value("title_ui","native")!="original";}
        if(saved.is_object() && saved.contains("settings_page") && saved["settings_page"].is_string())page=saved["settings_page"].get<std::string>();
        if(saved.is_object() && saved.contains("aspect") && saved["aspect"].is_string())frame::wide=saved["aspect"].get<std::string>()!="4:3";
        if(saved.is_object() && saved.contains("show_fps") && saved["show_fps"].is_boolean())fps_shown=saved["show_fps"].get<bool>();
        if(saved.is_object() && saved.contains("cheats") && saved["cheats"].is_array() && !std::getenv("SRW64_CHEATS")) {
            unsigned value=0;
            for(const auto& id:saved["cheats"])for(const auto& entry:srw64::cheats::catalog)if(id.is_string() && id.get<std::string>()==entry.id)value|=entry.bit;
            srw64::cheats::set_active(value);
        }
        if(saved.is_object() && saved.contains("ui_size") && saved["ui_size"].is_string()) {
            const auto name=saved["ui_size"].get<std::string>();
            for(const auto size:{UiSize::Standard,UiSize::Large,UiSize::Largest})if(name==ui_size_name(size))size_choice=int(size);
        }
    }
    // For comparisons: SRW64_ASPECT=4:3 or auto for a run, saved only if changed.
    if(const auto* aspect=std::getenv("SRW64_ASPECT"))frame::wide=std::string(aspect)!="4:3";
    if(const auto* path=std::getenv("SRW64_INPUT_SETTINGS"))input_destination=path;
    if(!input_destination.empty() && std::filesystem::exists(input_destination)) {
        std::ifstream file(input_destination);
        input::live_bindings().set(load_bindings(nlohmann::json::parse(file,nullptr,false)),false);
    }
    input::live_bindings().set_saver(save_bindings);
}
std::string key_name(int key){return SDL_GetScancodeName(SDL_Scancode(key));}
std::string key_display_name(int key) {
    const std::string name=SDL_GetKeyName(SDL_GetKeyFromScancode(SDL_Scancode(key)));
    return name.empty()?key_name(key):name;
}
int key_from_name(const std::string& name){return int(SDL_GetScancodeFromName(name.c_str()));}
std::string pad_input_name(const input::PadInput& p) {
    if(p.kind==input::PadInput::Button){const char* name=SDL_GameControllerGetStringForButton(SDL_GameControllerButton(p.index));return name?name:"";}
    const char* name=SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(p.index));
    return std::string(name?name:"")+(p.kind==input::PadInput::AxisMinus?"-":"+");
}
std::optional<input::PadInput> pad_input_from_name(const std::string& name) {
    if(name.empty())return std::nullopt;
    if(name.back()=='+' || name.back()=='-') {
        const auto axis=SDL_GameControllerGetAxisFromString(name.substr(0,name.size()-1).c_str());
        if(axis==SDL_CONTROLLER_AXIS_INVALID)return std::nullopt;
        return input::axis(uint8_t(axis),name.back()=='-'?-1:1);
    }
    const auto button=SDL_GameControllerGetButtonFromString(name.c_str());
    if(button==SDL_CONTROLLER_BUTTON_INVALID)return std::nullopt;
    return input::button(uint8_t(button));
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
