#pragma once
#include <filesystem>

// Shared SDL/RmlUi settings, opened from the application menu or Ctrl/Cmd+comma.
// Rules, language and image mode apply through the existing game interfaces.
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
