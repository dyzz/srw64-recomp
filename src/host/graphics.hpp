#pragma once
#include "ultramodern/renderer_context.hpp"
#include "json/json.hpp"
#include <filesystem>

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle);
ultramodern::renderer::WindowHandle srw64_create_window(void*);
void srw64_update_window(void*);
void srw64_keyboard_input(uint16_t* buttons, float* x, float* y);
// Controller buttons as the N64 mask (bits 16-19: stick up/down/left/right), for
// native pages that own the pad.
uint32_t srw64_pad_state();
void srw64_destroy_window();
void srw64_set_capture_directory(const std::filesystem::path& path);
void srw64_set_capture_clock(const char* name);
uint64_t srw64_current_vi();
// Window thread: focus, size and title, for the debug interface's status.
nlohmann::json srw64_window_status();
// Window thread: resize ({width, height}), raise ({front: true}) and/or press the
// close button ({close: true}) of the game window.
nlohmann::json srw64_window_control(const nlohmann::json& params);
