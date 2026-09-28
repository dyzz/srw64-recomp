#pragma once
// Running on a Steam Deck. Steam sets SteamDeck=1 for the games it starts there; started
// any other way (Desktop Mode, a terminal) the firmware still names the machine: Valve's
// Jupiter (LCD) or Galileo (OLED). graphics.cpp shows the Deck's button icons and
// presentation_settings.cpp gives it the larger interface.
#include <cstdlib>
#include <fstream>
#include <string>

namespace srw64 {
inline bool on_steam_deck() {
    static const bool deck = [] {
        if (const char* value = std::getenv("SteamDeck"); value && std::string(value) == "1") return true;
#ifdef __linux__
        const auto read = [](const char* path) { std::ifstream file(path); std::string line; std::getline(file, line); return line; };
        const auto product = read("/sys/devices/virtual/dmi/id/product_name");
        return read("/sys/devices/virtual/dmi/id/board_vendor") == "Valve" && (product == "Jupiter" || product == "Galileo");
#else
        return false;
#endif
    }();
    return deck;
}
}  // namespace srw64
