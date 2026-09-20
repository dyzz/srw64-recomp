#pragma once
#include <string>

namespace srw64::app_menu {
// Native menu entry only; the settings page stays in SDL/RmlUi.
#ifdef __APPLE__
void update(const std::string& title);
bool available();
bool activate_settings();
bool take_settings_request();
void shutdown();
#else
inline void update(const std::string&) {}
inline bool available() { return false; }
inline bool activate_settings() { return false; }
inline bool take_settings_request() { return false; }
inline void shutdown() {}
#endif
}
