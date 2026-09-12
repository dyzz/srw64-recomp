#pragma once

#include <cstdint>
#include <filesystem>

struct SDL_Window;

namespace srw64::qa {
// Called on the window thread. Inert unless SRW64_WINDOW_CONTROL is enabled.
// Controls presentation and the real window only; never accesses guest RAM.
void update_window(SDL_Window* window, const std::filesystem::path& output, uint64_t vi);
}
