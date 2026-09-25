#define HLSL_CPU
#include "native_gpu.hpp"
#include "plume_render_interface_builders.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string_view>

// Embedded shader binaries (cmake/NativeGpu.cmake); add each new program here.
#if defined(__APPLE__)
#include "HdMapVS.hlsl.metal.h"
#include "HdMapPS.hlsl.metal.h"
#include "HdBackgroundVS.hlsl.metal.h"
#include "HdBackgroundPS.hlsl.metal.h"
#else
#include "HdMapVS.hlsl.spirv.h"
#include "HdMapPS.hlsl.spirv.h"
#include "HdBackgroundVS.hlsl.spirv.h"
#include "HdBackgroundPS.hlsl.spirv.h"
#ifdef _WIN32
#include "HdMapVS.hlsl.dxil.h"
#include "HdMapPS.hlsl.dxil.h"
#include "HdBackgroundVS.hlsl.dxil.h"
#include "HdBackgroundPS.hlsl.dxil.h"
#endif
#endif

namespace srw64::gpu {
using namespace plume;
namespace {
using Bytes = std::span<const unsigned char>;
template<size_t N> Bytes bytes(const char (&value)[N]) { return {reinterpret_cast<const unsigned char*>(value), N}; }
struct Blobs { std::string_view name; Bytes vertex, pixel; RenderShaderFormat format; };

// Every embedded program, for each shader format this platform builds.
#define SRW64_NATIVE_PROGRAMS(X) X(HdMap) X(HdBackground)
std::vector<Blobs> embedded() {
    std::vector<Blobs> all;
#if defined(__APPLE__)
#define SRW64_MSL(name) all.push_back({#name, bytes(name##VSBlobMSL), bytes(name##PSBlobMSL), RenderShaderFormat::METAL});
    SRW64_NATIVE_PROGRAMS(SRW64_MSL)
#else
#define SRW64_SPIRV(name) all.push_back({#name, bytes(name##VSBlobSPIRV), bytes(name##PSBlobSPIRV), RenderShaderFormat::SPIRV});
    SRW64_NATIVE_PROGRAMS(SRW64_SPIRV)
#ifdef _WIN32
#define SRW64_DXIL(name) all.push_back({#name, bytes(name##VSBlobDXIL), bytes(name##PSBlobDXIL), RenderShaderFormat::DXIL});
    SRW64_NATIVE_PROGRAMS(SRW64_DXIL)
#endif
#endif
    return all;
}

template<class T> std::unique_ptr<T> require(std::unique_ptr<T> value, const char* what) {
    if (!value) throw std::runtime_error(what);
    return value;
}

// Per-draw constants: 64K float4 (1 MiB) in one persistently mapped upload buffer,
// read as StructuredBuffer<float4>. A layer draws a handful of times per workload, so
// a slot is reused only long after RT64 has waited for the workload that read it.
constexpr uint32_t kDataEntries = 1u << 16;
// Retired objects outlive the recording that used them by at least this long; RT64
// has waited for that workload long before.
constexpr auto kRetireDelay = std::chrono::seconds(2);

struct Context {
    RenderInterface* rhi{};
    RenderDevice* device{};
    RenderShaderFormat format{};
    std::unique_ptr<RenderBuffer> data;
    uint8_t* mapped{};
    uint32_t cursor = 0;
    std::unique_ptr<RenderDescriptorSet> data_set;
    RenderDescriptorSetBuilder data_builder;
    std::deque<std::pair<std::chrono::steady_clock::time_point, std::shared_ptr<void>>> retired;
    std::mutex mutex;
};
std::unique_ptr<Context> context;

void collect() {
    const auto now = std::chrono::steady_clock::now();
    while (!context->retired.empty() && now - context->retired.front().first > kRetireDelay) context->retired.pop_front();
}

uint32_t aligned(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }
}

void init(RenderInterface* rhi, RenderDevice* device) {
    context = std::make_unique<Context>();
    context->rhi = rhi;
    context->device = device;
    context->format = rhi->getCapabilities().shaderFormat;
    context->data = require(device->createBuffer(RenderBufferDesc::UploadBuffer(uint64_t(kDataEntries) * 16, RenderBufferFlag::STORAGE)),
                            "HD layer data buffer allocation failed");
    context->mapped = static_cast<uint8_t*>(context->data->map());
    if (!context->mapped) throw std::runtime_error("Cannot map the HD layer data buffer");
    context->data_builder.begin();
    const uint32_t slot = context->data_builder.addStructuredBuffer(1);
    context->data_builder.end();
    context->data_set = require(context->data_builder.create(device), "HD layer data descriptors failed");
    const RenderBufferStructuredView view(16);
    context->data_set->setBuffer(slot, context->data.get(), uint64_t(kDataEntries) * 16, &view);
}

void shutdown() {
    if (!context) return;
    context->retired.clear();
    context->data_set.reset();
    if (context->mapped) context->data->unmap();
    context.reset();
}

bool ready() { return context != nullptr; }

uint32_t push_data(const void* data, size_t bytes) {
    if (!context) return 0;
    std::lock_guard lock(context->mutex);
    const uint32_t entries = uint32_t((bytes + 15) / 16);
    if (entries == 0 || entries > kDataEntries / 4) throw std::runtime_error("HD layer draw data size out of range");
    if (context->cursor + entries > kDataEntries) context->cursor = 0;
    const uint32_t index = context->cursor;
    std::memcpy(context->mapped + size_t(index) * 16, data, bytes);
    context->cursor += entries;
    return index;
}

void retire(std::shared_ptr<void> object) {
    if (!context || !object) return;
    std::lock_guard lock(context->mutex);
    collect();
    context->retired.emplace_back(std::chrono::steady_clock::now(), std::move(object));
}

Texture::Texture(uint32_t width, uint32_t height, RenderFormat format, std::vector<std::vector<uint8_t>> levels)
    : width_(width), height_(height), format(format), levels(std::move(levels)) {
    if (format != RenderFormat::R8G8B8A8_UNORM && format != RenderFormat::R8_UINT)
        throw std::runtime_error("HD layer textures are RGBA8 or R8_UINT");
    if (this->levels.empty()) throw std::runtime_error("HD layer texture without pixels");
}

Texture::~Texture() {
    // A texture dropped while a recorded draw may still read it waits for that work.
    if (texture) retire(std::shared_ptr<RenderTexture>(texture.release()));
}

bool Texture::upload(RenderCommandList* list) {
    if (texture) return true;
    if (!context) return false;
    const uint32_t pixel = format == RenderFormat::R8_UINT ? 1 : 4;
    // D3D12 wants 256-byte rows and 512-byte subresource offsets; the others accept them.
    struct Level { uint32_t width, height, row_pixels; uint64_t offset; };
    std::vector<Level> layout;
    uint64_t total = 0;
    uint32_t w = width_, h = height_;
    for (size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].size() != size_t(w) * h * pixel) throw std::runtime_error("HD layer texture level has the wrong size");
        const uint32_t row_pixels = aligned(w * pixel, 256) / pixel;
        total = aligned(uint32_t(total), 512);
        layout.push_back({w, h, row_pixels, total});
        total += uint64_t(row_pixels) * pixel * h;
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    auto staging = std::shared_ptr<RenderBuffer>(require(context->device->createBuffer(RenderBufferDesc::UploadBuffer(total)),
                                                         "HD layer staging allocation failed"));
    auto* destination = static_cast<uint8_t*>(staging->map());
    if (!destination) throw std::runtime_error("Cannot map HD layer staging buffer");
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto& level = layout[i];
        for (uint32_t y = 0; y < level.height; ++y)
            std::memcpy(destination + level.offset + uint64_t(y) * level.row_pixels * pixel,
                        levels[i].data() + size_t(y) * level.width * pixel, size_t(level.width) * pixel);
    }
    staging->unmap();
    texture = require(context->device->createTexture(RenderTextureDesc::Texture2D(width_, height_, uint32_t(levels.size()), format)),
                      "HD layer texture allocation failed");
    list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.get(), RenderTextureLayout::COPY_DEST));
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto& level = layout[i];
        list->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.get(), uint32_t(i)),
            RenderTextureCopyLocation::PlacedFootprint(staging.get(), format, level.width, level.height, 1, level.row_pixels, level.offset));
    }
    list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture.get(), RenderTextureLayout::SHADER_READ));
    retire(staging);
    levels.clear(); levels.shrink_to_fit();
    return true;
}

