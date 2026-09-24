#define HLSL_CPU
#include "native_background.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_metal.h"
#include "json/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace srw64::backgrounds {
namespace {
using json = nlohmann::json;

// A G_NOOP carrying this tag immediately precedes the marker rectangle.
constexpr uint32_t kTagW0 = 0x00534247;      // G_NOOP, "SBG"
constexpr uint32_t kTagW1 = 0x42470000;      // "BG" + 16-bit draw id
constexpr uint32_t kIdBase = 0x42470000;     // native draw ids handed to RT64
constexpr size_t kRing = 64;
constexpr size_t kMaxRects = 128;            // 80 tiles of 32x32 for a 320x240 picture
constexpr uint32_t kSlots = 0x000FFA70, kSlotSize = 0xC4, kSubBase = 0x3C, kSubSize = 0x30;
constexpr uint32_t kHandles = 0x00160340, kHandleSize = 20, kHandleCount = 200;  // 8008A11C

struct Asset {
    uint16_t image = 0, palette = 0;
    std::filesystem::path file;
    std::vector<std::vector<uint8_t>> levels;  // premultiplied RGBA8 mip chain, dropped after upload
    std::vector<std::pair<int, int>> sizes;
    MTL::Texture* texture{};
};

struct Quad { float rect[4], uv[4]; };        // N64 screen pixels; normalised picture coordinates
struct Draw {
    uint32_t id = 0;
    int asset = -1;
    float prim[4]{1, 1, 1, 1};                // the combiner is TEXEL0 * PRIM (fades)
    std::vector<Quad> quads;
};

int width = 0, height = 0, source_width = 0, source_height = 0;
std::vector<Asset> assets;
std::map<std::pair<uint16_t, uint16_t>, int> by_ids;
std::mutex asset_mutex;
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
RT64::NativeMeshClassify *previous_classify{};
RT64::NativeMeshRender *previous_render{};
MTL::Device *device{};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState*> pipelines;
MTL::DepthStencilState *depth_state{};
MTL::SamplerState *sampler{};
std::atomic<uint64_t> rewritten{}, rendered{}, skipped{}, unexpected{}, decoded{};
std::ofstream dump;          // SRW64_BG_DUMP: the first draw of each picture without HD, for new layouts
std::set<std::pair<uint16_t, uint16_t>> dumped;
std::filesystem::path output;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value; std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4); return value;
}
void put(uint8_t* rdram, uint32_t address, uint32_t value) {
    std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4);
}
uint16_t half(const uint8_t* rdram, uint32_t address) {
    uint16_t value; std::memcpy(&value, rdram + ((address & 0x1FFFFFFF) ^ 2), 2); return value;
}
// A resource handle (sprite sub-record +0xC/+0xE) -> ROM resource id.
bool resource(const uint8_t* rdram, int16_t handle, uint16_t& id) {
    if (handle < 0 || uint32_t(handle) >= kHandleCount) return false;
    const uint32_t record = kHandles + uint32_t(handle) * kHandleSize;
    if (half(rdram, record) != 1) return false;
    id = half(rdram, record + 2);
    return true;
}

bool hd_enabled() {
    return !presentation::image_mode.enabled() || presentation::image_mode.current() == 1;
}

