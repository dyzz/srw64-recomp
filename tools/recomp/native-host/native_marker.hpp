#pragma once
#include <filesystem>
namespace plume { struct RenderDevice; }
namespace srw64::marker {
void configure(const std::filesystem::path& output);
bool replacement_enabled();
void metal_init(plume::RenderDevice* device);
void shutdown();
}
