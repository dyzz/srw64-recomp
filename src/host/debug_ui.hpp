#pragma once
#include "json/json.hpp"
#include <filesystem>

// Generic host keyboard and mouse for the native (AppKit) UI that replaces or
// sits on top of the game's own screens: the name page, the settings panel,
// the menu bar and whatever comes later. Nothing here knows a particular page;
// it works on windows, views, key events and clicks. Window thread only.
//
// Coordinates are points in a window's content area with the origin at the top
// left, the same orientation as screenshots (pixels = points x "scale").
// A window selector is "game" (default), "key", a window number, or a title
// substring such as "Options" / "选项".
namespace srw64::debug_ui {
void window_init(void* cocoa_window);
// Presses the game window's close button (NSWindow performClose:).
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
// Draws the game window's visible AppKit overlays onto a GPU screenshot in place.
nlohmann::json compose(const std::filesystem::path& png);
// Renders a pure AppKit window (e.g. the settings panel) to a PNG.
nlohmann::json capture(const nlohmann::json& params, const std::filesystem::path& png);
// Main-thread facts for status: windows and the focused control.
nlohmann::json summary();
}
