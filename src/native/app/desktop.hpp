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
// macOS implementation only; tests of run_desktop inject deterministic UI.
DesktopUi macos_desktop_ui();
bool macos_choose_another_rom();
}
