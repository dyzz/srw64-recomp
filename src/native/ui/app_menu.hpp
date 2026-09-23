#pragma once
#include <string>

namespace srw64::app_menu {
// Native menu entries only; the settings page stays in SDL/RmlUi. The second
// entry reads the dialogue text files again (F5).
#ifdef __APPLE__
void update(const std::string& settings_title,const std::string& reload_title);
bool available();
bool activate_settings();
bool take_settings_request();
bool take_reload_request();
void shutdown();
#else
inline void update(const std::string&,const std::string&) {}
inline bool available() { return false; }
inline bool activate_settings() { return false; }
inline bool take_settings_request() { return false; }
inline bool take_reload_request() { return false; }
inline void shutdown() {}
#endif
}
