#include "post_filter.hpp"
#include "presentation_settings.hpp"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef __APPLE__
#include "plume_metal.h"
#endif
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace srw64::post_filter {
namespace {

// The few librashader entry points we call (librashader.h, ABI 2), by name at run time.
// Handles are pointers; Metal objects pass as their Objective-C pointers.
using Error = void*;
using Preset = void*;
using Chain = void*;
struct Viewport {float x, y; uint32_t width, height;};
struct Api {
    void* library = nullptr;
    Error (*preset_create)(const char*, Preset*) = nullptr;
    Error (*mtl_create)(Preset*, void* queue, const void* options, Chain*) = nullptr;
    Error (*mtl_frame)(Chain*, void* commands, size_t frame, void* image, void* output, const Viewport*, const float* mvp, const void* options) = nullptr;
    Error (*mtl_free)(Chain*) = nullptr;
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
bool metal = false;
std::filesystem::path downscale_preset;
void* queue = nullptr;            // the MTLCommandQueue RT64 presents on
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
// A preset becomes a chain; the preset itself is consumed by the chain's creation.
std::string compile(const std::string& path, Chain& out) {
    Preset preset = nullptr;
    if (auto error = take(api.preset_create(path.c_str(), &preset)); !error.empty()) return error;
    return take(api.mtl_create(&preset, queue, nullptr, &out));
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
    }
    for (const auto& path : candidates) {
        void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) continue;
        const auto symbol = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
            return function != nullptr;
        };
        const bool complete = symbol(api.preset_create, "libra_preset_create") && symbol(api.mtl_create, "libra_mtl_filter_chain_create") &&
            symbol(api.mtl_frame, "libra_mtl_filter_chain_frame") && symbol(api.mtl_free, "libra_mtl_filter_chain_free") &&
            symbol(api.error_write, "libra_error_write") && symbol(api.error_free_string, "libra_error_free_string") &&
            symbol(api.error_free, "libra_error_free") && symbol(api.abi_version, "libra_instance_abi_version");
        if (complete && api.abi_version() == kAbi) {
            api.library = library;
            fprintf(stderr, "SRW64_FILTER library=%s\n", path.c_str());
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
        if (++it->second > 4) {api.mtl_free(&it->first); it = retired.erase(it);}
        else ++it;
    }
}

#ifdef __APPLE__
MTL::Texture* full = nullptr;
MTL::Texture* small = nullptr;
MTL::Texture* ensure(MTL::Device* device, MTL::Texture*& texture, unsigned w, unsigned h, MTL::PixelFormat format) {
    if (texture && texture->width() == w && texture->height() == h && texture->pixelFormat() == format) return texture;
    if (texture) texture->release();
    auto* desc = MTL::TextureDescriptor::texture2DDescriptor(format, w, h, false);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    desc->setStorageMode(MTL::StorageModePrivate);
    texture = device->newTexture(desc);
    return texture;
}
#endif
}  // namespace

Status status() {
    std::lock_guard lock(mutex);
    return state;
}

void init(bool is_metal, const std::filesystem::path& scratch) {
    metal = is_metal;
    if (!metal || !load_library()) return;
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
#ifdef __APPLE__
    if (!metal || !api.library) return;
    auto* commands = static_cast<plume::MetalCommandList*>(list);
    if (!queue) {
        queue = commands->queue->mtl;
        // Our passthrough compiles once, here, where the queue is known.
        if (auto error = compile(downscale_preset.string(), downscale); !error.empty()) {
            fprintf(stderr, "SRW64_FILTER passthrough error=%s\n", error.c_str());
            downscale = nullptr;
        }
    }
    follow_settings();
    free_retired();
    Chain current;
    {
        std::lock_guard lock(mutex);
        current = chain;
    }
    if (!current) return;
    MTL::Texture* target = static_cast<const plume::MetalFramebuffer*>(framebuffer)->colorAttachments[0].getTexture();
    x = std::max(0, x);
    y = std::max(0, y);
    w = std::min<int>(w, int(target->width()) - x);
    h = std::min<int>(h, int(target->height()) - y);
    if (w <= 0 || h <= 0) return;
    MTL::Device* device = commands->queue->device->mtl;
    commands->endActiveRenderEncoder();
    commands->endActiveBlitEncoder();
    commands->endActiveComputeEncoder();
    commands->endActiveResolveTextureComputeEncoder();
    auto* copy = ensure(device, full, unsigned(w), unsigned(h), target->pixelFormat());
    auto* blit = commands->mtl->blitCommandEncoder();
    blit->copyFromTexture(target, 0, 0, MTL::Origin(x, y, 0), MTL::Size(w, h, 1), copy, 0, 0, MTL::Origin(0, 0, 0));
    blit->endEncoding();
    MTL::Texture* source = copy;
    ++frames;
    const unsigned scale = settings::filter_scale();
    if (scale > 0 && downscale) {
        const unsigned sh = 240 * scale, sw = std::max(1u, unsigned(picture_width * scale + 0.5f));
        if (sh < unsigned(h)) {
            auto* reduced = ensure(device, small, sw, sh, target->pixelFormat());
            const Viewport whole{0, 0, sw, sh};
            if (auto error = take(api.mtl_frame(&downscale, commands->mtl, frames, copy, reduced, &whole, nullptr, nullptr)); error.empty())
                source = reduced;
        }
    }
    const Viewport out{float(x), float(y), uint32_t(w), uint32_t(h)};
    if (auto error = take(api.mtl_frame(&current, commands->mtl, frames, source, target, &out, nullptr, nullptr)); !error.empty()) {
        fprintf(stderr, "SRW64_FILTER frame error=%s\n", error.c_str());
        std::lock_guard lock(mutex);
        state.error = error;
        retired.push_back({chain, 0});
        chain = nullptr;
        drawn.clear();
        state.preset.clear();
    }
#else
    (void)list; (void)framebuffer; (void)x; (void)y; (void)w; (void)h; (void)picture_width;
#endif
}

void shutdown() {
    if (worker.joinable()) worker.join();
    if (!api.library) return;
    std::lock_guard lock(mutex);
    for (auto& [old, age] : retired) api.mtl_free(&old);
    retired.clear();
    if (chain) api.mtl_free(&chain);
    if (pending.chain) api.mtl_free(&pending.chain);
    if (downscale) api.mtl_free(&downscale);
#ifdef __APPLE__
    if (full) full->release();
    if (small) small->release();
    full = small = nullptr;
#endif
    queue = nullptr;
}

}  // namespace srw64::post_filter
