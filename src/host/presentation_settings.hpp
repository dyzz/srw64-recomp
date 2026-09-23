#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
struct SDL_Window;
namespace srw64::settings {
void window_init(SDL_Window* window,const std::filesystem::path& output);
bool owns_input();
void release_input_when(bool all_keys_released);
void update();
bool failed();
uint32_t filter_input(uint32_t input);
void shutdown();
void control(SDL_Window*,const std::filesystem::path&);
// Apply and remember one registered locale (the settings window; F7 cycles).
void request_locale(const std::string& locale);
// Pre-battle confirmation: the native page (default) or the original HUD.
// Read on the game thread when a confirmation opens; the choice is saved with
// the locale. SRW64_NATIVE_BATTLE_UI=0 forces the original for a run.
bool native_battle_ui();
void set_native_battle_ui(bool native);
// インターミッション screens (main menu, ユニット改造／武器改造, and the screens that
// follow): the native pages (default) or the original screens. Read on the
// game thread when a screen builds; saved as intermission_ui with the locale.
// SRW64_NATIVE_INTERMISSION=0 / SRW64_NATIVE_UPGRADE=0 force the original for a run.
bool native_intermission_ui();
void set_native_intermission_ui(bool native);
}
