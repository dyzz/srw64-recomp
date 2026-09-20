#pragma once
#include "json/json.hpp"
#include <filesystem>

// Window-thread debug controls for the shared SDL/RmlUi game surface.
// Coordinates are window points, top-left origin; pixels = points * scale.
// ui.tree exposes stable element ids. click accepts text/id or coordinates.
// Settings share the game window. Legacy native-window capture is unsupported.
namespace srw64::debug_ui {
// Delivers SDL_WINDOWEVENT_CLOSE to the game window.
void close_game_window();
// Every visible window of the app (or the selected one) with its view tree:
// class, frame, title/text, enabled, focused, and the game's Metal view marked.
nlohmann::json tree(const nlohmann::json& params);
// {x, y} or {text} (a button title, field text or accessibility label), with
// optional window, button ("left"/"right") and count.
nlohmann::json click(const nlohmann::json& params);
// {key} ("return", "tab", "escape", "delete", arrows, "space", a-z, 0-9, f1-f12)
// or {key_code, characters}; optional modifiers ["cmd","shift","option","control"].
nlohmann::json key(const nlohmann::json& params);
// {text}: inserted into the window's focused text field, as typing would.
// {text, marked: true} leaves it as an input-method composition; {unmark: true}
// commits the composition.
nlohmann::json type(const nlohmann::json& params);
// {path: ["选项","游戏性调整"]}: presses the item, or lists a submenu; {} lists the bar.
nlohmann::json menu(const nlohmann::json& params);
// Shared UI is already part of the GPU screenshot; reports that no overlay pass is needed.
nlohmann::json compose(const std::filesystem::path& png);
// Legacy separate-window capture reports that callers must use the game surface.
nlohmann::json capture(const nlohmann::json& params, const std::filesystem::path& png);
// Main-thread facts for status: windows and the focused control.
nlohmann::json summary();
}
