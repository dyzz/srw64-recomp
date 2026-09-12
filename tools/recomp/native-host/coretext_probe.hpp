#pragma once
#include <filesystem>

namespace plume { struct RenderCommandList; struct RenderFramebuffer; struct RenderDevice; }

// Enabled only in the captured-frame experimental executable.
void srw64_coretext_init(plume::RenderDevice* device, const std::filesystem::path& output);
void srw64_coretext_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer);
void srw64_coretext_shutdown();
