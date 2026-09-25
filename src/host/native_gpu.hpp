#pragma once
// The host-drawn HD layers on any Plume backend: Metal, Vulkan or D3D12
// (docs/design/three-platform-port.md, X1). Shaders are HLSL in src/host/shaders
// (bindings in NativeGpu.hlsli), embedded at build time by cmake/NativeGpu.cmake.
//
// Everything here runs on RT64's workload thread, inside the native mesh hook, and
// relies on RT64 waiting for each workload's GPU work before recording the next
// (rt64_workload_queue.cpp): data and uploads recorded for one workload are done
// before a later workload runs.
#include "plume_render_interface.h"
#include "rhi/rt64_render_hooks.h"
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace srw64::gpu {
enum class Blend : uint8_t {
    opaque,         // overwrite
    premultiplied,  // ONE, ONE_MINUS_SRC_ALPHA
    straight,       // SRC_ALPHA, ONE_MINUS_SRC_ALPHA; alpha ONE, ONE_MINUS_SRC_ALPHA
};

struct State {
    Blend blend = Blend::opaque;
    bool depth_test = false, depth_write = false;  // the test compares LESS_EQUAL
    plume::RenderCullMode cull = plume::RenderCullMode::NONE;
    bool counter_clockwise = false;                 // front faces
    plume::RenderPrimitiveTopology topology = plume::RenderPrimitiveTopology::TRIANGLE_STRIP;
    auto operator<=>(const State&) const = default;
};

struct Sampler {
    bool linear = true, mipmaps = false, repeat = false;
};

// Called from the render hooks' init and deinit.
void init(plume::RenderInterface* rhi, plume::RenderDevice* device);
void shutdown();
bool ready();

// Stores one draw's constants in the shared ring and returns the index of its first
// float4, for NativeParams.data. Returns false when there is no device.
uint32_t push_data(const void* data, size_t bytes);

// Keeps a GPU object alive until the work recorded with it has finished.
void retire(std::shared_ptr<void> object);

// A texture in shader-read layout, filled on first use from CPU levels.
class Texture {
public:
    // RGBA8_UNORM (4 bytes per pixel) or R8_UINT (1); levels[0] is width x height,
    // each further level half the size (at least 1), rows tightly packed.
    Texture(uint32_t width, uint32_t height, plume::RenderFormat format, std::vector<std::vector<uint8_t>> levels);
    ~Texture();
    // Records the upload the first time; false without a device.
    bool upload(plume::RenderCommandList* list);
    plume::RenderTexture* get() const { return texture.get(); }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
private:
    uint32_t width_, height_;
    plume::RenderFormat format;
    std::vector<std::vector<uint8_t>> levels;
    std::unique_ptr<plume::RenderTexture> texture;
};

// One shader pair with its own textures (t0.. in space1) and immutable samplers
// (bound after the textures). Pipelines are made per RT64 target and State.
class Program {
public:
    Program(const char* name, uint32_t textures, std::vector<Sampler> samplers);
    ~Program();
    // A descriptor set holding these textures, in order (all must be uploaded).
    std::unique_ptr<plume::RenderDescriptorSet> bind_textures(std::span<Texture* const> textures) const;
    // Sets pipeline, descriptors, push constants, viewport and scissor for a draw on
    // RT64's scene target; issue the draw call after. False if the target is unusable.
    bool begin(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call,
               const State& state, plume::RenderDescriptorSet* textures, uint32_t data, uint32_t flags = 0) const;
private:
    struct Shaders;
    const char* name;
    uint32_t texture_count;
    std::vector<std::unique_ptr<plume::RenderSampler>> samplers;
    std::unique_ptr<Shaders> shaders;
    plume::RenderDescriptorSetDesc texture_set_desc{};
    std::vector<plume::RenderDescriptorRange> texture_ranges;
    std::vector<const plume::RenderSampler*> sampler_pointers;
    std::unique_ptr<plume::RenderPipelineLayout> layout;
    using Key = std::tuple<plume::RenderFormat, plume::RenderFormat, plume::RenderSampleCounts, State>;
    mutable std::map<Key, std::unique_ptr<plume::RenderPipeline>> pipelines;
    void create() const;
};
}