struct Program::Shaders {
    std::unique_ptr<RenderShader> vertex, pixel;
};

Program::Program(const char* name, uint32_t textures, std::vector<Sampler> sampler_descs)
    : name(name), texture_count(textures), shaders(std::make_unique<Shaders>()) {
    if (!context) throw std::runtime_error("HD layer program created before the GPU context");
    for (const auto& blobs : embedded()) {
        if (blobs.name != name || blobs.format != context->format) continue;
        shaders->vertex = require(context->device->createShader(blobs.vertex.data(), blobs.vertex.size(), "VSMain", blobs.format),
                                  "HD layer vertex shader failed");
        shaders->pixel = require(context->device->createShader(blobs.pixel.data(), blobs.pixel.size(), "PSMain", blobs.format),
                                 "HD layer pixel shader failed");
    }
    if (!shaders->vertex) throw std::runtime_error(std::string("No embedded HD layer shaders for ") + name + " on this backend");
    for (const auto& desc : sampler_descs) {
        RenderSamplerDesc sampler;
        sampler.minFilter = sampler.magFilter = desc.linear ? RenderFilter::LINEAR : RenderFilter::NEAREST;
        sampler.mipmapMode = desc.linear ? RenderMipmapMode::LINEAR : RenderMipmapMode::NEAREST;
        if (!desc.mipmaps) sampler.maxLOD = 0;
        sampler.addressU = sampler.addressV = sampler.addressW =
            desc.repeat ? RenderTextureAddressMode::WRAP : RenderTextureAddressMode::CLAMP;
        samplers.push_back(require(context->device->createSampler(sampler), "HD layer sampler failed"));
    }
    // Set 1: textures at bindings 0.., then the samplers (NativeGpu.hlsli).
    for (uint32_t i = 0; i < texture_count; ++i) texture_ranges.emplace_back(RenderDescriptorRangeType::TEXTURE, i, 1, nullptr);
    sampler_pointers.reserve(samplers.size());
    for (const auto& sampler : samplers) sampler_pointers.push_back(sampler.get());
    for (uint32_t i = 0; i < samplers.size(); ++i)
        texture_ranges.emplace_back(RenderDescriptorRangeType::SAMPLER, texture_count + i, 1, &sampler_pointers[i]);
    texture_set_desc.descriptorRanges = texture_ranges.data();
    texture_set_desc.descriptorRangesCount = uint32_t(texture_ranges.size());
    RenderPipelineLayoutBuilder builder;
    builder.begin();
    builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    builder.addDescriptorSet(context->data_builder.descriptorSetDesc);
    builder.addDescriptorSet(texture_set_desc);
    builder.end();
    layout = require(builder.create(context->device), "HD layer pipeline layout failed");
}

