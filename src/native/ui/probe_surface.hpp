#pragma once
#include "common/rt64_plume.h"
#include <SDL.h>
#include <filesystem>
#include <functional>

namespace srw64::ui {
// Platform boundary for the standalone probe, separate from page/input logic.
class ProbeSurface {
    void* view{};
public:
    SDL_Window* window{};
    plume::RenderWindow handle{};
    std::unique_ptr<plume::RenderInterface> interface;
    ProbeSurface();
    ~ProbeSurface();
    // Record readback, return an encoder run only after the queue fence.
    std::function<void()> capture(plume::RenderDevice*,plume::RenderCommandList*,plume::RenderTexture*,
                                  unsigned,unsigned,const std::filesystem::path&);
};
}
