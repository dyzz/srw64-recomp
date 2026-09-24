#define HLSL_CPU
#include "native_portrait.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_metal.h"
#include "json/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace srw64::portraits {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// A G_NOOP carrying this tag immediately precedes the marker rectangle.
constexpr uint32_t kTagW0 = 0x00535054;      // G_NOOP, "SPT"
constexpr uint32_t kTagW1 = 0x50540000;      // "PT" + 16-bit draw id
constexpr uint32_t kIdBase = 0x50540000;     // native draw ids handed to RT64
constexpr size_t kRing = 1024;
constexpr int kTiles = 9;                    // 3x3 LoadTile blocks of 32x32
constexpr int32_t kSpan = 96 * 4;            // drawn area in 10.2 screen coordinates
constexpr size_t kResident = 64;             // textures kept before idle ones are released
constexpr auto kIdle = std::chrono::seconds(20);

struct Asset {
    uint16_t image = 0;
    std::filesystem::path file;
    std::vector<std::vector<uint8_t>> levels;  // premultiplied RGBA8 mip chain, dropped after upload
    MTL::Texture* texture{};
    Clock::time_point used{};
};

struct Draw {
    uint32_t id = 0;
    int asset = -1;
    bool flip = false, silhouette = false;
    float rect[4]{};                         // N64 screen pixels
};

int size = 0;
float silhouette_rgb[3]{};
std::vector<Asset> assets;
// The sprite record holds resource handles, so a portrait is recognised by FNV-1a 64
// digests of the index and palette data its SETTIMGs point at: (pixels, palette) ->
// (asset, silhouette). One pair of images shares its pixels and differs by palette.
std::map<std::pair<uint64_t, uint64_t>, std::pair<int, bool>> by_content;
std::mutex asset_mutex;                      // decoded levels and textures cross threads
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
RT64::NativeMeshClassify *previous_classify{};
RT64::NativeMeshRender *previous_render{};
MTL::Device *device{};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState*> pipelines;
MTL::DepthStencilState *depth_state{};
MTL::SamplerState *sampler{};
std::atomic<uint64_t> rewritten{}, rendered{}, skipped{}, unexpected{}, unknown{}, decoded{}, released{};
std::map<std::string, uint64_t> unexpected_shapes;  // "rects/width/height/texture width" of draws left original
std::mutex shape_mutex;
std::filesystem::path output;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}

void put(uint8_t* rdram, uint32_t address, uint32_t value) {
    std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4);
}

// RDRAM holds big-endian words byte-swapped within each 32-bit word.
uint64_t digest(const uint8_t* rdram, uint32_t address, uint32_t length) {
    uint64_t value = 0xCBF29CE484222325ull;
    address &= 0x1FFFFFFF;
    for (uint32_t i = 0; i < length; ++i) value = (value ^ rdram[(address + i) ^ 3]) * 0x100000001B3ull;
    return value;
}

uint64_t hex64(const json& value) { return std::stoull(value.get<std::string>(), nullptr, 16); }

bool hd_enabled() {
    return !presentation::image_mode.enabled() || presentation::image_mode.current() == 1;
}

