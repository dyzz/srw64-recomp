#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct SDL_Window;

namespace srw64::app_menu {
// Native menu entries only; the pages themselves stay in SDL/RmlUi. The application menu
// (macOS) or the window's menu bar (Windows, macos/app_menu.mm and windows/app_menu_windows.cpp)
// opens the settings and reads the dialogue text files again (F5); its About entry opens
// the settings window's About page, and Check for Updates that page with a check started
// (update_check.hpp); Windows has those two in Help instead. After it come the menus
// frontend.cpp describes (View, Tools, Language, Help): each entry is an id the frontend
// takes back when it is chosen, shown with the key that does the same.
struct Labels {
    std::string settings, reload;
    std::string about, check_updates;
    std::string game, exit;  // Windows: the first menu's title and its Exit entry
};
constexpr int kScales = 4;  // View's window sizes, 1x to 4x the original's 240 lines
// macOS key equivalents' modifiers (mac_mods).
enum Mods : unsigned { Command = 1, Control = 2 };
struct Item {
    std::string id;        // empty: a separator
    std::string title;
    std::string shortcut;  // the key shown beside the title ("F12", the bound "Tab")
    // macOS: a key equivalent in place of the shown key ("F6", "f", "1"), only for a key
    // the menu may take before the game sees it: fixed keys, never bound ones.
    std::string mac_key;
    unsigned mac_mods = 0;
    bool checked = false, enabled = true;
    std::vector<Item> items;  // a submenu
};
struct Menu {
    std::string title;
    std::vector<Item> items;
    // Shown in the bar before the title: "globe".
    std::string icon;
    bool help = false;  // the Help menu: macOS puts it last, after Window
};
#ifdef _WIN32
// Puts the menu bar on the window, before the renderer starts (frontend window_init).
void attach(SDL_Window* window);
#else
inline void attach(SDL_Window*) {}
#endif
#if defined(__APPLE__) || defined(_WIN32)
// Window thread, every frame; the menus are built again only when their entries change.
// fullscreen: Windows shows the bar there only while the mouse is at the screen's top.
void update(const Labels& labels, const std::vector<Menu>& menus, bool fullscreen);
bool available();
bool activate_settings();
// Presses the entry of this title in the menus update() describes, as a player would
// (debug interface).
bool activate(const std::string& title);
bool take_settings_request();
bool take_reload_request();
bool take_about_request();
bool take_update_request();
std::string take_request();  // an entry chosen since, oldest first; empty for none
// Shows the file selected in its folder: Finder, or Explorer. False when it could not.
bool reveal(const std::filesystem::path& file);
void shutdown();
#else
inline void update(const Labels&, const std::vector<Menu>&, bool) {}
inline bool available() { return false; }
inline bool activate_settings() { return false; }
inline bool activate(const std::string&) { return false; }
inline bool take_settings_request() { return false; }
inline bool take_reload_request() { return false; }
inline bool take_about_request() { return false; }
inline bool take_update_request() { return false; }
inline std::string take_request() { return {}; }
inline bool reveal(const std::filesystem::path&) { return false; }
inline void shutdown() {}
#endif
}
