// Thin present-hook adapter. Upload/pipeline/draw code is shared by Metal, Vulkan
// and D3D12; only target inspection differs, and completion goes through the host.
#include "dialogue_raster.hpp"
#include "game_frame.hpp"
#include "presentation/pixel_compositor.hpp"
#ifdef __APPLE__
#include "plume_metal.h"
#endif
#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <sstream>

// graphics.cpp: runs once the GPU has finished the list the present hook records.
void srw64_after_gpu(plume::RenderCommandList* list, std::function<void(bool completed)> callback);

namespace srw64::dialogue {
namespace {
using presentation::PixelCompositor;
using json = nlohmann::json;
std::unique_ptr<PixelCompositor> compositor;
// A window-sized texture kept between frames; only what changed is painted and uploaded.
PixelCompositor::Image canvas;
uint32_t canvas_width{}, canvas_height{};
std::atomic<bool> canvas_lost{};  // a list with canvas uploads never ran: repaint it all
IncrementalRaster raster;
std::filesystem::path output;
bool metal = false;  // Plume's Metal backend: its framebuffer reports its format
}
void gpu_init(plume::RenderInterface* rhi, plume::RenderDevice* device, const std::filesystem::path& directory) {
    canvas.reset(); raster.reset(); output = directory;
    metal = rhi->getCapabilities().shaderFormat == plume::RenderShaderFormat::METAL;
    compositor = std::make_unique<PixelCompositor>(*device,
        presentation::embedded_pixel_shaders(rhi->getCapabilities().shaderFormat));
}
void gpu_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, uint64_t workload) {
    auto frame = presented_frame(workload);
    if (!frame || std::none_of(frame->boxes.begin(),frame->boxes.end(),[](const auto& b){return b.visible;})) return;
    if (!compositor) throw std::runtime_error("Dialogue compositor is not initialized");
    localization::Scope locale(frame->catalog);
    // The present hook always draws into RT64's swapchain framebuffer, which is
    // B8G8R8A8 without MSAA (rt64_application.cpp), R8G8B8A8 on Android
    // (rt64_android_patches.py); Plume has no format query.
#ifdef __ANDROID__
    auto format = plume::RenderFormat::R8G8B8A8_UNORM;
#else
    auto format = plume::RenderFormat::B8G8R8A8_UNORM;
#endif
#ifdef __APPLE__
    if (metal) {
        const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
        if (fb->colorAttachments.size() != 1 || fb->colorAttachments[0].getTexture()->sampleCount() != 1)
            throw std::runtime_error("Dialogue requires one non-MSAA color target");
        format = fb->colorAttachments[0].format;
        if (format != plume::RenderFormat::B8G8R8A8_UNORM && format != plume::RenderFormat::R8G8B8A8_UNORM)
            throw std::runtime_error("Unvalidated native UI color target");
        auto* command = static_cast<plume::MetalCommandList*>(list);
        command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    }
#endif
    const uint32_t width = framebuffer->getWidth(), height = framebuffer->getHeight();
    if (canvas_lost.exchange(false)) raster.reset();
    if (!canvas || canvas_width != width || canvas_height != height) {
        // The old canvas stays alive through its in-flight draws' retention.
        canvas = compositor->canvas(width, height);
        canvas_width = width; canvas_height = height;
        raster.reset();
    }
    std::ostringstream context;
    context << localization::catalog().locale << ',' << localization::catalog().font << ',' << localization::catalog().revision;
    auto update = raster.update(*frame, width, height, srw64::frame::width(width, height), context.str());
    for (const auto& patch : update.patches) {
        // Keep each upload's staging until the command buffer completes.
        auto staging = compositor->update(*list, canvas, uint32_t(patch.rect.left), uint32_t(patch.rect.top), patch.pixels);
        srw64_after_gpu(list, [staging](bool completed) { (void)staging; if (!completed) canvas_lost = true; });
    }
    if (update.drawn.empty()) return;
    const plume::RenderRect scissor(update.drawn.left, update.drawn.top, update.drawn.right, update.drawn.bottom);
    const auto retained = compositor->draw(*list,*framebuffer,format,canvas,&scissor);
    srw64_after_gpu(list, [retained](bool completed) {
        (void)retained;
        if (!completed) std::fputs("SRW64_DIALOGUE_GPU_FAILED\n",stderr);
    });
}
void gpu_shutdown() {
    // The host must wait for submitted work before destroying its RenderDevice.
    // Completion callbacks own any older images and their shared pipeline state.
    canvas.reset(); compositor.reset(); raster.reset();
}
}
