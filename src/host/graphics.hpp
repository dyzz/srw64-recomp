#pragma once
#include "ultramodern/renderer_context.hpp"
#include "json/json.hpp"
#include <filesystem>
#include <functional>

namespace plume { struct RenderCommandList; }

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle);
ultramodern::renderer::WindowHandle srw64_create_window(void*);
void srw64_update_window(void*);
void srw64_keyboard_input(uint16_t* buttons, float* x, float* y);
// Controller buttons as the N64 mask (bits 16-19: stick up/down/left/right; bits
// 20-22: View, L2, R2, input_mode.hpp), for native pages that own the pad.
uint32_t srw64_pad_state();
// The connected controller's name as SDL knows it, empty without one (window thread).
std::string srw64_pad_name();
// Keyboard and controller together, the same mask, as held before any page or the
// dialogue reader filters them: for host features that must see a button the game
// is not shown.
uint32_t srw64_keyboard_state();
void srw64_destroy_window();
void srw64_set_capture_directory(const std::filesystem::path& path);
void srw64_set_capture_clock(const char* name);
uint64_t srw64_current_vi();
// Present thread: run the callback once the GPU has finished the command list the
// draw hook is recording. Metal uses the command buffer's completion handler;
// other backends run it after RT64's fence wait on that submission.
void srw64_after_gpu(plume::RenderCommandList* list, std::function<void(bool completed)> callback);
// Window thread: focus, size and title, for the debug interface's status.
nlohmann::json srw64_window_status();
// The graphics API in use and the GPU's name, vendor, driver and memory (any thread).
nlohmann::json srw64_graphics_info();
// Window thread: resize ({width, height}), raise ({front: true}) and/or press the
// close button ({close: true}) of the game window.
nlohmann::json srw64_window_control(const nlohmann::json& params);