// Premultiplied RGBA with a box-filtered mip chain (any size; odd edges clamp).
void decode(Asset& asset) {
    int w = 0, h = 0, n = 0;
    uint8_t* pixels = stbi_load(asset.file.c_str(), &w, &h, &n, 4);
    if (!pixels) throw std::runtime_error("Cannot read HD background " + asset.file.string());
    if (w != width || h != height) { stbi_image_free(pixels); throw std::runtime_error("HD background size differs from the manifest: " + asset.file.string()); }
    std::vector<uint8_t> level(pixels, pixels + size_t(w) * h * 4);
    stbi_image_free(pixels);
    for (size_t i = 0; i < level.size(); i += 4)
        for (int c = 0; c < 3; ++c) level[i + c] = uint8_t((level[i + c] * level[i + 3] + 127) / 255);
    asset.levels.clear(); asset.sizes.clear();
    asset.levels.push_back(std::move(level)); asset.sizes.push_back({w, h});
    while (w > 1 || h > 1) {
        const auto& src = asset.levels.back();
        const int dw = std::max(1, w / 2), dh = std::max(1, h / 2);
        std::vector<uint8_t> next(size_t(dw) * dh * 4);
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](int xx, int yy) { return src[(size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)) * 4 + c]; };
                    next[(size_t(y) * dw + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        asset.levels.push_back(std::move(next)); asset.sizes.push_back({dw, dh});
        w = dw; h = dh;
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
        struct Quad { float4 rect, uv; };
        struct Frame { float4 resolution, prim; };
        struct V { float4 p [[position]]; float2 uv; };
        vertex V vs(uint i [[vertex_id]], uint q [[instance_id]], const device Quad* quads [[buffer(0)]],
                    constant Frame& f [[buffer(1)]]) {
            const float2 corner = float2(i & 1, i >> 1);
            const float2 p = mix(quads[q].rect.xy, quads[q].rect.zw, corner);
            const float2 clip = (p - f.resolution.xy * .5f) / (f.resolution.xy * float2(.5f, -.5f));
            return {float4(clip, 0, 1), mix(quads[q].uv.xy, quads[q].uv.zw, corner)};
        }
        fragment float4 fs(V in [[stage_in]], texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]],
                           constant Frame& f [[buffer(0)]]) {
            const float4 c = image.sample(s, in.uv);   // premultiplied
            return float4(c.rgb * f.prim.rgb * f.prim.a, c.a * f.prim.a);
        }
    )";
    NS::Error* error{};
    auto* library = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "HD background shader failed");
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
    if (!next) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "HD background pipeline failed");
    return pipelines[key] = next;
}

// GPU thread: upload on first use. There are only sixteen pictures and one shows at a
// time, so textures stay resident once made.
MTL::Texture* texture_for(Asset& asset) {
    std::lock_guard lock(asset_mutex);
    if (asset.texture) return asset.texture;
    if (asset.levels.empty()) return nullptr;
    auto* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, width, height, true);
    desc->setUsage(MTL::TextureUsageShaderRead);
    asset.texture = device->newTexture(desc);
    if (!asset.texture) throw std::runtime_error("HD background texture allocation failed");
    for (size_t level = 0; level < asset.levels.size() && level < asset.texture->mipmapLevelCount(); ++level) {
        const auto [w, h] = asset.sizes[level];
        asset.texture->replaceRegion(MTL::Region(0, 0, w, h), level, asset.levels[level].data(), size_t(w) * 4);
    }
    asset.levels.clear(); asset.levels.shrink_to_fit();
    return asset.texture;
}

bool render(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) != kIdBase) return previous_render ? previous_render(list, framebuffer, call) : false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[call.id % kRing];
    }
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !device || draw.quads.empty()) { ++skipped; return true; }
    MTL::Texture* texture = texture_for(assets[size_t(draw.asset)]);
    if (!texture) { ++skipped; return true; }
    const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (fb->colorAttachments.size() != 1 || !fb->depthAttachment.getTexture())
        throw std::runtime_error("HD background requires the scene color and depth attachments");
    auto* color = fb->colorAttachments[0].getTexture();
    auto* depth = fb->depthAttachment.getTexture();
    auto* pipeline = pipeline_for(color, depth);
    struct { float resolution[4], prim[4]; } frame{{float(call.fbWidth), float(call.fbHeight), 0, 0},
                                                   {draw.prim[0], draw.prim[1], draw.prim[2], draw.prim[3]}};
    auto* command = static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(color); attachment->setLoadAction(MTL::LoadActionLoad); attachment->setStoreAction(MTL::StoreActionStore);
    pass->depthAttachment()->setTexture(depth);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionLoad); pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    auto* encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setLabel(NS::String::string("SRW64 HD background", NS::UTF8StringEncoding));
    encoder->setRenderPipelineState(pipeline);
    encoder->setDepthStencilState(depth_state);
    encoder->setViewport(MTL::Viewport{call.viewport.x, call.viewport.y, call.viewport.width, call.viewport.height, call.viewport.minDepth, call.viewport.maxDepth});
    const auto& s = call.scissor;
    encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(s.left), NS::UInteger(s.top), NS::UInteger(s.right - s.left), NS::UInteger(s.bottom - s.top)});
    encoder->setCullMode(MTL::CullModeNone);
    encoder->setVertexBytes(draw.quads.data(), draw.quads.size() * sizeof(Quad), 0);
    encoder->setVertexBytes(&frame, sizeof(frame), 1);
    encoder->setFragmentTexture(texture, 0);
    encoder->setFragmentSamplerState(sampler, 0);
    encoder->setFragmentBytes(&frame, sizeof(frame), 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4), NS::UInteger(draw.quads.size()));
    encoder->endEncoding();
    ++rendered;
    return true;
}
}

