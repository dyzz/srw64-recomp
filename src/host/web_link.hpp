#pragma once
// The web maker link (docs/design/unit-mod-web-tool.md W3): the site's unit MOD maker
// (web/src/lib/game-link.ts) reaches the game on the same computer over plain HTTP on
// 127.0.0.1:46464, while the About page's switch (settings::web_link) is on. Only pages of
// the site (and a development server on this computer) by their Origin, and only after the
// player allowed the page in the game (pairing hands it a token). It can put a package to try
// in the game (mods::install_trial), start the battle viewer from the title and take a
// screenshot; nothing else of the debug interface.
#include "json/json.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace srw64::web_link {
inline constexpr uint16_t port=46464;
inline constexpr int protocol=1;

// Window thread, every frame: listens while the switch is on, stops when it goes off.
void service();
// Whether it listens now, and why not when it tried and could not ("" when it did not try).
bool listening();
std::string failure();

// A page asking to pair, waiting for the player's answer (the window thread shows it).
struct Prompt { unsigned id; std::string origin; };
std::optional<Prompt> prompt();
void answer(unsigned id,bool allow);

// Window thread, every frame: the viewer start a page asked for, if any, run by `start`
// (the choice in viewer.start's form; "" when it started, else why not).
void run_view(const std::function<std::string(const nlohmann::json& choice)>& start);

// Pure parts, for tests: whether a page's Origin may use the link.
bool allowed_origin(const std::string& origin);
}
