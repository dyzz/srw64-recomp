#pragma once
#include <filesystem>

// Settings window opened from 选项 → 设置…: the same rule switches as the menu,
// plus language and image mode, each applied immediately. Implemented for macOS
// in settings_window_macos.mm; hosts without a window never call it.
namespace srw64::settings_window {
void open();
void close();
bool visible();
// Window thread, every frame: keeps titles and values in step with the game.
void update();
void shutdown();
// While the window has focus the game must not see keystrokes, and it must not
// see the keys still held when it closes.
bool owns_input();
uint16_t filter_input(uint16_t buttons, bool physical_keys_held);
// QA only (SRW64_WINDOW_CONTROL=1): drive the window from settings-control.json.
void control(const std::filesystem::path& directory);
}
