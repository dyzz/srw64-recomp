// Thin adapter for the current Metal host. Upload/pipeline/draw code is shared
// with Vulkan and D3D12; only target inspection and completion remain here.
#include "dialogue_raster.hpp"
#include "diagnostics.hpp"
#include "presentation/pixel_compositor.hpp"
#include "plume_metal.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace srw64::dialogue {
namespace {
using presentation::PixelCompositor;
using json = nlohmann::json;
std::unique_ptr<PixelCompositor> compositor;
PixelCompositor::Image image;
std::filesystem::path output;
std::string cached_key;
}
void metal_init(plume::RenderDevice* device, const std::filesystem::path& directory) {
    image.reset(); cached_key.clear(); output = directory;
    compositor = std::make_unique<PixelCompositor>(*device,
        presentation::embedded_pixel_shaders(plume::RenderShaderFormat::METAL));
}
void metal_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, uint64_t workload) {
    auto frame = presented_frame(workload);
    if (!frame || std::none_of(frame->boxes.begin(),frame->boxes.end(),[](const auto& b){return b.visible;})) return;
    if (!compositor) throw std::runtime_error("Dialogue compositor is not initialized");
    localization::Scope locale(frame->catalog);
    const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (fb->colorAttachments.size() != 1 || fb->colorAttachments[0].getTexture()->sampleCount() != 1)
        throw std::runtime_error("Dialogue requires one non-MSAA color target");
    const auto format = fb->colorAttachments[0].format;
    if (format != plume::RenderFormat::B8G8R8A8_UNORM && format != plume::RenderFormat::R8G8B8A8_UNORM)
        throw std::runtime_error("Unvalidated native UI color target");
    auto* command = static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    // Preserve the existing workload/cache identity and CPU raster behavior.
    std::ostringstream key;
    key << localization::catalog().locale << localization::catalog().font << localization::catalog().revision
        << ',' << framebuffer->getWidth() << ',' << framebuffer->getHeight() << ',' << frame->font_size
        << ',' << frame->speed << frame->auto_read << frame->history_open << frame->history_offset << frame->fast << frame->skipping
        << ',' << frame->reading_event << ',' << frame->advance.visible << frame->advance.waiting << frame->advance.paused << ',' << frame->advance.permille;
    for (const auto& box : frame->boxes)
        key << ':' << box.event << ',' << box.visible << box.active << ',' << box.page << ',' << box.revealed << ',' << box.x << ',' << box.y;
    if (!image || cached_key != key.str()) {
        auto raster = rasterize_frame(*frame,framebuffer->getWidth(),framebuffer->getHeight());
        auto next = compositor->upload(*list,raster.image);
        // Retain uploads independently of draw success until the command buffer
        // completes. A later cache replacement cannot free a recorded upload.
        command->mtl->addCompletedHandler([next](MTL::CommandBuffer*) { (void)next; });
        image = std::move(next); cached_key = key.str();
        // The scene reports its portable CPU text backend.
        raster.report["presentation"] = "Plume (Metal surface)";
        raster.report["compositor"] = "Plume";
        if (srw64_full_diagnostics()) std::ofstream(output/"dialogue-raster.json") << raster.report.dump(2) << '\n';
    }
    const auto retained = compositor->draw(*list,*framebuffer,format,image);
    command->mtl->addCompletedHandler([retained](MTL::CommandBuffer* completed) {
        (void)retained;
        if (completed->status() != MTL::CommandBufferStatusCompleted)
            std::fputs("SRW64_DIALOGUE_GPU_FAILED\n",stderr);
    });
    if (!srw64_full_diagnostics()) return;
    json state = {{"schema","srw64.native-dialogue-present.v1"},{"workload",workload},{"native_vi",frame->vi},
        {"locale",localization::catalog().locale},{"catalog",localization::catalog().revision},
        {"history_open",frame->history_open},{"auto_read",frame->auto_read},{"speed",frame->speed},{"skipping",frame->skipping},
        {"reading_event",frame->reading_event},{"advance",{{"visible",frame->advance.visible},
            {"permille",frame->advance.permille},{"waiting",frame->advance.waiting},{"paused",frame->advance.paused}}},
        {"boxes",json::array()},{"compositor","Plume"}};
    const auto* focus = frame->focused_box(); state["focused_slot"] = focus ? json(focus->slot) : json(nullptr);
    for (const auto& b : frame->boxes) if (b.visible) state["boxes"].push_back({{"event",b.event},{"text_id",b.text_id},{"segment",b.segment},
        {"active",b.active},{"speaker",utf8(b.speaker)},{"text",utf8(b.layout.text)},
        {"page",b.page},{"pages",b.layout.pages.size()},{"revealed_utf16",b.revealed}});
    std::ofstream(output/"dialogue-present.tmp") << state.dump(2) << '\n';
    std::filesystem::rename(output/"dialogue-present.tmp",output/"dialogue-present.json");
}
void metal_shutdown() {
    // The host must wait for submitted work before destroying its RenderDevice.
    // Completion callbacks own any older images and their shared pipeline state.
    image.reset(); compositor.reset(); cached_key.clear();
}
}
