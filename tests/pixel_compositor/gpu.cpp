#include "presentation/pixel_compositor.hpp"
#if defined(__APPLE__)
#include "plume_metal.h"
#elif defined(_WIN32)
#include "plume_d3d12.h"
#else
#include "plume_vulkan.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace plume;
using namespace srw64::presentation;
namespace {
unsigned checks{};
void check(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F action) {
    ++checks;
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid compositor operation accepted");
}
Bgra8Surface pattern(uint32_t w, uint32_t h, unsigned seed) {
    Bgra8Surface image(w, h);
    constexpr unsigned alphas[] = {0, 1, 64, 127, 128, 254, 255};
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
        auto* p = &image.pixels[(size_t(y) * w + x) * 4];
        const auto a = alphas[(x + y * 3 + seed) % 7];
        p[0] = static_cast<unsigned char>((x * 37 + y * 11 + seed * 41) % (a + 1));
        p[1] = static_cast<unsigned char>((x * 13 + y * 67 + seed * 23) % (a + 1));
        p[2] = static_cast<unsigned char>((x * 71 + y * 17 + seed * 53) % (a + 1));
        p[3] = static_cast<unsigned char>(a);
    }
    return image;
}
struct Draw {
    uint32_t w, h, row;
    RenderFormat format;
    std::unique_ptr<RenderCommandList> list;
    std::unique_ptr<RenderTexture> target;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    std::unique_ptr<RenderBuffer> readback;
    std::vector<PixelCompositor::Retention> retained;
    std::vector<unsigned char> expected;
    Draw(RenderDevice& d, RenderCommandQueue& q, uint32_t width, uint32_t height, RenderFormat f)
        : w(width), h(height), row((width + 63U) & ~63U), format(f), list(q.createCommandList()),
          target(d.createTexture(RenderTextureDesc::ColorTarget(w, h, f))),
          readback(d.createBuffer(RenderBufferDesc::ReadbackBuffer(size_t(row) * h * 4))) {
        check(bool(list) && bool(target) && bool(readback), "GPU fixture allocation failed");
        const RenderTexture* attachment = target.get();
        framebuffer = d.createFramebuffer(RenderFramebufferDesc(&attachment, 1));
        check(bool(framebuffer), "Framebuffer allocation failed");
        expected.resize(size_t(w) * h * 4);
        for (size_t i = 0; i < expected.size(); i += 4) {
            expected[i] = 17; expected[i+1] = 43; expected[i+2] = 89; expected[i+3] = 113;
        }
        list->begin();
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target.get(), RenderTextureLayout::COLOR_WRITE));
        list->setFramebuffer(framebuffer.get());
        list->clearColor(0, RenderColor(17.f/255, 43.f/255, 89.f/255, 113.f/255));
    }
    void blend(const Bgra8Surface& image) {
        for (size_t i = 0; i < expected.size(); i += 4) for (size_t c = 0; c < 4; ++c) {
            const auto source = image.pixels[i + (c == 0 ? 2 : c == 2 ? 0 : c)];
            expected[i+c] = static_cast<unsigned char>(std::lround(source + expected[i+c] * (255.0-image.pixels[i+3])/255.0));
        }
    }
    void finish() {
        list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(target.get(), RenderTextureLayout::COPY_SOURCE));
#if defined(__APPLE__)
        // The pinned Metal backend lacks texture->buffer in copyTextureRegion.
        // Readback is test/platform glue only; the compositor itself has no cast.
        auto* command = static_cast<MetalCommandList*>(list.get());
        command->checkActiveBlitEncoder();
        command->activeBlitEncoder->copyFromTexture(static_cast<MetalTexture*>(target.get())->mtl,
            0, 0, MTL::Origin(0,0,0), MTL::Size(w,h,1), static_cast<MetalBuffer*>(readback.get())->mtl,
            0, size_t(row)*4, size_t(row)*h*4);
        command->endActiveBlitEncoder();
#else
        list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, w, h, 1, row),
                                RenderTextureCopyLocation::Subresource(target.get()));
