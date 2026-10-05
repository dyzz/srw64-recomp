#include "post_filter.hpp"
#include "presentation_settings.hpp"
#include "rt64_render_hooks.h"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#include "plume_vulkan.h"
#ifdef __APPLE__
#include "plume_metal.h"
#endif
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace srw64::post_filter {
namespace {

// The few librashader entry points we call (librashader.h, ABI 2), by name at run time.
// Handles are pointers; Metal objects pass as their Objective-C pointers, and the Vulkan
// structs below match libra_device_vk_t and libra_image_vk_t, which go by value.
using Error = void*;
using Preset = void*;
using Chain = void*;
struct Viewport {float x, y; uint32_t width, height;};
struct DeviceVk {VkPhysicalDevice physical; VkInstance instance; VkDevice device; VkQueue queue; PFN_vkGetInstanceProcAddr entry;};
struct ImageVk {VkImage handle; VkFormat format; uint32_t width, height;};
struct Api {
    void* library = nullptr;
    Error (*preset_create)(const char*, Preset*) = nullptr;
    Error (*mtl_create)(Preset*, void* queue, const void* options, Chain*) = nullptr;
    Error (*mtl_frame)(Chain*, void* commands, size_t frame, void* image, void* output, const Viewport*, const float* mvp, const void* options) = nullptr;
    Error (*mtl_free)(Chain*) = nullptr;
    Error (*vk_create_deferred)(Preset*, DeviceVk, VkCommandBuffer, const void* options, Chain*) = nullptr;
    Error (*vk_frame)(Chain*, VkCommandBuffer, size_t frame, ImageVk image, ImageVk output, const Viewport*, const float* mvp, const void* options) = nullptr;
    Error (*vk_free)(Chain*) = nullptr;
    int32_t (*error_write)(Error, char**) = nullptr;
    int32_t (*error_free_string)(char**) = nullptr;
    int32_t (*error_free)(Error*) = nullptr;
    size_t (*abi_version)() = nullptr;
} api;
constexpr size_t kAbi = 2;

// Ours, written beside the run: one linear pass with the source's mipmaps, which brings
// the window-sized picture down to the lines the player chose before their preset runs.
constexpr const char* kPassthrough = R"(#version 450
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;
#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }
#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
void main() { FragColor = vec4(texture(Source, vTexCoord).rgb, 1.0); }
)";
constexpr const char* kPassthroughPreset =
    "shaders = 1\nshader0 = srw64-passthrough.slang\nfilter_linear0 = true\nmipmap_input0 = true\nscale_type0 = viewport\n";

std::mutex mutex;
Status state;
Backend backend = Backend::none;
std::filesystem::path downscale_preset;
bool bound = false;               // the device and queue below are known
void* metal_queue = nullptr;      // the MTLCommandQueue RT64 presents on
struct {
    plume::VulkanDevice* device = nullptr;
    plume::VulkanCommandQueue* queue = nullptr;
    DeviceVk handles{};
} vulkan;
Chain chain = nullptr, downscale = nullptr;
std::string wanted, drawn;        // the preset asked for and the one in `chain`
struct Compiled {Chain chain = nullptr; std::string path, error; bool done = false;};
Compiled pending;                 // the worker's result, taken by the render thread
std::thread worker;
std::vector<std::pair<Chain, unsigned>> retired;   // freed a few presents later
size_t frames = 0;

std::string take(Error error) {
    if (!error) return {};
    char* text = nullptr;
    std::string out = "librashader error";
    if (api.error_write && api.error_write(error, &text) == 0 && text) out = text;
    if (text) api.error_free_string(&text);
    api.error_free(&error);
    return out;
}
void free_chain(Chain& handle) {
    if (!handle) return;
    if (backend == Backend::metal) api.mtl_free(&handle);
    else api.vk_free(&handle);
    handle = nullptr;
}

// Vulkan: the chain's uploads go into a command buffer of our own, submitted under the
// queue's lock (plume's VulkanQueue::mutex), so a worker never races RT64's submissions.
std::string create_vulkan(Preset& preset, Chain& out) {
    const VkDevice device = vulkan.handles.device;
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = vulkan.queue->familyIndex;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS) return "vkCreateCommandPool failed";
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    std::string error;
    if (vkAllocateCommandBuffers(device, &allocate, &commands) != VK_SUCCESS) error = "vkAllocateCommandBuffers failed";
    if (error.empty()) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        error = take(api.vk_create_deferred(&preset, vulkan.handles, commands, nullptr, &out));
        vkEndCommandBuffer(commands);
    }
    if (error.empty()) {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(device, &fence_info, nullptr, &fence);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        VkResult result;
        {
            std::scoped_lock lock(*vulkan.queue->queue->mutex);
            result = vkQueueSubmit(vulkan.handles.queue, 1, &submit, fence);
        }
        if (result == VK_SUCCESS) vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        else error = "vkQueueSubmit failed";
        vkDestroyFence(device, fence, nullptr);
        if (!error.empty()) free_chain(out);
    }
    vkDestroyCommandPool(device, pool, nullptr);
    return error;
}
// A preset becomes a chain; the preset itself is consumed by the chain's creation.
std::string compile(const std::string& path, Chain& out) {
    Preset preset = nullptr;
    if (auto error = take(api.preset_create(path.c_str(), &preset)); !error.empty()) return error;
    if (backend == Backend::metal) return take(api.mtl_create(&preset, metal_queue, nullptr, &out));
    return create_vulkan(preset, out);
}

