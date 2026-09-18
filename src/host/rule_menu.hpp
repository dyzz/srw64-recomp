#pragma once

// 选项 in the application menu bar: 游戏性调整 holds one checked item per optional
// rule (rule_fixes.hpp), switchable at any time during play, and 设置… opens the
// settings window. Implemented for macOS in rule_menu_macos.mm; the hosts without
// a window never call it.
#include <filesystem>
namespace srw64::rule_menu {
// Window thread, every frame: installs the menu when the menu bar is ready and
// re-titles it when the player switches language.
void update();
void shutdown();
// QA only (SRW64_WINDOW_CONTROL=1): press one menu item by rule id, "defaults",
// "original" or "all", from rule-control.json, and record the menu's own state afterwards.
void control(const std::filesystem::path& directory);
}