// Premultiplied RGBA with a box-filtered mip chain, so linear sampling at any window
// scale never pulls colour from transparent texels.
void decode(Asset& asset) {
    int w = 0, h = 0, n = 0;
    uint8_t* pixels = stbi_load(asset.file.c_str(), &w, &h, &n, 4);
    if (!pixels) throw std::runtime_error("Cannot read HD portrait " + asset.file.string());
    if (w != size || h != size) { stbi_image_free(pixels); throw std::runtime_error("HD portrait size differs from the manifest: " + asset.file.string()); }
    std::vector<uint8_t> level(pixels, pixels + size_t(w) * h * 4);
    stbi_image_free(pixels);
    for (size_t i = 0; i < level.size(); i += 4)
        for (int c = 0; c < 3; ++c) level[i + c] = uint8_t((level[i + c] * level[i + 3] + 127) / 255);
    asset.levels.clear();
    asset.levels.push_back(std::move(level));
    for (int s = size; s > 1; s /= 2) {
        const auto& src = asset.levels.back();
        const int d = s / 2;
        std::vector<uint8_t> next(size_t(d) * d * 4);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](int xx, int yy) { return src[(size_t(yy) * s + xx) * 4 + c]; };
                    next[(size_t(y) * d + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        asset.levels.push_back(std::move(next));
    }
    ++decoded;
}

uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

MTL::RenderPipelineState* pipeline_for(MTL::Texture* color, MTL::Texture* depth) {
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = pipelines.find(key); found != pipelines.end()) return found->second;
    const char* source = R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Uniforms { float4 rect, uv, resolution; };
        struct Fill { float4 rgb_on; };
        struct V { float4 p [[position]]; float2 uv; };
        vertex V vs(uint i [[vertex_id]], constant Uniforms& u [[buffer(0)]]) {
            const float2 corner = float2(i & 1, i >> 1);
            const float2 p = mix(u.rect.xy, u.rect.zw, corner);
            const float2 clip = (p - u.resolution.xy * .5f) / (u.resolution.xy * float2(.5f, -.5f));
            return {float4(clip, 0, 1), mix(u.uv.xy, u.uv.zw, corner)};
        }
        fragment float4 fs(V in [[stage_in]], texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]],
                           constant Fill& fill [[buffer(0)]]) {
            const float4 c = image.sample(s, in.uv);   // premultiplied
            // The silhouette palette paints every opaque texel one dark grey.
            return fill.rgb_on.w > 0 ? float4(fill.rgb_on.rgb * c.a, c.a) : c;
        }
    )";
    NS::Error* error{};
    auto* library = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "HD portrait shader failed");
    auto* vs = library->newFunction(NS::String::string("vs", NS::UTF8StringEncoding));
    auto* fs = library->newFunction(NS::String::string("fs", NS::UTF8StringEncoding));
    auto* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs); desc->setFragmentFunction(fs);
    auto* attachment = desc->colorAttachments()->object(0);
    attachment->setPixelFormat(color->pixelFormat());
    attachment->setBlendingEnabled(true);
    attachment->setSourceRGBBlendFactor(MTL::BlendFactorOne);
    attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    attachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    desc->setDepthAttachmentPixelFormat(depth->pixelFormat());
    desc->setRasterSampleCount(color->sampleCount());
    auto* next = device->newRenderPipelineState(desc, &error);
    desc->release(); vs->release(); fs->release(); library->release();
    if (!next) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "HD portrait pipeline failed");
    return pipelines[key] = next;
}

// GPU thread. Upload on first use; release textures idle long enough that no command
// buffer can still reference them.
MTL::Texture* texture_for(Asset& asset) {
    std::lock_guard lock(asset_mutex);
    const auto now = Clock::now();
    if (!asset.texture) {
        if (asset.levels.empty()) return nullptr;
        size_t resident = 0;
        for (auto& other : assets) resident += other.texture != nullptr;
        if (resident >= kResident)
            for (auto& other : assets)
                if (other.texture && now - other.used > kIdle) { other.texture->release(); other.texture = nullptr; ++released; }
        auto* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, size, size, true);
        desc->setUsage(MTL::TextureUsageShaderRead);
        asset.texture = device->newTexture(desc);
        if (!asset.texture) throw std::runtime_error("HD portrait texture allocation failed");
        for (size_t level = 0, s = size_t(size); level < asset.levels.size(); ++level, s = std::max<size_t>(1, s / 2))
            asset.texture->replaceRegion(MTL::Region(0, 0, s, s), level, asset.levels[level].data(), s * 4);
        asset.levels.clear();
        asset.levels.shrink_to_fit();
    }
    asset.used = now;
    return asset.texture;
}

bool render(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) != kIdBase) return previous_render ? previous_render(list, framebuffer, call) : false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[call.id % kRing];
    }
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !device) { ++skipped; return true; }
    MTL::Texture* texture = texture_for(assets[size_t(draw.asset)]);
    if (!texture) { ++skipped; return true; }
    const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (fb->colorAttachments.size() != 1 || !fb->depthAttachment.getTexture())
        throw std::runtime_error("HD portrait requires the scene color and depth attachments");
    auto* color = fb->colorAttachments[0].getTexture();
    auto* depth = fb->depthAttachment.getTexture();
    auto* pipeline = pipeline_for(color, depth);
    struct { float rect[4], uv[4], resolution[4]; } u{};
    std::memcpy(u.rect, draw.rect, sizeof(u.rect));
    // The quad is in full-frame N64 coordinates (see native_map.cpp on screenScale).
    u.uv[0] = draw.flip ? 1.f : 0.f; u.uv[1] = 0; u.uv[2] = draw.flip ? 0.f : 1.f; u.uv[3] = 1;
    u.resolution[0] = float(call.fbWidth); u.resolution[1] = float(call.fbHeight);
    struct { float rgb_on[4]; } fill{{silhouette_rgb[0], silhouette_rgb[1], silhouette_rgb[2], draw.silhouette ? 1.f : 0.f}};
    auto* command = static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(color); attachment->setLoadAction(MTL::LoadActionLoad); attachment->setStoreAction(MTL::StoreActionStore);
    pass->depthAttachment()->setTexture(depth);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionLoad); pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    auto* encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setLabel(NS::String::string("SRW64 HD portrait", NS::UTF8StringEncoding));
    encoder->setRenderPipelineState(pipeline);
    encoder->setDepthStencilState(depth_state);
    encoder->setViewport(MTL::Viewport{call.viewport.x, call.viewport.y, call.viewport.width, call.viewport.height, call.viewport.minDepth, call.viewport.maxDepth});
    const auto& s = call.scissor;
    encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(s.left), NS::UInteger(s.top), NS::UInteger(s.right - s.left), NS::UInteger(s.bottom - s.top)});
    encoder->setCullMode(MTL::CullModeNone);
    encoder->setVertexBytes(&u, sizeof(u), 0);
    encoder->setFragmentTexture(texture, 0);
    encoder->setFragmentSamplerState(sampler, 0);
    encoder->setFragmentBytes(&fill, sizeof(fill), 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    encoder->endEncoding();
    ++rendered;
    return true;
}
}

