#pragma once
#include "ultramodern/renderer_context.hpp"
#include "json/json.hpp"
#include <filesystem>

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle);
ultramodern::renderer::WindowHandle srw64_create_window(void*);
void srw64_update_window(void*);
void srw64_keyboard_input(uint16_t* buttons, float* x, float* y);
void srw64_destroy_window();
void srw64_set_capture_directory(const std::filesystem::path& path);
void srw64_set_capture_clock(const char* name);
uint64_t srw64_current_vi();
// Window thread: focus, size and title, for the debug interface's status.
nlohmann::json srw64_window_status();