#endif
        list->end();
    }
    void verify() {
        const auto* bytes = static_cast<const unsigned char*>(readback->map());
        check(bytes != nullptr, "Readback map failed");
        size_t mismatches = 0;
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) for (unsigned c = 0; c < 4; ++c) {
            const unsigned src = format == RenderFormat::B8G8R8A8_UNORM ? (c == 0 ? 2 : c == 2 ? 0 : c) : c;
            const int actual = bytes[(size_t(y)*row+x)*4+src];
            const int wanted = expected[(size_t(y)*w+x)*4+c];
            if (std::abs(actual-wanted) > 1) {
                if (mismatches < 4) std::cerr << "pixel " << x << ',' << y << " channel " << c << " actual=" << actual << " expected=" << wanted << '\n';
                ++mismatches;
            }
        }
        readback->unmap();
        check(mismatches == 0, "GPU pixel, alpha, orientation or row-pitch mismatch");
        retained.clear(); // Only after the fence and readback.
    }
};
void exercise(RenderDevice& device, const PixelShaders& shaders, uint32_t w, uint32_t h, RenderFormat f) {
    auto queue = device.createCommandQueue(RenderCommandListType::DIRECT);
    auto fence = device.createCommandFence();
    check(bool(queue) && bool(fence), "Queue/fence allocation failed");
    auto compositor = std::make_unique<PixelCompositor>(device, shaders);
    auto first = pattern(w,h,1), second = pattern(w,h,2);
    const auto alternate = f == RenderFormat::B8G8R8A8_UNORM ? RenderFormat::R8G8B8A8_UNORM : RenderFormat::B8G8R8A8_UNORM;
    Draw a(device,*queue,w,h,f), b(device,*queue,w,h,alternate), c(device,*queue,w,h,f);
    auto old_image = compositor->upload(*a.list, first);
    for (const auto bad : {RenderFormat::UNKNOWN, RenderFormat::R16G16B16A16_FLOAT, RenderFormat::BC7_UNORM_SRGB})
        rejects([&] { compositor->draw(*a.list, *a.framebuffer, bad, old_image); });
    rejects([&] { compositor->draw(*a.list, *a.framebuffer, f, {}); });
    auto malformed = first; malformed.pixels.pop_back();
    rejects([&] { compositor->upload(*a.list, malformed); });
    {
        PixelCompositor other(device, shaders);
        rejects([&] { other.draw(*a.list, *a.framebuffer, f, old_image); });
        auto wrong = device.createTexture(RenderTextureDesc::ColorTarget(w+1,h,f));
        check(bool(wrong), "Mismatch fixture allocation failed");
        const RenderTexture* attachment = wrong.get();
        auto wrong_fb = device.createFramebuffer(RenderFramebufferDesc(&attachment,1));
        check(bool(wrong_fb), "Mismatch framebuffer allocation failed");
        rejects([&] { compositor->draw(*a.list, *wrong_fb, f, old_image); });
    }
    a.retained.push_back(compositor->draw(*a.list,*a.framebuffer,f,old_image)); a.blend(first);
    a.finish();
    auto new_image = compositor->upload(*b.list, second);
    b.retained.push_back(compositor->draw(*b.list,*b.framebuffer,alternate,new_image)); b.blend(second);
    b.finish();
    // Record the old cached texture AFTER a new upload; neither texture nor its
    // descriptor may have been overwritten. Two layers exercise alpha blending.
    c.retained.push_back(compositor->draw(*c.list,*c.framebuffer,f,old_image)); c.blend(first);
    c.retained.push_back(compositor->draw(*c.list,*c.framebuffer,f,new_image)); c.blend(second);
    c.finish();
    const std::weak_ptr<const void> old_ticket = a.retained.front(), new_ticket = b.retained.front();
    // Completion tickets must retain all GPU resources without the cache/owner.
    old_image.reset(); new_image.reset(); compositor.reset();
    check(!old_ticket.expired() && !new_ticket.expired(), "Resources released before completion");
    const RenderCommandList* lists[] = {a.list.get(),b.list.get(),c.list.get()};
    queue->executeCommandLists(lists,3,nullptr,0,nullptr,0,fence.get());
    queue->waitForCommandFence(fence.get());
    a.verify(); b.verify(); c.verify();
    check(old_ticket.expired() && new_ticket.expired(), "Completed image resources leaked");
}
}
int main() {
    try {
#if defined(__APPLE__)
        auto* pool = NS::AutoreleasePool::alloc()->init();
        std::unique_ptr<RenderInterface> api = std::make_unique<MetalInterface>();
        std::cout << "Backend: Metal\n";
#elif defined(_WIN32)
        std::unique_ptr<RenderInterface> api = std::make_unique<D3D12Interface>();
        std::cout << "Backend: D3D12\n";
#else
        std::unique_ptr<RenderInterface> api = std::make_unique<VulkanInterface>();
        std::cout << "Backend: Vulkan\n";
#endif
        for (const auto& name : api->getDeviceNames()) std::cout << "Enumerated device: " << name << '\n';
        auto device = api->createDevice();
        check(bool(device), "No GPU/software device available; this is not a passing test");
        auto shaders = embedded_pixel_shaders(api->getCapabilities().shaderFormat);
        for (auto dimensions : {std::pair{1U,1U},std::pair{3U,5U},std::pair{65U,17U},std::pair{321U,241U},std::pair{800U,600U},std::pair{1100U,760U}})
            for (auto format : {RenderFormat::B8G8R8A8_UNORM, RenderFormat::R8G8B8A8_UNORM})
                exercise(*device,shaders,dimensions.first,dimensions.second,format);
        std::cout << "pixel compositor: " << checks << " checks; 36 offscreen readbacks passed\n";
        device.reset(); api.reset();
#if defined(__APPLE__)
        pool->release();
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
