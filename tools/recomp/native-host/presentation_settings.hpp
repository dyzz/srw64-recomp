#pragma once
#include <filesystem>
#include <cstdint>
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
}