Program::~Program() = default;

std::unique_ptr<RenderDescriptorSet> Program::bind_textures(std::span<Texture* const> textures) const {
    if (textures.size() != texture_count) throw std::runtime_error("HD layer texture count differs from its program");
    auto set = require(context->device->createDescriptorSet(texture_set_desc), "HD layer texture descriptors failed");
    for (uint32_t i = 0; i < texture_count; ++i) {
        if (!textures[i]->get()) throw std::runtime_error("HD layer texture bound before its upload");
        set->setTexture(i, textures[i]->get(), RenderTextureLayout::SHADER_READ);
    }
    return set;
}

bool Program::begin(RenderCommandList* list, RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call,
                    const State& state, RenderDescriptorSet* textures, uint32_t data, uint32_t flags) const {
    if (!context || call.colorFormat == RenderFormat::UNKNOWN) return false;
    const Key key{call.colorFormat, call.depthFormat, call.sampleCount, state};
    auto found = pipelines.find(key);
    if (found == pipelines.end()) {
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout = layout.get();
        desc.vertexShader = shaders->vertex.get();
        desc.pixelShader = shaders->pixel.get();
        desc.renderTargetCount = 1;
        desc.renderTargetFormat[0] = call.colorFormat;
        desc.depthTargetFormat = call.depthFormat;
        desc.multisampling.sampleCount = call.sampleCount;
        desc.primitiveTopology = state.topology;
        desc.cullMode = state.cull;
        desc.frontFace = state.counter_clockwise ? RenderFrontFace::COUNTER_CLOCKWISE : RenderFrontFace::CLOCKWISE;
        desc.depthEnabled = call.depthFormat != RenderFormat::UNKNOWN && (state.depth_test || state.depth_write);
        desc.depthWriteEnabled = state.depth_write;
        desc.depthFunction = state.depth_test ? RenderComparisonFunction::LESS_EQUAL : RenderComparisonFunction::ALWAYS;
        auto& blend = desc.renderTargetBlend[0];
        blend = RenderBlendDesc::Copy();
        if (state.blend != Blend::opaque) {
            blend.blendEnabled = true;
            blend.srcBlend = state.blend == Blend::premultiplied ? RenderBlend::ONE : RenderBlend::SRC_ALPHA;
            blend.dstBlend = RenderBlend::INV_SRC_ALPHA;
            blend.blendOp = RenderBlendOperation::ADD;
            blend.srcBlendAlpha = RenderBlend::ONE;
            blend.dstBlendAlpha = RenderBlend::INV_SRC_ALPHA;
            blend.blendOpAlpha = RenderBlendOperation::ADD;
        }
        found = pipelines.emplace(key, require(context->device->createGraphicsPipeline(desc), "HD layer pipeline failed")).first;
    }
    const uint32_t params[4] = {data, flags, 0, 0};
    list->setFramebuffer(framebuffer);
    list->setGraphicsPipelineLayout(layout.get());
    list->setPipeline(found->second.get());
    list->setGraphicsDescriptorSet(context->data_set.get(), 0);
    if (textures) list->setGraphicsDescriptorSet(textures, 1);
    list->setGraphicsPushConstants(0, params);
    list->setViewports(call.viewport);
    list->setScissors(call.scissor);
    return true;
}
}
