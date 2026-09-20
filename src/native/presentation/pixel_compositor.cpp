#include "pixel_compositor.hpp"
#include <cstring>
#include <map>
#include <stdexcept>

namespace srw64::presentation {
using namespace plume;
namespace {
template<class T> std::unique_ptr<T> require(std::unique_ptr<T> value, const char* what) {
    if (!value) throw std::runtime_error(what);
    return value;
}
const RenderDescriptorRange image_range{RenderDescriptorRangeType::TEXTURE, 0, 1};
const RenderDescriptorSetDesc image_set{&image_range, 1};
bool supported(RenderFormat value) {
    return value == RenderFormat::B8G8R8A8_UNORM || value == RenderFormat::R8G8B8A8_UNORM;
}
}
struct PixelCompositor::State {
    RenderDevice* device;
    std::unique_ptr<RenderShader> vertex, pixel;
    std::unique_ptr<RenderPipelineLayout> layout;
    std::map<RenderFormat, std::unique_ptr<RenderPipeline>> pipelines;
    State(RenderDevice& d, const PixelShaders& shaders) : device(&d) {
        if (shaders.vertex.empty() || shaders.pixel.empty())
            throw std::runtime_error("Pixel compositor shader binaries are missing");
        vertex = require(d.createShader(shaders.vertex.data(), shaders.vertex.size(), "VSMain", shaders.format), "Pixel vertex shader failed");
        pixel = require(d.createShader(shaders.pixel.data(), shaders.pixel.size(), "PSMain", shaders.format), "Pixel fragment shader failed");
        layout = require(d.createPipelineLayout(RenderPipelineLayoutDesc(nullptr, 0, &image_set, 1)), "Pixel pipeline layout failed");
    }
    RenderPipeline* pipeline(RenderFormat format) {
        auto found = pipelines.find(format);
        if (found != pipelines.end()) return found->second.get();
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = layout.get();
        desc.vertexShader = vertex.get();
        desc.pixelShader = pixel.get();
        desc.renderTargetCount = 1;
        desc.renderTargetFormat[0] = format;
        // CPU pixels already have alpha applied. Applying SRC_ALPHA a second
        // time darkens antialiased glyph edges and changes translucent panels.
        desc.renderTargetBlend[0] = RenderBlendDesc::AlphaBlend();
        desc.renderTargetBlend[0].srcBlend = RenderBlend::ONE;
        desc.depthEnabled = false;
        desc.depthWriteEnabled = false;
        desc.cullMode = RenderCullMode::NONE;
        auto next = require(device->createGraphicsPipeline(desc), "Pixel graphics pipeline failed");
        auto* result = next.get();
        pipelines.emplace(format, std::move(next));
        return result;
    }
};
struct PixelCompositor::Pixels {
    std::shared_ptr<State> owner;
    uint32_t width{}, height{};
    std::unique_ptr<RenderBuffer> staging;
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderDescriptorSet> descriptors;
};
PixelCompositor::PixelCompositor(RenderDevice& device, const PixelShaders& shaders)
    : state_(std::make_shared<State>(device, shaders)) {}
PixelCompositor::~PixelCompositor() = default;

PixelCompositor::Image PixelCompositor::upload(RenderCommandList& list, const Bgra8Surface& surface) {
    surface.validate();
    auto result = std::make_shared<Pixels>();
    result->owner = state_;
    result->width = surface.width;
    result->height = surface.height;
    const uint32_t row_pixels = (surface.width + 63U) & ~63U;
    const size_t pitch = size_t(row_pixels) * 4;
    const size_t total = pitch * surface.height;
    result->staging = require(state_->device->createBuffer(RenderBufferDesc::UploadBuffer(total)), "Pixel staging allocation failed");
    result->texture = require(state_->device->createTexture(RenderTextureDesc::Texture2D(surface.width, surface.height, 1, RenderFormat::B8G8R8A8_UNORM)), "Pixel texture allocation failed");
    result->descriptors = require(state_->device->createDescriptorSet(image_set), "Pixel descriptor allocation failed");
    auto* destination = static_cast<unsigned char*>(result->staging->map());
    if (!destination) throw std::runtime_error("Cannot map pixel upload buffer");
    std::memset(destination, 0, total);
    for (uint32_t y = 0; y < surface.height; ++y)
        std::memcpy(destination + size_t(y) * pitch, surface.pixels.data() + size_t(y) * surface.row_bytes(), surface.row_bytes());
    result->staging->unmap();
    result->descriptors->setTexture(0, result->texture.get(), RenderTextureLayout::SHADER_READ);
    list.barriers(RenderBarrierStage::COPY, RenderTextureBarrier(result->texture.get(), RenderTextureLayout::COPY_DEST));
    list.copyTextureRegion(RenderTextureCopyLocation::Subresource(result->texture.get()),
        RenderTextureCopyLocation::PlacedFootprint(result->staging.get(), RenderFormat::B8G8R8A8_UNORM,
            surface.width, surface.height, 1, row_pixels));
    list.barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(result->texture.get(), RenderTextureLayout::SHADER_READ));
    return result;
}
PixelCompositor::Retention PixelCompositor::draw(RenderCommandList& list, RenderFramebuffer& target,
                                                RenderFormat format, const Image& image) {
    if (!supported(format)) throw std::runtime_error("Pixel compositor requires BGRA/RGBA8 UNORM, not sRGB/HDR");
    if (!image || image->owner != state_) throw std::runtime_error("Pixel image belongs to a different compositor");
    if (target.getWidth() != image->width || target.getHeight() != image->height)
        throw std::runtime_error("Pixel image and target dimensions differ");
    auto* pipeline = state_->pipeline(format);
    list.setFramebuffer(&target);
    list.setGraphicsPipelineLayout(state_->layout.get());
    list.setPipeline(pipeline);
    list.setGraphicsDescriptorSet(image->descriptors.get(), 0);
    list.setViewports(RenderViewport(0, 0, float(image->width), float(image->height)));
    list.setScissors(RenderRect(0, 0, int32_t(image->width), int32_t(image->height)));
    list.drawInstanced(3, 1, 0, 0);
    return image;
}
}
