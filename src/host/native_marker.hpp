#pragma once
#include <filesystem>
namespace plume { struct RenderDevice; }
namespace srw64::marker {
void configure(const std::filesystem::path& output);
bool replacement_enabled();
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
}
