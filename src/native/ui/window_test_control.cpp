#include <SDL.h>
#include "window_test_control.hpp"
#include "presentation/image_mode.hpp"
#include "json/json.hpp"
#include <cstdlib>
#include <fstream>

namespace srw64::qa {
void update_window(SDL_Window* window,const std::filesystem::path& output,uint64_t vi) {
    if(!std::getenv("SRW64_WINDOW_CONTROL"))return;
    static uint64_t close_sequence{},resize_sequence{},image_sequence{};
    {
        std::ifstream request(output/"window-close.txt");std::string magic;uint64_t sequence{},at_vi{};
        if(request>>magic>>sequence>>at_vi && magic=="SRWX1" && sequence>close_sequence && vi>=at_vi){
            close_sequence=sequence;SDL_Event event{};event.type=SDL_WINDOWEVENT;
            event.window.windowID=SDL_GetWindowID(window);event.window.event=SDL_WINDOWEVENT_CLOSE;SDL_PushEvent(&event);
            std::ofstream(output/"window-close-events.jsonl",std::ios::app)<<nlohmann::json({
                {"schema","srw64.window-close.v1"},{"sequence",sequence},{"vi",vi},
                {"window_id",SDL_GetWindowID(window)},{"action","SDL_WINDOWEVENT_CLOSE"}}).dump()<<'\n';
        }
    }
    {
        std::ifstream request(output/"window-control.txt");std::string magic;uint64_t sequence{};int width{},height{};
        if(request>>magic>>sequence>>width>>height && magic=="SRWW1" && sequence>resize_sequence && width>=640 && width<=2560 && height>=480 && height<=1600){
            resize_sequence=sequence;SDL_SetWindowSize(window,width,height);
        }
    }
    {
        std::ifstream request(output/"image-control.txt");std::string magic,mode;uint64_t sequence{};
        if(request>>magic>>sequence>>mode && magic=="SRWI1" && sequence>image_sequence && (mode=="original" || mode=="hd")){
            image_sequence=sequence;presentation::image_mode.request(mode=="hd");
        }
    }
}
}