bool load_library() {
#ifndef _WIN32
    std::vector<std::string> candidates;
    if (const char* path = std::getenv("SRW64_LIBRASHADER"); path && *path) candidates.push_back(path);
    if (char* base = SDL_GetBasePath()) {
        const std::string dir(base);
        SDL_free(base);
        candidates.push_back(dir + "librashader.dylib");
        candidates.push_back(dir + "../MacOS/librashader.dylib");   // the app bundle (package_macos.py)
        candidates.push_back(dir + "../Frameworks/librashader.dylib");
        candidates.push_back(dir + "librashader.so");
        candidates.push_back(dir + "lib/librashader.so");            // the Linux tarball (build_linux.py)
    }
    for (const auto& path : candidates) {
        void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) continue;
        const auto symbol = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
            return function != nullptr;
        };
        bool complete = symbol(api.preset_create, "libra_preset_create") &&
            symbol(api.error_write, "libra_error_write") && symbol(api.error_free_string, "libra_error_free_string") &&
            symbol(api.error_free, "libra_error_free") && symbol(api.abi_version, "libra_instance_abi_version");
        if (backend == Backend::metal)
            complete = complete && symbol(api.mtl_create, "libra_mtl_filter_chain_create") &&
                symbol(api.mtl_frame, "libra_mtl_filter_chain_frame") && symbol(api.mtl_free, "libra_mtl_filter_chain_free");
        else
            complete = complete && symbol(api.vk_create_deferred, "libra_vk_filter_chain_create_deferred") &&
                symbol(api.vk_frame, "libra_vk_filter_chain_frame") && symbol(api.vk_free, "libra_vk_filter_chain_free");
        if (complete && api.abi_version() == kAbi) {
            api.library = library;
            fprintf(stderr, "SRW64_FILTER library=%s backend=%s\n", path.c_str(), backend == Backend::metal ? "metal" : "vulkan");
            return true;
        }
        fprintf(stderr, "SRW64_FILTER rejected=%s complete=%d\n", path.c_str(), int(complete));
        dlclose(library);
        api = {};
    }
#endif
    return false;
}

