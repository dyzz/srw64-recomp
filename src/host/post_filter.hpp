#pragma once
#include <filesystem>
#include <string>

namespace plume {
struct RenderCommandList;
struct RenderFramebuffer;
}

// RetroArch slang shader presets over the game picture (docs/native/bezels-and-filters.md),
// run by librashader, which the host loads at run time: a build or machine without it
// plays as before and the settings say the filters are unavailable. Metal, Vulkan and
// D3D12 (Windows).
//
// Each present, after the picture and the dialogue are drawn and before the interface:
// the picture's rectangle is copied out, brought down to the height the player chose
// (filter_scale lines, by a passthrough preset of our own), and the chosen preset draws
// it back into the same rectangle. The interface, the bezel and the settings stay sharp.
namespace srw64::post_filter {

struct Status {
    bool available = false;   // librashader loaded and the backend is supported
    bool loading = false;     // a preset is being compiled
    std::string preset;       // the preset now drawn, empty for none
    std::string error;        // why the last preset failed, empty if it did not
};
Status status();   // any thread
// Any thread: a preset is chosen and the library can run it, so this present goes through
// apply() (the interface is then drawn in two passes, ui::draw).
bool active();

// Render thread, once RT64 hands over the device: RT64's backend, and a folder for our
// own passthrough preset.
enum class Backend {none, metal, vulkan, d3d12};
void init(Backend backend, const std::filesystem::path& scratch);
// Render thread: replaces the picture rectangle (x, y, w, h in the target's pixels) with
// the chosen preset's output. `picture_width` is the picture in original pixels (320 at
// 4:3). Does nothing while no preset is chosen or loaded.
void apply(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer,
           int x, int y, int w, int h, float picture_width);
void shutdown();

}  // namespace srw64::post_filter