void configure(const std::filesystem::path& art_directory, const std::filesystem::path& directory) {
    const auto spec_path = art_directory / "srw64-portraits-hd.json";
    if (!std::filesystem::exists(spec_path)) return;
    output = directory;
    std::ifstream stream(spec_path);
    const json spec = json::parse(stream);
    if (spec.at("schema") != "srw64.portrait-images.v1" || spec.at("source_size") != 96)
        throw std::runtime_error("Unsupported HD portrait specification");
    size = spec.at("size").get<int>();
    if (size < 96 || size > 2048 || size % 96 != 0) throw std::runtime_error("Unsupported HD portrait size");
    const uint64_t silhouette = hex64(spec.at("silhouette").at("palette_fnv1a64"));
    for (int c = 0; c < 3; ++c) silhouette_rgb[c] = spec.at("silhouette").at("rgb")[c].get<float>() / 255.f;
    for (const auto& row : spec.at("images")) {
        Asset asset;
        asset.image = row.at("image").get<uint16_t>();
        asset.file = art_directory / row.at("file").get<std::string>();
        if (!std::filesystem::exists(asset.file)) throw std::runtime_error("Missing HD portrait " + asset.file.string());
        const uint64_t pixels = hex64(row.at("pixels_fnv1a64"));
        if (!by_content.emplace(std::make_pair(pixels, hex64(row.at("palette_fnv1a64"))), std::make_pair(int(assets.size()), false)).second)
            throw std::runtime_error("Duplicate HD portrait identity");
        by_content.emplace(std::make_pair(pixels, silhouette), std::make_pair(int(assets.size()), true));
        assets.push_back(std::move(asset));
    }
    if (assets.empty()) return;
    // Chain after marker/hdmap, which install their hooks first.
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
    fprintf(stderr, "SRW64_HD_PORTRAITS loaded %zu portrait(s) at %dpx\n", assets.size(), size);
}

void metal_init(plume::RenderDevice* value) {
    device = static_cast<plume::MetalDevice*>(value)->mtl;
    if (!depth_state) {
        auto* depth = MTL::DepthStencilDescriptor::alloc()->init();
        depth->setDepthCompareFunction(MTL::CompareFunctionAlways);
        depth->setDepthWriteEnabled(false);
        depth_state = device->newDepthStencilState(depth); depth->release();
    }
    if (!sampler) {
        auto* sampling = MTL::SamplerDescriptor::alloc()->init();
        sampling->setMinFilter(MTL::SamplerMinMagFilterLinear); sampling->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampling->setMipFilter(MTL::SamplerMipFilterLinear);
        sampling->setSAddressMode(MTL::SamplerAddressModeClampToEdge); sampling->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
        sampler = device->newSamplerState(sampling); sampling->release();
    }
}