// Render thread: starts compiling a newly chosen preset, and takes a finished one.
void follow_settings() {
    const auto path = settings::filter();
    std::lock_guard lock(mutex);
    if (pending.done) {
        if (worker.joinable()) worker.join();
        if (pending.path == wanted) {
            if (chain) retired.push_back({chain, 0});
            chain = pending.chain;
            drawn = pending.chain ? pending.path : std::string();
            state.preset = drawn;
            state.error = pending.error;
        } else if (pending.chain) {
            retired.push_back({pending.chain, 0});
        }
        if (!pending.error.empty()) fprintf(stderr, "SRW64_FILTER failed=%s error=%s\n", pending.path.c_str(), pending.error.c_str());
        pending = {};
        state.loading = false;
    }
    if (path == wanted || state.loading) return;
    wanted = path;
    if (path.empty()) {
        if (chain) retired.push_back({chain, 0});
        chain = nullptr;
        drawn.clear();
        state.preset.clear();
        state.error.clear();
        return;
    }
    state.loading = true;
    worker = std::thread([path] {
        Compiled result;
        result.path = path;
        result.error = compile(path, result.chain);
        if (!result.error.empty()) result.chain = nullptr;
        result.done = true;
        std::lock_guard lock(mutex);
        pending = std::move(result);
    });
}
void free_retired() {
    std::lock_guard lock(mutex);
    for (auto it = retired.begin(); it != retired.end();) {
        if (++it->second > 4) {free_chain(it->first); it = retired.erase(it);}
        else ++it;
    }
}
// Once per run, where the device and queue are first seen; our passthrough compiles here.
void bind_once() {
    if (bound) return;
    bound = true;
    if (auto error = compile(downscale_preset.string(), downscale); !error.empty()) {
        fprintf(stderr, "SRW64_FILTER passthrough error=%s\n", error.c_str());
        downscale = nullptr;
    }
}
Chain current_chain() {
    std::lock_guard lock(mutex);
    return chain;
}
void dropped(const std::string& error) {
    fprintf(stderr, "SRW64_FILTER frame error=%s\n", error.c_str());
    std::lock_guard lock(mutex);
    state.error = error;
    if (chain) retired.push_back({chain, 0});
    chain = nullptr;
    drawn.clear();
    state.preset.clear();
}
// The height the preset reads, and the reduced picture's size for it; none when the
// player chose the window's own pixels or the window is no taller than that.
bool reduced_size(float picture_width, int h, unsigned& sw, unsigned& sh) {
    const unsigned scale = settings::filter_scale();
    if (scale == 0 || !downscale) return false;
    sh = 240 * scale;
    sw = std::max(1u, unsigned(picture_width * scale + 0.5f));
    return sh < unsigned(h);
}

