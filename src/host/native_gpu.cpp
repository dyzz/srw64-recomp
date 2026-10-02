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

// Embedded shader binaries (cmake/NativeGpu.cmake); add each new program here and
// to SRW64_NATIVE_PROGRAMS.
#include "HdMapVS.hlsl.spirv.h"
#include "HdMapPS.hlsl.spirv.h"
#include "HdBackgroundVS.hlsl.spirv.h"
#include "HdBackgroundPS.hlsl.spirv.h"
#include "HdMarkerVS.hlsl.spirv.h"
#include "HdMarkerPS.hlsl.spirv.h"
#include "HdModelVS.hlsl.spirv.h"
#include "HdModelPS.hlsl.spirv.h"
#include "HdRingVS.hlsl.spirv.h"
#include "HdRingPS.hlsl.spirv.h"
#include "HdTrailVS.hlsl.spirv.h"
#include "HdTrailPS.hlsl.spirv.h"
#include "HdPlateVS.hlsl.spirv.h"
#include "HdPlatePS.hlsl.spirv.h"
#include "HdBakedVS.hlsl.spirv.h"
#include "HdBakedPS.hlsl.spirv.h"
#include "HdWaterVS.hlsl.spirv.h"
#include "HdWaterPS.hlsl.spirv.h"
#include "HdSpriteVS.hlsl.spirv.h"
#include "HdSpritePS.hlsl.spirv.h"
#include "HdPortraitVS.hlsl.spirv.h"
#include "HdPortraitPS.hlsl.spirv.h"
#if defined(__APPLE__)
#include "HdMapVS.hlsl.metal.h"
#include "HdMapPS.hlsl.metal.h"
#include "HdBackgroundVS.hlsl.metal.h"
#include "HdBackgroundPS.hlsl.metal.h"
#include "HdMarkerVS.hlsl.metal.h"
#include "HdMarkerPS.hlsl.metal.h"
#include "HdModelVS.hlsl.metal.h"
#include "HdModelPS.hlsl.metal.h"
#include "HdRingVS.hlsl.metal.h"
#include "HdRingPS.hlsl.metal.h"
#include "HdTrailVS.hlsl.metal.h"
#include "HdTrailPS.hlsl.metal.h"
#include "HdPlateVS.hlsl.metal.h"
#include "HdPlatePS.hlsl.metal.h"
#include "HdBakedVS.hlsl.metal.h"
#include "HdBakedPS.hlsl.metal.h"
#include "HdWaterVS.hlsl.metal.h"
#include "HdWaterPS.hlsl.metal.h"
#include "HdSpriteVS.hlsl.metal.h"
#include "HdSpritePS.hlsl.metal.h"
#include "HdPortraitVS.hlsl.metal.h"
#include "HdPortraitPS.hlsl.metal.h"
#elif defined(_WIN32)
#include "HdMapVS.hlsl.dxil.h"
#include "HdMapPS.hlsl.dxil.h"
#include "HdBackgroundVS.hlsl.dxil.h"
#include "HdBackgroundPS.hlsl.dxil.h"
#include "HdMarkerVS.hlsl.dxil.h"
#include "HdMarkerPS.hlsl.dxil.h"
#include "HdModelVS.hlsl.dxil.h"
#include "HdModelPS.hlsl.dxil.h"
#include "HdRingVS.hlsl.dxil.h"
#include "HdRingPS.hlsl.dxil.h"
#include "HdTrailVS.hlsl.dxil.h"
#include "HdTrailPS.hlsl.dxil.h"
#include "HdPlateVS.hlsl.dxil.h"
#include "HdPlatePS.hlsl.dxil.h"
#include "HdBakedVS.hlsl.dxil.h"
#include "HdBakedPS.hlsl.dxil.h"
#include "HdWaterVS.hlsl.dxil.h"
#include "HdWaterPS.hlsl.dxil.h"
#include "HdSpriteVS.hlsl.dxil.h"
#include "HdSpritePS.hlsl.dxil.h"
#include "HdPortraitVS.hlsl.dxil.h"
#include "HdPortraitPS.hlsl.dxil.h"
#endif

