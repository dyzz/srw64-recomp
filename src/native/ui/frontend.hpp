#pragma once
#include <SDL.h>
#include <filesystem>
#include "json/json.hpp"
namespace plume { class RenderInterface; class RenderDevice; class RenderCommandList; class RenderFramebuffer; }
namespace srw64::ui {
// SDL/window thread owns events, layout and semantic actions. RmlUi and the
// renderer are serialized; layout waits for the previous GPU submission before
// releasing any geometry or textures. No guest memory is read here.
void window_init(SDL_Window*, const std::filesystem::path&);
void update();
bool event(SDL_Event&);
// The on-screen controller's buttons (touch_pad.hpp), in srw64_pad_state()'s bits.
uint32_t touch_buttons();
void shutdown();
void render_init(plume::RenderInterface*, plume::RenderDevice*);
// layer: -1 every page; 0 those that belong to the game picture, drawn before a RetroArch
// filter; 1 the rest (settings, notices, the bezel, the frame rate, touch buttons), after it
// (docs/native/bezels-and-filters.md).
bool draw(plume::RenderCommandList*, plume::RenderFramebuffer*, bool name_cover, int layer = -1);
void presented();
void render_shutdown();
nlohmann::json tree();
nlohmann::json click(const nlohmann::json&);
nlohmann::json key(const nlohmann::json&);
nlohmann::json type(const nlohmann::json&);
nlohmann::json menu(const nlohmann::json&);
}
