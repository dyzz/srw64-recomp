#pragma once
#include "json/json.hpp"
#include <string>

// Short native banners at the top of the game window, for events the game itself
// does not announce (an upgrade refund). Any thread may post; the window thread
// shows each one for a few seconds. The banners are ordinary AppKit views, so the
// debug interface's ui.tree lists them and its screenshots include them.
namespace srw64::notices {
void window_init(void* cocoa_window);
void post(const std::string& kind,const std::string& text);
// Window thread, every frame: shows posted banners, fades and removes old ones,
// and follows the window size.
void update();
// The latest notices, newest last, for the debug status.
nlohmann::json recent();
}
