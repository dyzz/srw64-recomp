#pragma once
#include "runtime.hpp"

namespace srw64::app {
// Callbacks are invoked on the main/window thread. The controller has no GUI,
// renderer, interpreter or subprocess dependency; each platform supplies its UI.
struct DesktopUi {
    std::function<std::optional<fs::path>()> choose_rom;
    std::function<void(const std::string&)> show_error;
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
// (zh-Hans, en or ja; anything else gets Simplified Chinese).
std::string play_lock_message(const std::string& locale);
// Where Marchwind64.cmd looks for the ROM, in its order: `named` (SRW64_ROM), then
// rom.z64, rom.n64 and rom.v64, each in the user directory before the game's folder.
// Empty when none of them is a file.
fs::path launcher_rom(const fs::path& named, const fs::path& user_dir, const fs::path& game_dir);
// macOS implementation only; tests of run_desktop inject deterministic UI.
DesktopUi macos_desktop_ui();
bool macos_choose_another_rom();
// Windows (src/host/windows/desktop_windows.cpp): the open-file dialog and message box,
// SRW64_ROM as a path, and closing the console window a double-click opened for the
// program (a console started from a shell stays).
DesktopUi windows_desktop_ui();
fs::path windows_named_rom();
void windows_release_console();
}