namespace srw64::gpu {
using namespace plume;
namespace {
using Bytes = std::span<const unsigned char>;
template<size_t N> Bytes bytes(const char (&value)[N]) { return {reinterpret_cast<const unsigned char*>(value), N}; }
struct Blobs { std::string_view name; Bytes vertex, pixel; RenderShaderFormat format; };

// Every embedded program, for each shader format this platform builds.
#define SRW64_NATIVE_PROGRAMS(X) X(HdMap) X(HdBackground) X(HdMarker) X(HdModel) X(HdRing) X(HdTrail) X(HdPlate) X(HdSprite) X(HdPortrait) X(HdBaked) X(HdWater)
std::vector<Blobs> embedded() {
    std::vector<Blobs> all;
#define SRW64_SPIRV(name) all.push_back({#name, bytes(name##VSBlobSPIRV), bytes(name##PSBlobSPIRV), RenderShaderFormat::SPIRV});
    SRW64_NATIVE_PROGRAMS(SRW64_SPIRV)
#if defined(__APPLE__)
#define SRW64_MSL(name) all.push_back({#name, bytes(name##VSBlobMSL), bytes(name##PSBlobMSL), RenderShaderFormat::METAL});
    SRW64_NATIVE_PROGRAMS(SRW64_MSL)
#elif defined(_WIN32)
#define SRW64_DXIL(name) all.push_back({#name, bytes(name##VSBlobDXIL), bytes(name##PSBlobDXIL), RenderShaderFormat::DXIL});
    SRW64_NATIVE_PROGRAMS(SRW64_DXIL)
#endif
    return all;
}

template<class T> std::unique_ptr<T> require(std::unique_ptr<T> value, const char* what) {
    if (!value) throw std::runtime_error(what);
    return value;
}

// Per-draw constants: 128K float4 (2 MiB) in one persistently mapped upload buffer,
// read as StructuredBuffer<float4>. A layer draws a handful of times per workload, so
// a slot is reused only long after RT64 has waited for the workload that read it.
constexpr uint32_t kDataEntries = 1u << 17;
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
    if (entries == 0 || entries > kMaxDrawData) throw std::runtime_error("HD layer draw data size out of range");
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

Buffer::Buffer(const std::vector<uint8_t>& bytes) : size_(bytes.size()) {
    if (!context) throw std::runtime_error("HD layer buffer created before the GPU context");
    if (bytes.empty() || bytes.size() % 4) throw std::runtime_error("HD layer buffer size must be a positive multiple of 4");
    buffer = require(context->device->createBuffer(RenderBufferDesc::UploadBuffer(size_, RenderBufferFlag::STORAGE)),
                     "HD layer buffer allocation failed");
    auto* destination = buffer->map();
    if (!destination) throw std::runtime_error("Cannot map an HD layer buffer");
    std::memcpy(destination, bytes.data(), bytes.size());
    buffer->unmap();
}

Buffer::~Buffer() {
    if (buffer) retire(std::shared_ptr<RenderBuffer>(buffer.release()));
}

std::vector<std::vector<uint8_t>> rgba_mips(std::vector<uint8_t> level0, uint32_t width, uint32_t height) {
    if (level0.size() != size_t(width) * height * 4) throw std::runtime_error("RGBA level has the wrong size");
    std::vector<std::vector<uint8_t>> levels;
    levels.push_back(std::move(level0));
    uint32_t w = width, h = height;
    while (w > 1 || h > 1) {
        const auto& src = levels.back();
        const uint32_t dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
        std::vector<uint8_t> next(size_t(dw) * dh * 4);
        for (uint32_t y = 0; y < dh; ++y)
            for (uint32_t x = 0; x < dw; ++x)
                for (uint32_t c = 0; c < 4; ++c) {
                    const auto at = [&](uint32_t xx, uint32_t yy) { return src[(size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)) * 4 + c]; };
                    next[(size_t(y) * dw + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        levels.push_back(std::move(next));
        w = dw; h = dh;
    }
    return levels;
}

struct Program::Shaders {
    std::unique_ptr<RenderShader> vertex, pixel;
};

Program::Program(const char* name, uint32_t textures, std::vector<Sampler> sampler_descs)
    : Program(name, [&] {
          std::vector<Slot> slots(textures, Slot{Slot::texture});
          for (const auto& filter : sampler_descs) slots.push_back({Slot::sampler, filter});
          return slots;
      }()) {}

Program::Program(const char* name, std::vector<Slot> slot_list)
    : name(name), slots(std::move(slot_list)), shaders(std::make_unique<Shaders>()) {
    if (!context) throw std::runtime_error("HD layer program created before the GPU context");
    for (const auto& blobs : embedded()) {
        if (blobs.name != name || blobs.format != context->format) continue;
        shaders->vertex = require(context->device->createShader(blobs.vertex.data(), blobs.vertex.size(), "VSMain", blobs.format),
                                  "HD layer vertex shader failed");
        shaders->pixel = require(context->device->createShader(blobs.pixel.data(), blobs.pixel.size(), "PSMain", blobs.format),
                                 "HD layer pixel shader failed");
    }
    if (!shaders->vertex) throw std::runtime_error(std::string("No embedded HD layer shaders for ") + name + " on this backend");
    // Set 1: one binding per slot, in order (NativeGpu.hlsli).
    sampler_pointers.reserve(slots.size());
    for (uint32_t binding = 0; binding < slots.size(); ++binding) {
        const auto& slot = slots[binding];
        if (slot.kind == Slot::texture) texture_ranges.emplace_back(RenderDescriptorRangeType::TEXTURE, binding, 1, nullptr);
        else if (slot.kind == Slot::buffer) texture_ranges.emplace_back(RenderDescriptorRangeType::STRUCTURED_BUFFER, binding, 1, nullptr);
        else {
            RenderSamplerDesc sampler;
            sampler.minFilter = sampler.magFilter = slot.filter.linear ? RenderFilter::LINEAR : RenderFilter::NEAREST;
            sampler.mipmapMode = slot.filter.linear ? RenderMipmapMode::LINEAR : RenderMipmapMode::NEAREST;
            if (!slot.filter.mipmaps) sampler.maxLOD = 0;
            sampler.addressU = slot.filter.repeat_u ? RenderTextureAddressMode::WRAP : RenderTextureAddressMode::CLAMP;
            sampler.addressV = slot.filter.repeat_v ? RenderTextureAddressMode::WRAP : RenderTextureAddressMode::CLAMP;
            sampler.addressW = RenderTextureAddressMode::CLAMP;
            samplers.push_back(require(context->device->createSampler(sampler), "HD layer sampler failed"));
            sampler_pointers.push_back(samplers.back().get());
            texture_ranges.emplace_back(RenderDescriptorRangeType::SAMPLER, binding, 1, &sampler_pointers.back());
        }
    }
    texture_set_desc.descriptorRanges = texture_ranges.data();
    texture_set_desc.descriptorRangesCount = uint32_t(texture_ranges.size());
    RenderPipelineLayoutBuilder builder;
    builder.begin();
    builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    builder.addDescriptorSet(context->data_builder.descriptorSetDesc);
    if (!slots.empty()) builder.addDescriptorSet(texture_set_desc);
    builder.end();
    layout = require(builder.create(context->device), "HD layer pipeline layout failed");
}

Program::~Program() = default;

std::unique_ptr<RenderDescriptorSet> Program::bind(std::initializer_list<Resource> resources) const {
    auto set = require(context->device->createDescriptorSet(texture_set_desc), "HD layer descriptors failed");
    auto next = resources.begin();
    uint32_t index = 0;  // descriptor index: every range here holds one
    for (const auto& slot : slots) {
        if (slot.kind != Slot::sampler) {
            if (next == resources.end()) throw std::runtime_error(std::string("Too few resources for ") + name);
            if (slot.kind == Slot::texture) {
                auto* texture = std::get<Texture*>(*next);
                if (!texture->get()) throw std::runtime_error("HD layer texture bound before its upload");
                set->setTexture(index, texture->get(), RenderTextureLayout::SHADER_READ);
            } else {
                const RenderBufferStructuredView view(4);
                auto* buffer = std::get<Buffer*>(*next);
                set->setBuffer(index, buffer->get(), buffer->size(), &view);
            }
            ++next;
        }
        ++index;
    }
    if (next != resources.end()) throw std::runtime_error(std::string("Too many resources for ") + name);
    return set;
}

std::unique_ptr<RenderDescriptorSet> Program::bind_textures(std::span<Texture* const> textures) const {
    if (textures.size() == 1) return bind({textures[0]});
    if (textures.size() == 2) return bind({textures[0], textures[1]});
    throw std::runtime_error("HD layer texture count unsupported");
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
    // The hook's framebuffer is the one RT64 has bound (switchToDepthWrite just before);
    // setting it again would end RT64's render pass and start another for every draw, a
    // full store and load of the picture on a tiler. Draws reopen the pass after a copy.
    (void)framebuffer;
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