void configure(const std::filesystem::path& art_directory, const std::filesystem::path& directory) {
    output = directory;
    if (std::getenv("SRW64_BG_DUMP")) dump.open(directory / "background-draws.jsonl");
    const auto spec_path = art_directory / "srw64-backgrounds-hd.json";
    if (!std::filesystem::exists(spec_path)) return;
    std::ifstream stream(spec_path);
    const json spec = json::parse(stream);
    if (spec.at("schema") != "srw64.background-images.v1") throw std::runtime_error("Unsupported HD background specification");
    width = spec.at("size")[0]; height = spec.at("size")[1];
    source_width = spec.at("source_size")[0]; source_height = spec.at("source_size")[1];
    if (width < source_width || height < source_height || width > 8192 || height > 8192)
        throw std::runtime_error("Unsupported HD background size");
    for (const auto& row : spec.at("images")) {
        Asset asset;
        asset.image = row.at("image").get<uint16_t>();
        asset.palette = row.at("palette").get<uint16_t>();
        asset.file = art_directory / row.at("file").get<std::string>();
        if (!std::filesystem::exists(asset.file)) throw std::runtime_error("Missing HD background " + asset.file.string());
        if (!by_ids.emplace(std::make_pair(asset.image, asset.palette), int(assets.size())).second)
            throw std::runtime_error("Duplicate HD background identity");
        assets.push_back(std::move(asset));
    }
    if (assets.empty()) return;
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
    fprintf(stderr, "SRW64_HD_BACKGROUNDS loaded %zu background(s) at %dx%d\n", assets.size(), width, height);
}

void metal_init(plume::RenderDevice* value) {
    device = static_cast<plume::MetalDevice*>(value)->mtl;
    if (!depth_state) {
        auto* depthDesc = MTL::DepthStencilDescriptor::alloc()->init();
        depthDesc->setDepthCompareFunction(MTL::CompareFunctionAlways);
        depthDesc->setDepthWriteEnabled(false);
        depth_state = device->newDepthStencilState(depthDesc); depthDesc->release();
    }
    if (!sampler) {
        auto* sampling = MTL::SamplerDescriptor::alloc()->init();
        sampling->setMinFilter(MTL::SamplerMinMagFilterLinear); sampling->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampling->setMipFilter(MTL::SamplerMipFilterLinear);
        sampling->setSAddressMode(MTL::SamplerAddressModeClampToEdge); sampling->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
        sampler = device->newSamplerState(sampling); sampling->release();
    }
}

