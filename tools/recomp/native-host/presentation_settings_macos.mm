#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "presentation_settings.hpp"
#include "native_dialogue.hpp"
#include "modal_input.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>

namespace srw64::settings {
namespace {
std::atomic_bool applying{},awaiting_release{};
ModalInputRelease release_gate;
uint64_t applying_request{};
std::string applying_locale,last_error;
std::filesystem::path destination,output;
NSWindow* game_window;
id key_monitor;
void persist(const std::filesystem::path& path,const std::string& locale) {
    const auto bytes=nlohmann::json({{"schema","srw64.presentation-settings.v1"},{"locale",locale}}).dump(2)+"\n";
    auto temporary=path.string()+".XXXXXX";
    const int fd=mkstemp(temporary.data());
    if(fd<0)throw std::runtime_error("Cannot create settings file");
    bool okay=true;size_t offset=0;
    while(offset<bytes.size()) {
        const auto n=write(fd,bytes.data()+offset,bytes.size()-offset);
        if(n<=0){okay=false;break;}offset+=size_t(n);
    }
    if(fsync(fd)!=0)okay=false;
    if(close(fd)!=0)okay=false;
    if(!okay || rename(temporary.c_str(),path.c_str())!=0) {
        unlink(temporary.c_str());throw std::runtime_error("Cannot publish settings file");
    }
    const int directory=::open(path.parent_path().c_str(),O_RDONLY);
    if(directory>=0){fsync(directory);close(directory);}
}
void toggle() {
    if(applying || release_gate.pending() || destination.empty())return;
    try {
        applying_locale=localization::next_locale(localization::catalog().locale);
        applying_request=dialogue::request_locale(applying_locale);
        release_gate.hold();awaiting_release=true;applying=true;last_error.clear();
    } catch(const std::exception& error) {last_error=error.what();}
}
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
    SDL_SysWMinfo info{};SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error("Cannot initialize language hotkey");
    game_window=info.info.cocoa.window;
    // One native event path for the Metal view and the embedded name editor.
    // Consume F7 before Cocoa/SDL can give it another meaning.
    key_monitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event) {
        if(event.window==game_window && event.keyCode==98 &&
           !(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagOption|NSEventModifierFlagControl|NSEventModifierFlagShift))) {
            // F7 belongs to the input method while it is composing text.
            if([game_window.firstResponder isKindOfClass:[NSTextView class]] &&
               [(NSTextView*)game_window.firstResponder hasMarkedText])return event;
            if(!event.isARepeat)toggle();return nil;
        }
        return event;
    }];
}
void control(SDL_Window*,const std::filesystem::path& directory) {
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;
    static uint64_t sequence{};
    std::ifstream file(directory/"language-control.json");
    const auto command=nlohmann::json::parse(file,nullptr,false);
    if(!command.is_object() || command.value("schema","")!="srw64.language-hotkey.v1" ||
       command.value("sequence",uint64_t{})<=sequence)return;
    sequence=command.at("sequence");
    [game_window makeKeyAndOrderFront:nil];
    const auto modifiers=command.value("command_modifier",false)?NSEventModifierFlagCommand:0;
    NSString* characters=[NSString stringWithFormat:@"%C",NSF7FunctionKey];
    for(auto type:{NSEventTypeKeyDown,NSEventTypeKeyUp}) {
        auto* event=[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:modifiers timestamp:0
            windowNumber:game_window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters
            isARepeat:command.value("repeat",false) keyCode:98];
        [NSApp sendEvent:event];
    }
    std::ofstream(directory/"language-key.json")<<nlohmann::json({{"schema","srw64.language-key.v1"},
        {"sequence",sequence},{"request",dialogue::locale_status().request},{"key","F7"},
        {"window_id",game_window.windowNumber},{"sheet_open",game_window.attachedSheet!=nil}}).dump(2)<<'\n';
}
void shutdown(){if(key_monitor)[NSEvent removeMonitor:key_monitor];key_monitor=nil;game_window=nil;}
}
