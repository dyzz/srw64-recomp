#pragma once
#include <string>

namespace srw64::app_menu {
// Native menu entries only; the settings page stays in SDL/RmlUi. The application menu
// opens the settings and reads the dialogue text files again (F5); its About entry opens
// the settings window's About page, and Check for Updates that page with a check started
// (update_check.hpp). A View menu toggles full screen and sets the window to a whole
// multiple of the original's 240 lines.
struct Labels {
    std::string settings, reload;
    std::string view, fullscreen, window_scale;  // window_scale: "{n}" becomes 1..4
    std::string about, check_updates;
};
// What the View menu shows: full screen checked; the scale whose size the window has
// checked (0 for none); scales up to `largest` fit the screen, none while full screen.
struct WindowState {
    bool fullscreen = false;
    int scale = 0, largest = 0;
};
constexpr int kScales = 4;
#ifdef __APPLE__
void update(const Labels& labels, const WindowState& state);
bool available();
bool activate_settings();
// Presses the entry of this title in the View menu as a player would (debug interface).
bool activate(const std::string& title);
bool take_settings_request();
bool take_reload_request();
bool take_fullscreen_request();
int take_scale_request();  // 0 when none
bool take_about_request();
bool take_update_request();
void shutdown();
#else
inline void update(const Labels&, const WindowState&) {}
inline bool available() { return false; }
inline bool activate_settings() { return false; }
inline bool activate(const std::string&) { return false; }
inline bool take_settings_request() { return false; }
inline bool take_reload_request() { return false; }
inline bool take_fullscreen_request() { return false; }
inline int take_scale_request() { return 0; }
inline bool take_about_request() { return false; }
inline bool take_update_request() { return false; }
inline void shutdown() {}
#endif
}
