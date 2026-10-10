#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <optional>
#include <string_view>
#include <vector>
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
// The game picture: following the screen's shape from 4:3 to 16:9 (the default; the
// original centred with room on each side) or the original 4:3 (game_frame.hpp). Saved
// as aspect, "auto" or "4:3"; applies from the next frame.
bool wide_picture();
void set_wide_picture(bool wide);
// Map unit icons may keep their original pixels while the rest of the art is HD.
// Saved as map_unit_icons ("hd" by default, or "original"); read on the renderer thread.
bool hd_map_unit_icons();
void set_hd_map_unit_icons(bool hd);
// A small readout of the game's frames per second and the longest frame in a corner
// (frame_rate.hpp, frontend.cpp). Off by default; saved as show_fps.
bool show_fps();
void set_show_fps(bool show);
// The reading controls under the dialogue (dialogue_scene.cpp): hidden 5 s after the
// dialogue appears and shown again 3 s by a direction key (the default), or always shown.
// Saved as dialogue_hints, "auto" or "always".
bool dialogue_hints_always();
void set_dialogue_hints_always(bool always);
// Dialogue text size, independent of the interface scale. Saved as dialogue_font_size.
// Safe on the game thread too: the window thread writes pending changes in update().
unsigned dialogue_font_size();
void set_dialogue_font_size(unsigned size,bool remember=true);
// Phone controls use a fixed D-pad. Opacity (25..100) scales controls, text and outlines.
unsigned touch_opacity();
void set_touch_opacity(unsigned percent);
// The debug interface for AI agents over MCP (docs/guide/debug-interface.md): switched in
// the About page, saved as debug_interface, off by default. --debug (SRW64_DEBUG=1) turns
// it on for a run whatever is saved, and the switch then cannot turn it off.
bool debug_interface();
void set_debug_interface(bool on);
bool debug_interface_forced();
// Where it listens while on ("127.0.0.1:port") and the run directory, for the About page;
// both empty while off. Set by debug_server.cpp.
struct DebugEndpoint {std::string address,run;};
DebugEndpoint debug_endpoint();
void set_debug_endpoint(DebugEndpoint endpoint);
// The user directory (presentation.json's folder; the run directory without one), for a
// bug report (bug_report.hpp).
std::filesystem::path data_folder();
// Around the picture and over it (docs/native/bezels-and-filters.md). A bezel is a
// RetroArch overlay image (a .png, or an overlay .cfg naming one) laid around the picture
// while it is 4:3; a filter is a RetroArch slang shader preset (.slangp) run over the
// picture through librashader (post_filter.hpp). Both are absolute paths, empty for none,
// saved as bezel and filter. filter_scale is the picture's height the preset reads: 1-4
// times the original 240 lines, or 0 for the window's own pixels; saved as filter_scale.
std::string bezel();
void set_bezel(const std::string& path);
std::string filter();
void set_filter(const std::string& path);
unsigned filter_scale();
void set_filter_scale(unsigned scale);
// Where the pickers start (docs/native/bezels-and-filters.md): the ones shipped beside the
// program (filters/, bezels/), the player's own folders of the same names in their data
// folder (made with a README.txt the first time the window opens, for them to add to),
// then RetroArch's overlays and slang shaders where RetroArch is installed.
struct LookFolder {
    enum Kind {builtin, mine, retroarch} kind;
    std::filesystem::path path;
};
std::vector<LookFolder> bezel_roots();
std::vector<LookFolder> filter_roots();
// The cheats page's switches (cheats.hpp), a mask of cheats::Switch. Saved as cheats,
// a list of ids; SRW64_CHEATS for a run takes the place of the saved list.
unsigned cheats();
void set_cheats(unsigned switches);
// The size of the interface drawn in dp (the settings window, the pre-battle page, the
// title's settings button, notices) and of the dialogue's bottom bar: Standard, Large or
// Largest, 1, 1.25 or 1.5 times, as far as the window has room (frontend.cpp sync). Saved
// as ui_size once the player chooses; until then Largest on a Steam Deck, whose 7-inch
// 1280 x 800 screen shows a dp at about half a desktop's size (Largest still falls
// short of a desktop's Standard), and Standard elsewhere.
enum class UiSize {Standard,Large,Largest};
UiSize ui_size();
void set_ui_size(UiSize size);
// "standard", "large", "largest"; a name this build does not know reads as the default.
const char* ui_size_name(UiSize size);
float ui_scale(UiSize size);
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
// A key as the player sees it on this keyboard layout ("Z", "Y" on a German one).
std::string key_display_name(int scancode);
}