void rewrite(uint8_t* rdram, const BackgroundDraw& draw) {
    if (draw.slot >= 300 || draw.sub >= 4) return;
    const uint32_t sub = kSlots + draw.slot * kSlotSize + kSubBase + draw.sub * kSubSize;
    uint16_t image = 0, palette = 0;
    if (!resource(rdram, int16_t(half(rdram, sub + 0xC)), image) || !resource(rdram, int16_t(half(rdram, sub + 0xE)), palette)) return;
    const auto found = by_ids.find({image, palette});
    if (found == by_ids.end() && dump.is_open() && dumped.insert({image, palette}).second) {
        json words = json::array();
        for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end && words.size() < 400; p += 8) words.push_back({word(rdram, p), word(rdram, p + 4)});
        dump << json({{"slot", draw.slot}, {"sub", draw.sub}, {"image", image}, {"palette", palette}, {"words", words}}).dump() << '\n';
        dump.flush();
    }
    if (found == by_ids.end() || !hd_enabled()) return;
    // The drawer sets PRIM once, then per 32x32 tile: SETTIMG (the picture), LOADTILE of
    // the tile's region (image coordinates), SETTILESIZE to (0,0)-(31,31) and TEXRECT
    // (E4, E1, F1) at the tile's screen position with S/T from the tile's origin.
    Draw record;
    record.asset = found->second;
    std::vector<uint32_t> rects;
    float load[4]{}, tile[2]{};
    float columns = 1;  // picture pixels per loaded texel: 2 when CI4 is loaded as 8-bit (starfield)
    bool loaded = false;
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), w1 = word(rdram, p + 4), op = w0 >> 24;
        if (op == 0xFD && ((w0 >> 19) & 3) == 1) {
            columns = float(source_width) / float((w0 & 0xFFF) + 1);  // 8-bit SETTIMG: its row width
        } else if (op == 0xFA) {
            for (int c = 0; c < 4; ++c) record.prim[c] = float((w1 >> (24 - 8 * c)) & 0xFF) / 255.f;
        } else if (op == 0xF4) {
            load[0] = ((w0 >> 12) & 0xFFF) / 4.f * columns; load[1] = (w0 & 0xFFF) / 4.f;
            load[2] = (((w1 >> 12) & 0xFFF) / 4.f + 1) * columns; load[3] = (w1 & 0xFFF) / 4.f + 1;
            loaded = true;
        } else if (op == 0xF2 && ((w1 >> 24) & 7) == 0) {
            tile[0] = ((w0 >> 12) & 0xFFF) / 4.f; tile[1] = (w0 & 0xFFF) / 4.f;  // render tile 0 origin
        } else if (op == 0xE4 && p + 24 <= draw.dl_end) {
            if (word(rdram, p + 8) >> 24 != 0xE1 || word(rdram, p + 16) >> 24 != 0xF1 || !loaded || rects.size() >= kMaxRects) {
                ++unexpected; return;
            }
            const uint32_t st = word(rdram, p + 12), d = word(rdram, p + 20);
            const float sx0 = ((w1 >> 12) & 0xFFF) / 4.f, sy0 = (w1 & 0xFFF) / 4.f;
            const float sx1 = ((w0 >> 12) & 0xFFF) / 4.f, sy1 = (w0 & 0xFFF) / 4.f;
            const float s = int16_t(st >> 16) / 32.f, t = int16_t(st & 0xFFFF) / 32.f;
            const float dsdx = int16_t(d >> 16) / 1024.f, dtdy = int16_t(d & 0xFFFF) / 1024.f;
            // Texel (s, t) of tile 0 is picture pixel load origin + (s, t) - tile origin.
            const float u0 = load[0] + s - tile[0], v0 = load[1] + t - tile[1];
            Quad q{{sx0, sy0, sx1, sy1},
                   {u0 / source_width, v0 / source_height, (u0 + (sx1 - sx0) * dsdx) / source_width, (v0 + (sy1 - sy0) * dtdy) / source_height}};
            record.quads.push_back(q);
            x0 = std::min<int32_t>(x0, (w1 >> 12) & 0xFFF); y0 = std::min<int32_t>(y0, w1 & 0xFFF);
            x1 = std::max<int32_t>(x1, (w0 >> 12) & 0xFFF); y1 = std::max<int32_t>(y1, w0 & 0xFFF);
            rects.push_back(p);
            p += 16;
        }
    }
    if (rects.empty() || x1 <= x0 || y1 <= y0) { ++unexpected; return; }
    {
        std::lock_guard lock(asset_mutex);
        Asset& asset = assets[size_t(record.asset)];
        if (!asset.texture && asset.levels.empty()) decode(asset);
    }
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    // The last rectangle becomes the marker covering the whole picture; the SETTILESIZE
    // before it carries the tag. The others become no-ops, except the first: when the
    // picture opens the frame (the world-map starfield), RT64 still has the frame's clear
    // pending and applies it with its next pass, wiping a native draw that came first.
    // Its own first tile starts that pass; the whole picture covers it later.
    const size_t marker = rects.size() - 1;
    for (size_t r = 0; r < rects.size(); ++r) {
        const uint32_t p = rects[r];
        if (r == 0 && marker > 0) continue;
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
        std::ofstream(output / "hd-background-summary.json") << json({{"schema", "srw64.hd-background-run.v1"},
            {"backgrounds", assets.size()}, {"size", {width, height}}, {"rewritten_draws", rewritten.load()},
            {"native_draws", rendered.load()}, {"skipped", skipped.load()}, {"unexpected_layout", unexpected.load()},
            {"decoded", decoded.load()}, {"resident_at_exit", resident}}).dump(2) << '\n';
    }
    for (auto& asset : assets) if (asset.texture) { asset.texture->release(); asset.texture = nullptr; }
    for (auto& [key, state] : pipelines) state->release();
    pipelines.clear();
    if (depth_state) depth_state->release(); depth_state = nullptr;
    if (sampler) sampler->release(); sampler = nullptr;
    device = nullptr;
}
}
