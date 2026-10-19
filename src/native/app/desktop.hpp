#pragma once
#include "runtime.hpp"

namespace srw64::app {
// Callbacks are invoked on the main/window thread. The controller has no GUI,
// renderer, interpreter or subprocess dependency; each platform supplies its UI.
struct DesktopUi {
    // The chosen file, nullopt when the player cancels; throws DesktopPickerUnavailable when
    // no picker can open at all, which is not a cancel.
    std::function<std::optional<fs::path>()> choose_rom;
    std::function<void(const std::string&)> show_error;
    // A yes/no question; the labels name the two buttons where the platform lets them.
    std::function<bool(const std::string& message, const std::string& accept, const std::string& cancel)> confirm;
};
struct DesktopPickerUnavailable : std::runtime_error {
    DesktopPickerUnavailable() : std::runtime_error("No file picker is available") {}
};
// The existing standalone bootstrap calls ready once, AFTER ROM/content/save
// validation and while its Session still holds the user-directory lock, directly
// before entering the game. Never call ready before acquiring that lock.
using DesktopReady = std::function<void()>;
using DesktopLaunch = std::function<int(const Options&, const DesktopReady&)>;
int run_desktop(Options options, const std::string& expected_rom_sha256,
                const DesktopUi& ui, const DesktopLaunch& launch,
                bool choose_another_rom = false);
// What the player sees when another game holds the user directory, in their language
// (zh-Hans, en or ja; anything else gets Simplified Chinese): the notice, and the question
// with its two buttons when that game can be ended.
// The player's language for a message before the game runs: --language, else the one
// saved in the user directory, else Simplified Chinese.
std::string player_locale(const Options& options);
std::string play_lock_message(const std::string& locale);
struct PlayLockQuestion { std::string message, accept, cancel; };
PlayLockQuestion play_lock_question(const std::string& locale);
// Where Marchwind64.cmd looks for the ROM, in its order: `named` (SRW64_ROM), then
// rom.z64, rom.n64 and rom.v64, each in the user directory before the game's folder.
// Empty when none of them is a file.
fs::path launcher_rom(const fs::path& named, const fs::path& user_dir, const fs::path& game_dir);
// Shared Windows/Linux policy for desktop and --play (scripts delegate to --play).
// Existing game_dir/user-data opts in; missing settings/save groups import from the system user directory,
// and normal desktop exit mirrors back with backup/confirmation. Explicit --user-dir stays
// isolated. Save-only commands retain their previous system-directory default.
Options prepare_desktop_launch(Options options, const fs::path& game_dir, Platform platform,
                               const EnvironmentLookup& lookup = {}, const fs::path& named_rom = {});
Options prepare_windows_launch(Options options, const fs::path& game_dir,
                               const EnvironmentLookup& lookup = {}, const fs::path& named_rom = {});
Options prepare_linux_launch(Options options, const fs::path& game_dir,
                             const EnvironmentLookup& lookup = {}, const fs::path& named_rom = {});
// Linux uses kdialog or zenity through argument vectors, never a shell. Missing helpers
// never authorize replacement; --rom still permits play without a picker. Without them the
// picker throws DesktopPickerUnavailable and errors go to `fallback` (the game's own box),
// so the player is never left with a game that closed without a word.
DesktopUi linux_desktop_ui(std::function<void(const std::string&)> fallback = {});
fs::path linux_named_rom();
// macOS implementation only; tests of run_desktop inject deterministic UI.
DesktopUi macos_desktop_ui();
bool macos_choose_another_rom();
// Windows (src/host/windows/desktop_windows.cpp): the open-file dialog and message box,
// SRW64_ROM as a path, and closing the console window a double-click opened for the
// program (a console started from a shell stays), before anything else opens a handle.
DesktopUi windows_desktop_ui();
fs::path windows_named_rom();
void windows_release_console();
}
