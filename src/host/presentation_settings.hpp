#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <optional>
#include <string_view>
#include "input_bindings.hpp"
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
// Pre-battle confirmation: our redesigned page (default), the original layout
// redrawn as a native page ("hd"), or the original screen with original images.
// Read on the game thread when a confirmation opens; the choice is saved with the
// locale as battle_ui, where the older native/original values keep their meaning.
// SRW64_NATIVE_BATTLE_UI=0 forces the original for a run.
enum class BattleUi {Native,HD,Original};
BattleUi battle_ui();
void set_battle_ui(BattleUi ui);
// "native", "hd", "original"; a name this build does not know reads as native.
const char* battle_ui_name(BattleUi ui);
BattleUi battle_ui_from(std::string_view name);
// インターミッション screens (main menu, ユニット改造／武器改造, and the screens that
// follow): the native pages (default) or the original screens. Read on the
// game thread when a screen builds; saved as intermission_ui with the locale.
// SRW64_NATIVE_INTERMISSION=0 / SRW64_NATIVE_UPGRADE=0 force the original for a run.
bool native_intermission_ui();
void set_native_intermission_ui(bool native);
// Protagonist selection and name entry on a new game: the native pages (default)
// or the original screens. Read on the game thread when a page would open; saved as
// name_entry_ui. SRW64_NATIVE_NAME_ENTRY=0 forces the original for a run.
bool native_name_entry_ui();
void set_native_name_entry_ui(bool native);
// The title screen's ロード, オプション, サウンドセレクト and カラオケモード: the native
// pages (default) or the original screens. Read on the game thread when a screen is
// built; saved as title_ui.
bool native_title_ui();
void set_native_title_ui(bool native);
// The settings window's page last shown ("general", "interface", "rules", "controls",
// "about"; empty before the first), saved as settings_page so the window reopens there.
// Window thread only.
std::string settings_page();
void set_settings_page(const std::string& page);
// Controls (input_bindings.hpp): the SDL names input.json uses, and back. window_init loads
// SRW64_INPUT_SETTINGS into input::live_bindings() and saves each later change there.
std::string key_name(int scancode);
int key_from_name(const std::string& name);
std::string pad_input_name(const input::PadInput& input);
std::optional<input::PadInput> pad_input_from_name(const std::string& name);
}