#ifdef __APPLE__
MTL::Texture* metal_full = nullptr;
MTL::Texture* metal_small = nullptr;
MTL::Texture* ensure(MTL::Device* device, MTL::Texture*& texture, unsigned w, unsigned h, MTL::PixelFormat format) {
    if (texture && texture->width() == w && texture->height() == h && texture->pixelFormat() == format) return texture;
    if (texture) texture->release();
    auto* desc = MTL::TextureDescriptor::texture2DDescriptor(format, w, h, false);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    desc->setStorageMode(MTL::StorageModePrivate);
    texture = device->newTexture(desc);
    return texture;
}
void apply_metal(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, int x, int y, int w, int h, float picture_width) {
    auto* commands = static_cast<plume::MetalCommandList*>(list);
    if (!metal_queue) metal_queue = commands->queue->mtl;
    bind_once();
    follow_settings();
    free_retired();
    Chain current = current_chain();
    if (!current) return;
    MTL::Texture* target = static_cast<const plume::MetalFramebuffer*>(framebuffer)->colorAttachments[0].getTexture();
    w = std::min<int>(w, int(target->width()) - x);
    h = std::min<int>(h, int(target->height()) - y);
    if (w <= 0 || h <= 0) return;
    MTL::Device* device = commands->queue->device->mtl;
    commands->endActiveRenderEncoder();
    commands->endActiveBlitEncoder();
    commands->endActiveComputeEncoder();
    commands->endActiveResolveTextureComputeEncoder();
    auto* copy = ensure(device, metal_full, unsigned(w), unsigned(h), target->pixelFormat());
    auto* blit = commands->mtl->blitCommandEncoder();
    blit->copyFromTexture(target, 0, 0, MTL::Origin(x, y, 0), MTL::Size(w, h, 1), copy, 0, 0, MTL::Origin(0, 0, 0));
    blit->endEncoding();
    MTL::Texture* source = copy;
    ++frames;
    if (unsigned sw, sh; reduced_size(picture_width, h, sw, sh)) {
        auto* reduced = ensure(device, metal_small, sw, sh, target->pixelFormat());
        const Viewport whole{0, 0, sw, sh};
        if (take(api.mtl_frame(&downscale, commands->mtl, frames, copy, reduced, &whole, nullptr, nullptr)).empty()) source = reduced;
    }
    const Viewport out{float(x), float(y), uint32_t(w), uint32_t(h)};
    if (auto error = take(api.mtl_frame(&current, commands->mtl, frames, source, target, &out, nullptr, nullptr)); !error.empty()) dropped(error);
}
#endif

std::unique_ptr<plume::RenderTexture> vulkan_full, vulkan_small;
plume::RenderTexture* ensure(std::unique_ptr<plume::RenderTexture>& texture, unsigned w, unsigned h, plume::RenderFormat format) {
    if (texture) {
        const auto& desc = static_cast<plume::VulkanTexture*>(texture.get())->desc;
        if (desc.width == w && desc.height == h && desc.format == format) return texture.get();
    }
    texture = vulkan.device->createTexture(plume::RenderTextureDesc::ColorTarget(w, h, format));
    return texture.get();
}
// The swapchain's images carry no format of their own in plume; RT64 makes them B8G8R8A8
// (R8G8B8A8 on Android, rt64_android_patches.py).
plume::RenderFormat format_of(plume::RenderTexture* texture) {
    const auto format = static_cast<plume::VulkanTexture*>(texture)->desc.format;
    if (format != plume::RenderFormat::UNKNOWN) return format;
#ifdef __ANDROID__
    return plume::RenderFormat::R8G8B8A8_UNORM;
#else
    return plume::RenderFormat::B8G8R8A8_UNORM;
#endif
}
ImageVk image_of(plume::RenderTexture* texture) {
    auto* vk = static_cast<plume::VulkanTexture*>(texture);
    VkFormat format = vk->imageFormat;
    if (format == VK_FORMAT_UNDEFINED)
        format = format_of(texture) == plume::RenderFormat::R8G8B8A8_UNORM ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
    return {vk->vk, format, uint32_t(vk->desc.width), uint32_t(vk->desc.height)};
}
void apply_vulkan(plume::RenderCommandList* list, int x, int y, int w, int h, float picture_width) {
    using namespace plume;
    auto* commands = static_cast<VulkanCommandList*>(list);
    if (!vulkan.device) {
        vulkan.device = commands->queue->device;
        vulkan.queue = commands->queue;
        vulkan.handles = {vulkan.device->physicalDevice, vulkan.device->renderInterface->instance, vulkan.device->vk,
                          vulkan.queue->queue->vk, vkGetInstanceProcAddr};
    }
    bind_once();
    follow_settings();
    free_retired();
    Chain current = current_chain();
    if (!current) return;
    // RT64's swapchain image, in COLOR_WRITE (rt64_present_queue.cpp, as the capture uses it).
    RenderTexture* target = RT64::GetRenderHookSwapChainTexture();
    if (!target) return;
    const auto& target_desc = static_cast<VulkanTexture*>(target)->desc;
    w = std::min<int>(w, int(target_desc.width) - x);
    h = std::min<int>(h, int(target_desc.height) - y);
    if (w <= 0 || h <= 0) return;
    commands->endActiveRenderPass();
    RenderTexture* copy = ensure(vulkan_full, unsigned(w), unsigned(h), format_of(target));
    list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(target, RenderTextureLayout::COPY_SOURCE));
    list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(copy, RenderTextureLayout::COPY_DEST));
    const RenderBox box(x, y, x + w, y + h);
    list->copyTextureRegion(RenderTextureCopyLocation::Subresource(copy), RenderTextureCopyLocation::Subresource(target), 0, 0, 0, &box);
    list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(copy, RenderTextureLayout::SHADER_READ));
    list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target, RenderTextureLayout::COLOR_WRITE));
    RenderTexture* source = copy;
    ++frames;
    if (unsigned sw, sh; reduced_size(picture_width, h, sw, sh)) {
        RenderTexture* reduced = ensure(vulkan_small, sw, sh, format_of(target));
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(reduced, RenderTextureLayout::COLOR_WRITE));
        const Viewport whole{0, 0, sw, sh};
        if (take(api.vk_frame(&downscale, commands->vk, frames, image_of(copy), image_of(reduced), &whole, nullptr, nullptr)).empty()) {
            list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(reduced, RenderTextureLayout::SHADER_READ));
            source = reduced;
        }
    }
    const Viewport out{float(x), float(y), uint32_t(w), uint32_t(h)};
    if (auto error = take(api.vk_frame(&current, commands->vk, frames, image_of(source), image_of(target), &out, nullptr, nullptr)); !error.empty())
        dropped(error);
    // librashader bound its own pipelines and descriptor sets; plume binds its own again.
    commands->activeGraphicsPipelineLayout = nullptr;
    commands->activeComputePipelineLayout = nullptr;
    commands->targetFramebuffer = nullptr;
}
}  // namespace