void rewrite(uint8_t* rdram, const PortraitDraw& draw) {
    if (assets.empty() || !hd_enabled()) return;
    // The drawer loads the palette (SETTIMG RGBA16 FD10 + LoadTLUT of 64 entries), then
    // writes nine 24-byte texture rectangles (E4 + E1 + F1), each after its CI8 SETTIMG
    // (FD48, width 96 or 97), tile load and SETTILESIZE. A flipped portrait starts every
    // tile at S = 32.
    std::vector<uint32_t> rects;
    bool flip = false;
    uint32_t image = 0, palette = 0, width = 0;
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if (op == 0xFD) {
            if ((w0 & 0xFFFFF000u) == 0xFD100000u && !palette) palette = word(rdram, p + 4);
            else if ((w0 & 0xFFFFF000u) == 0xFD480000u && !image) { image = word(rdram, p + 4); width = (w0 & 0xFFF) + 1; }
            continue;
        }
        if (op != 0xE4 || p + 24 > draw.dl_end) continue;
        const uint32_t w1 = word(rdram, p + 4);
        if (word(rdram, p + 8) >> 24 != 0xE1 || word(rdram, p + 16) >> 24 != 0xF1) continue;
        x0 = std::min<int32_t>(x0, (w1 >> 12) & 0xFFF); y0 = std::min<int32_t>(y0, w1 & 0xFFF);
        x1 = std::max<int32_t>(x1, (w0 >> 12) & 0xFFF); y1 = std::max<int32_t>(y1, w0 & 0xFFF);
        flip = flip || (word(rdram, p + 12) >> 16) != 0;
        rects.push_back(p);
        p += 16;
    }
    // Only a whole, unscaled portrait is replaced; anything else keeps the original tiles.
    if (rects.size() != kTiles || x1 - x0 != kSpan || y1 - y0 != kSpan || !image || !palette || (width != 96 && width != 97)) {
        ++unexpected;
        std::lock_guard lock(shape_mutex);
        if (unexpected_shapes.size() < 32)
            ++unexpected_shapes[std::to_string(rects.size()) + "/" + std::to_string((x1 - x0) / 4) + "/" + std::to_string((y1 - y0) / 4) + "/" + std::to_string(width)];
        return;
    }
    const auto found = by_content.find({digest(rdram, image, width * width), digest(rdram, palette, 128)});
    if (found == by_content.end()) { ++unknown; return; }
    Asset& asset = assets[size_t(found->second.first)];
    {
        std::lock_guard lock(asset_mutex);
        if (!asset.texture && asset.levels.empty()) decode(asset);
    }
    Draw record;
    record.asset = found->second.first;
    record.flip = flip;
    record.silhouette = found->second.second;
    record.rect[0] = x0 / 4.0f; record.rect[1] = y0 / 4.0f;
    record.rect[2] = x1 / 4.0f; record.rect[3] = y1 / 4.0f;
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    // The last rectangle becomes the marker covering the whole portrait; its SETTILESIZE
    // (the 8 bytes before it) carries the tag. The other rectangles become no-ops.
    const size_t marker = rects.size() - 1;
    for (size_t r = 0; r < rects.size(); ++r) {
        const uint32_t p = rects[r];
        if (r == marker) {
            put(rdram, p, 0xE4000000 | uint32_t(x1) << 12 | uint32_t(y1));
            put(rdram, p + 4, uint32_t(x0) << 12 | uint32_t(y0));
            put(rdram, p + 8, 0xE1000000); put(rdram, p + 12, 0);
            put(rdram, p + 16, 0xF1000000); put(rdram, p + 20, 0x04000400);
            continue;
        }
        for (uint32_t k = 0; k < 24; k += 8) { put(rdram, p + k, 0); put(rdram, p + k + 4, 0); }
    }
    put(rdram, rects[marker] - 8, kTagW0); put(rdram, rects[marker] - 4, kTagW1 | record.id);
    ++rewritten;
}

void shutdown() {
    if (!output.empty() && !assets.empty()) {
        size_t resident = 0;
        for (const auto& asset : assets) resident += asset.texture != nullptr;
        std::ofstream(output / "hd-portrait-summary.json") << json({{"schema", "srw64.hd-portrait-run.v1"},
            {"portraits", assets.size()}, {"size", size}, {"rewritten_draws", rewritten.load()}, {"native_draws", rendered.load()},
            {"skipped", skipped.load()}, {"unexpected_layout", unexpected.load()}, {"unknown_content", unknown.load()},
            {"decoded", decoded.load()}, {"released", released.load()}, {"resident_at_exit", resident},
            {"unexpected_shapes", [] { std::lock_guard lock(shape_mutex); return json(unexpected_shapes); }()}}).dump(2) << '\n';
    }
    for (auto& asset : assets) if (asset.texture) { asset.texture->release(); asset.texture = nullptr; }
    for (auto& [key, state] : pipelines) state->release();
    pipelines.clear();
    if (depth_state) depth_state->release(); depth_state = nullptr;
    if (sampler) sampler->release(); sampler = nullptr;
    device = nullptr;
}
}