Status status() {
    std::lock_guard lock(mutex);
    return state;
}

bool active() {
    if (!api.library) return false;
    if (!settings::filter().empty()) return true;
    // Still holding the last preset: one more pass lets apply() see it is gone.
    std::lock_guard lock(mutex);
    return chain || state.loading || !drawn.empty();
}

void init(Backend chosen, const std::filesystem::path& scratch) {
    backend = chosen;
    if (backend == Backend::none || !load_library()) return;
    std::error_code error;
    const auto folder = scratch / "filter-passthrough";
    std::filesystem::create_directories(folder, error);
    std::ofstream(folder / "srw64-passthrough.slang") << kPassthrough;
    std::ofstream(folder / "srw64-passthrough.slangp") << kPassthroughPreset;
    downscale_preset = folder / "srw64-passthrough.slangp";
    std::lock_guard lock(mutex);
    state.available = true;
}

void apply(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, int x, int y, int w, int h, float picture_width) {
    if (!api.library) return;
    x = std::max(0, x);
    y = std::max(0, y);
#ifdef __APPLE__
    if (backend == Backend::metal) {apply_metal(list, framebuffer, x, y, w, h, picture_width); return;}
#endif
    (void)framebuffer;
    if (backend == Backend::vulkan) apply_vulkan(list, x, y, w, h, picture_width);
}

void shutdown() {
    if (worker.joinable()) worker.join();
    if (!api.library) return;
    std::lock_guard lock(mutex);
    for (auto& [old, age] : retired) free_chain(old);
    retired.clear();
    free_chain(chain);
    free_chain(pending.chain);
    free_chain(downscale);
#ifdef __APPLE__
    if (metal_full) metal_full->release();
    if (metal_small) metal_small->release();
    metal_full = metal_small = nullptr;
#endif
    vulkan_full.reset();
    vulkan_small.reset();
    vulkan = {};
    metal_queue = nullptr;
    bound = false;
}

}  // namespace srw64::post_filter
