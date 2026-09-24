#define HLSL_CPU
#include "native_sprite.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_workload.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_metal.h"
#include "json/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

uint64_t srw64_current_vi();
namespace srw64::sprites {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// A G_NOOP carrying this tag immediately precedes the marker primitive.
constexpr uint32_t kTagW0 = 0x0053534E;      // G_NOOP, "SSN"
constexpr uint32_t kTagW1 = 0x534E0000;      // "SN" + 16-bit draw id
constexpr uint32_t kIdBase = 0x534E0000;     // native draw ids handed to RT64
constexpr size_t kRing = 1024;
constexpr size_t kResident = 48;             // textures kept before idle ones are released
constexpr auto kIdle = std::chrono::seconds(20);
constexpr uint32_t kSlots = 0x000FFA70, kSlotSize = 0xC4, kSubBase = 0x3C, kSubSize = 0x30;
constexpr uint32_t kHandles = 0x00160340, kHandleSize = 20, kHandleCount = 200;  // 8008A11C

struct Asset {
    std::string key;
    std::filesystem::path file;              // HD frame image
    std::function<TextImage()> render;       // native text
    enum State { idle, queued, ready, failed } state = idle;
    uint32_t width = 0, height = 0;
    float units[2]{};
    std::vector<std::vector<uint8_t>> levels;  // premultiplied RGBA8 mips, dropped after upload
    MTL::Texture* texture{};
    Clock::time_point used{};
};
struct Image {
    uint16_t scene = 0, atlas = 0, palette = 0;
    std::set<uint8_t> frames;
    int asset = -1;
    float uv[4]{0, 0, 1, 1};
    bool wrap = false;
};
struct Draw {
    uint32_t id = 0;
    int asset = -1;
    bool quad = false, wrap = false, text = false;
    TextJob::Anchor anchor = TextJob::Anchor::center;
    float rect[4]{};      // union of the parts: screen x0,y0,x1,y1 (N64 px), or model x0,top,x1,bottom (y up)
    float z = 0;
    float uv[4]{0, 0, 1, 1};
    float color[4]{1, 1, 1, 1};  // tint and prim alpha
};

bool installed = false;
Describe describe{};
std::vector<std::unique_ptr<Asset>> assets;
std::map<std::string, int> by_key;
std::vector<Image> images;
std::mutex asset_mutex;                      // assets, levels and states cross threads
std::deque<int> queue;
std::condition_variable queue_ready;
std::thread worker;
bool stopping = false;
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
RT64::NativeMeshClassify* previous_classify{};
RT64::NativeMeshRender* previous_render{};
MTL::Device* device{};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger, bool>, MTL::RenderPipelineState*> pipelines;
std::array<MTL::DepthStencilState*, 4> depth_states{};
MTL::SamplerState *clamp_sampler{}, *wrap_sampler{};
std::atomic<uint64_t> rewritten{}, rendered{}, pending{}, skipped{}, unexpected{}, decoded{}, released{}, failures{};
std::mutex log_mutex;
std::set<std::string> logged;
std::ofstream log;
std::filesystem::path output;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}
void put(uint8_t* rdram, uint32_t address, uint32_t value) { std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4); }
// RDRAM holds big-endian words byte-swapped within each 32-bit word.
uint16_t half(const uint8_t* rdram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, rdram + ((address & 0x1FFFFFFF) ^ 2), 2);
    return value;
}
uint8_t byte(const uint8_t* rdram, uint32_t address) { return rdram[(address & 0x1FFFFFFF) ^ 3]; }

bool hd_enabled() { return !presentation::image_mode.enabled() || presentation::image_mode.current() == 1; }

void note(const std::string& key, json fields) {
    std::lock_guard lock(log_mutex);
    if (!log.is_open() || logged.size() > 4096 || !logged.insert(key).second) return;
    fields["vi"] = srw64_current_vi();
    log << fields.dump() << '\n';
    log.flush();
}

// A resource handle (sprite sub-record +0xA/+0xC/+0xE) -> (ROM resource, data address).
bool resource(const uint8_t* rdram, int16_t handle, uint16_t& id, uint32_t& data) {
    if (handle < 0 || uint32_t(handle) >= kHandleCount) return false;
    const uint32_t record = kHandles + uint32_t(handle) * kHandleSize;
    if (half(rdram, record) != 1) return false;
    id = half(rdram, record + 2);
    data = word(rdram, record + 0x10) & 0x1FFFFFFF;
    return data != 0 && data < 0x800000;
}

// Box-filtered mips of premultiplied RGBA8, any size.
void add_mips(Asset& asset) {
    uint32_t w = asset.width, h = asset.height;
    while (w > 1 || h > 1) {
        const auto& src = asset.levels.back();
        const uint32_t dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
        std::vector<uint8_t> next(size_t(dw) * dh * 4);
        for (uint32_t y = 0; y < dh; ++y)
            for (uint32_t x = 0; x < dw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](uint32_t xx, uint32_t yy) {
                        return uint32_t(src[(size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)) * 4 + c]);
                    };
                    next[(size_t(y) * dw + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) +
                                                                  at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        asset.levels.push_back(std::move(next));
        w = dw; h = dh;
    }
}

// Worker thread: decode HD frames and draw text off the game thread.
void work() {
    for (;;) {
        int index;
        std::string file_key;
        std::filesystem::path file;
        std::function<TextImage()> render;
        {
            std::unique_lock lock(asset_mutex);
            queue_ready.wait(lock, [] { return stopping || !queue.empty(); });
            if (stopping) return;
            index = queue.front(); queue.pop_front();
            file = assets[size_t(index)]->file;
            render = assets[size_t(index)]->render;
        }
        std::vector<uint8_t> pixels;
        uint32_t w = 0, h = 0;
        float units[2]{};
        bool ok = false;
        try {
            if (render) {
                auto image = render();
                w = image.width; h = image.height; units[0] = image.units[0]; units[1] = image.units[1];
                pixels = std::move(image.rgba);
                ok = w && h && pixels.size() == size_t(w) * h * 4;
            } else {
                int iw = 0, ih = 0, n = 0;
                if (uint8_t* data = stbi_load(file.c_str(), &iw, &ih, &n, 4)) {
                    w = uint32_t(iw); h = uint32_t(ih);
                    pixels.assign(data, data + size_t(w) * h * 4);
                    stbi_image_free(data);
                    for (size_t i = 0; i < pixels.size(); i += 4)
                        for (int c = 0; c < 3; ++c) pixels[i + c] = uint8_t((pixels[i + c] * pixels[i + 3] + 127) / 255);
                    ok = true;
                }
            }
        } catch (const std::exception& error) {
            fprintf(stderr, "SRW64_SCENE_SPRITE_FAILED %s\n", error.what());
        }
        std::lock_guard lock(asset_mutex);
        Asset& asset = *assets[size_t(index)];
        if (!ok) { asset.state = Asset::failed; ++failures; continue; }
        asset.width = w; asset.height = h;
        asset.units[0] = units[0]; asset.units[1] = units[1];
        asset.levels.clear();
        asset.levels.push_back(std::move(pixels));
        add_mips(asset);
        asset.state = Asset::ready;
        ++decoded;
    }
}

// Game thread. Ready: drawable now. Otherwise queue it once.
bool ready(int index) {
    std::lock_guard lock(asset_mutex);
    Asset& asset = *assets[size_t(index)];
    if (asset.state == Asset::ready) return true;
    if (asset.state == Asset::idle) {
        asset.state = Asset::queued;
        queue.push_back(index);
        queue_ready.notify_one();
    }
    return false;
}

int text_asset(const TextJob& job) {
    std::lock_guard lock(asset_mutex);
    if (const auto found = by_key.find(job.key); found != by_key.end()) return found->second;
    auto asset = std::make_unique<Asset>();
    asset->key = job.key;
    asset->render = job.render;
    assets.push_back(std::move(asset));
    return by_key[job.key] = int(assets.size() - 1);
}

uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

constexpr const char* kShader = R"(
    #include <metal_stdlib>
    using namespace metal;
    struct Uniforms {
        float4x4 mvp;
        float4 viewportScale, viewportTranslate, resolution, screen;
        float4 rect, uv, color, z;
    };
    struct V { float4 p [[position]]; float2 uv; };
    float4 to_clip(float3 position, constant Uniforms& u) {
        float4 p = u.mvp * float4(position, 1);
        float3 screen = p.xyz / float3(p.w, -p.w, p.w) * u.viewportScale.xyz + u.viewportTranslate.xyz;
        float2 clip = (screen.xy - u.resolution.xy * .5f) / (u.resolution.xy * float2(.5f, -.5f));
        clip = clip * u.screen.xy + u.screen.zw;
        return float4(clip * p.w, screen.z * p.w, p.w);
    }
    // Texture rectangles: rect is in full-frame N64 screen pixels.
    vertex V rect_vs(uint i [[vertex_id]], constant Uniforms& u [[buffer(0)]]) {
        const float2 corner = float2(i & 1, i >> 1);
        const float2 p = mix(u.rect.xy, u.rect.zw, corner);
        const float2 clip = (p - u.resolution.xy * .5f) / (u.resolution.xy * float2(.5f, -.5f));
        return {float4(clip, 0, 1), mix(u.uv.xy, u.uv.zw, corner)};
    }
    // Quads: rect is (left, top, right, bottom) in the sprite's model space, y up.
    vertex V quad_vs(uint i [[vertex_id]], constant Uniforms& u [[buffer(0)]]) {
        const float2 corner = float2(i & 1, i >> 1);
        return {to_clip(float3(mix(u.rect.x, u.rect.z, corner.x), mix(u.rect.y, u.rect.w, corner.y), u.z.x), u),
                mix(u.uv.xy, u.uv.zw, corner)};
    }
    fragment float4 fs(V in [[stage_in]], texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]],
                       constant Uniforms& u [[buffer(0)]]) {
        const float4 c = image.sample(s, in.uv);   // premultiplied
        return float4(c.rgb * u.color.rgb, c.a) * u.color.a;
    }
)";

MTL::RenderPipelineState* pipeline_for(MTL::Texture* color, MTL::Texture* depth, bool quad) {
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount(), quad);
    if (auto found = pipelines.find(key); found != pipelines.end()) return found->second;
    NS::Error* error{};
    auto* library = device->newLibrary(NS::String::string(kShader, NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Scene sprite shader failed");
    auto* vs = library->newFunction(NS::String::string(quad ? "quad_vs" : "rect_vs", NS::UTF8StringEncoding));
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
    if (!next) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Scene sprite pipeline failed");
    return pipelines[key] = next;
}

// GPU thread. Upload on first use; release textures idle long enough that no command
// buffer can still reference them.
MTL::Texture* texture_for(Asset& asset) {
    const auto now = Clock::now();
    if (!asset.texture) {
        if (asset.state != Asset::ready || asset.levels.empty()) return nullptr;
        size_t resident = 0;
        for (auto& other : assets) resident += other->texture != nullptr;
        if (resident >= kResident)
            for (auto& other : assets)
                if (other->texture && now - other->used > kIdle) {
                    other->texture->release(); other->texture = nullptr; ++released;
                    // A released text or frame is drawn again from its source when needed.
                    other->state = Asset::idle;
                }
        auto* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, asset.width, asset.height, true);
        desc->setUsage(MTL::TextureUsageShaderRead);
        desc->setMipmapLevelCount(asset.levels.size());
        asset.texture = device->newTexture(desc);
        if (!asset.texture) throw std::runtime_error("Scene sprite texture allocation failed");
        uint32_t w = asset.width, h = asset.height;
        for (size_t level = 0; level < asset.levels.size(); ++level) {
            asset.texture->replaceRegion(MTL::Region(0, 0, w, h), level, asset.levels[level].data(), size_t(w) * 4);
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
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
    // The original parts are gone either way: a stale id or a texture not ready draws nothing.
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !device || (draw.quad && !call.workload)) { ++skipped; return true; }
    MTL::Texture* texture;
    float units[2];
    {
        std::lock_guard lock(asset_mutex);
        Asset& asset = *assets[size_t(draw.asset)];
        texture = texture_for(asset);
        units[0] = asset.units[0]; units[1] = asset.units[1];
    }
    if (!texture) { ++skipped; return true; }
    struct {
        float mvp[16], viewportScale[4], viewportTranslate[4], resolution[4], screen[4];
        float rect[4], uv[4], color[4], z[4];
    } u{};
    float rect[4] = {draw.rect[0], draw.rect[1], draw.rect[2], draw.rect[3]};
    if (draw.text) {
        // Text keeps its own size, centred on the original frame or hung from its top.
        const float cx = (rect[0] + rect[2]) / 2, w = units[0], h = units[1];
        rect[0] = cx - w / 2; rect[2] = cx + w / 2;
        const float sign = draw.quad ? -1.f : 1.f;   // quads are y up
        const float top = draw.anchor == TextJob::Anchor::top ? rect[1] : (rect[1] + rect[3]) / 2 - sign * h / 2;
        rect[1] = top; rect[3] = top + sign * h;
    }
    std::memcpy(u.rect, rect, sizeof(rect));
    std::memcpy(u.uv, draw.uv, sizeof(u.uv));
    std::memcpy(u.color, draw.color, sizeof(u.color));
    u.z[0] = draw.z;
    u.resolution[0] = float(call.fbWidth); u.resolution[1] = float(call.fbHeight);
    u.screen[0] = call.screenScale[0]; u.screen[1] = call.screenScale[1];
    u.screen[2] = call.screenOffset[0]; u.screen[3] = call.screenOffset[1];
    if (draw.quad) {
        const auto& d = call.workload->drawData;
        const auto worldIndex = d.worldIndices.at(call.vertexIndex);
        const auto& world = d.lerpWorldTransforms.empty() ? d.worldTransforms.at(worldIndex) : d.lerpWorldTransforms.at(worldIndex);
        const auto& vp = d.modViewProjTransforms.empty() ? d.viewProjTransforms.at(call.viewProjIndex) : d.modViewProjTransforms.at(call.viewProjIndex);
        hlslpp::store(hlslpp::mul(world, vp), u.mvp);
        const auto& viewport = d.rspViewports.at(call.viewProjIndex);
        hlslpp::store(viewport.scale, u.viewportScale); hlslpp::store(viewport.translate, u.viewportTranslate);
    }
    const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (fb->colorAttachments.size() != 1 || !fb->depthAttachment.getTexture())
        throw std::runtime_error("Scene sprites require the scene color and depth attachments");
    auto* color = fb->colorAttachments[0].getTexture();
    auto* depth = fb->depthAttachment.getTexture();
    auto* pipeline = pipeline_for(color, depth, draw.quad);
    auto* command = static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(color); attachment->setLoadAction(MTL::LoadActionLoad); attachment->setStoreAction(MTL::StoreActionStore);
    pass->depthAttachment()->setTexture(depth);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionLoad); pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    auto* encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setLabel(NS::String::string("SRW64 scene sprite", NS::UTF8StringEncoding));
    encoder->setRenderPipelineState(pipeline);
    encoder->setDepthStencilState(depth_states[call.depthCompare ? 1 : 0]);  // blended: never write depth
    encoder->setViewport(MTL::Viewport{call.viewport.x, call.viewport.y, call.viewport.width, call.viewport.height, call.viewport.minDepth, call.viewport.maxDepth});
    const auto& s = call.scissor;
    encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(s.left), NS::UInteger(s.top), NS::UInteger(s.right - s.left), NS::UInteger(s.bottom - s.top)});
    encoder->setCullMode(MTL::CullModeNone);   // the chapter card turns its back while it flips
    encoder->setVertexBytes(&u, sizeof(u), 0);
    encoder->setFragmentBytes(&u, sizeof(u), 0);
    encoder->setFragmentTexture(texture, 0);
    encoder->setFragmentSamplerState(draw.wrap ? wrap_sampler : clamp_sampler, 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    encoder->endEncoding();
    ++rendered;
    return true;
}

bool identify(const uint8_t* rdram, const SceneDraw& draw, SceneId& id) {
    if (draw.slot >= 300 || draw.sub >= 4) return false;
    const uint32_t sub = kSlots + draw.slot * kSlotSize + kSubBase + draw.sub * kSubSize;
    uint32_t scene_data, atlas_data, palette_data;
    if (!resource(rdram, int16_t(half(rdram, sub + 0xA)), id.scene, scene_data) ||
        !resource(rdram, int16_t(half(rdram, sub + 0xC)), id.atlas, atlas_data) ||
        !resource(rdram, int16_t(half(rdram, sub + 0xE)), id.palette, palette_data)) return false;
    // Scene data: step count, vertex mode, then (frame, ticks) per step (80096CD8).
    const uint8_t step = byte(rdram, sub + 0x11);
    if (step >= byte(rdram, scene_data)) return false;
    id.frame = byte(rdram, scene_data + 2 + 2u * step);
    if (id.frame == 0xFF) return false;
    for (int i = 0; i < 16; ++i) id.colors[i] = half(rdram, palette_data + 8 + 2u * i);
    id.slot = draw.slot; id.sub = draw.sub;
    return true;
}
}

void configure(const std::filesystem::path& art_directory, const std::filesystem::path& directory) {
    output = directory;
    if (!art_directory.empty() && std::filesystem::exists(art_directory / "srw64-scene-images.json")) {
        std::ifstream stream(art_directory / "srw64-scene-images.json");
        const json spec = json::parse(stream);
        if (spec.at("schema") != "srw64.scene-images.v1") throw std::runtime_error("Unsupported scene image specification");
        for (const auto& row : spec.at("images")) {
            Image image;
            image.scene = row.at("scene"); image.atlas = row.at("atlas"); image.palette = row.at("palette");
            for (const auto& frame : row.at("frames")) image.frames.insert(frame.get<uint8_t>());
            for (int i = 0; i < 4; ++i) image.uv[i] = row.at("uv")[i].get<float>();
            image.wrap = row.at("wrap");
            auto asset = std::make_unique<Asset>();
            asset->file = art_directory / row.at("file").get<std::string>();
            if (!std::filesystem::exists(asset->file)) throw std::runtime_error("Missing scene image " + asset->file.string());
            asset->key = "image:" + row.at("file").get<std::string>();
            image.asset = int(assets.size());
            assets.push_back(std::move(asset));
            images.push_back(image);
        }
        fprintf(stderr, "SRW64_SCENE_IMAGES loaded %zu frame image(s)\n", images.size());
    }
    if (installed) return;
    installed = true;
    if (!output.empty()) log.open(output / "scene-sprites.jsonl");
    worker = std::thread(work);
    // Chain after marker/hdmap/portraits, which install their hooks first.
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
}

void set_text(Describe value) { describe = value; }

void metal_init(plume::RenderDevice* value) {
    device = static_cast<plume::MetalDevice*>(value)->mtl;
    for (int i = 0; i < 4; ++i) {
        if (depth_states[i]) continue;
        auto* desc = MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthCompareFunction((i & 1) ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
        desc->setDepthWriteEnabled(i & 2);
        depth_states[i] = device->newDepthStencilState(desc); desc->release();
    }
    for (auto [sampler, mode] : {std::pair{&clamp_sampler, MTL::SamplerAddressModeClampToEdge}, std::pair{&wrap_sampler, MTL::SamplerAddressModeRepeat}}) {
        if (*sampler) continue;
        auto* sampling = MTL::SamplerDescriptor::alloc()->init();
        sampling->setMinFilter(MTL::SamplerMinMagFilterLinear); sampling->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampling->setMipFilter(MTL::SamplerMipFilterLinear);
        sampling->setSAddressMode(mode); sampling->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
        *sampler = device->newSamplerState(sampling); sampling->release();
    }
}

void rewrite(uint8_t* rdram, const SceneDraw& draw) {
    if (!installed || draw.dl_end <= draw.dl_begin) return;
    SceneId id;
    if (!identify(rdram, draw, id)) return;
    // What goes in place of the parts: text in the reading language, else an HD frame.
    Draw record;
    TextJob job;
    const bool text = describe && describe(rdram, id, job);
    if (text) {
        record.asset = text_asset(job);
        record.text = true;
        record.anchor = job.anchor;
        std::copy(std::begin(job.tint), std::end(job.tint), record.color);
    } else {
        if (!hd_enabled()) return;
        const auto found = std::find_if(images.begin(), images.end(), [&](const Image& image) {
            return image.scene == id.scene && image.atlas == id.atlas && image.palette == id.palette && image.frames.count(id.frame);
        });
        if (found == images.end()) return;
        record.asset = found->asset;
        record.wrap = found->wrap;
        std::copy(std::begin(found->uv), std::end(found->uv), record.uv);
    }
    // 80096CD8: per part F2 SETTILESIZE, then TEXRECT (E4, E1, F1).
    // 8009761C: per part G_VTX of its 4 vertices, the texture load, two F2 SETTILESIZE, G_QUAD.
    std::vector<uint32_t> parts;
    float alpha = 1;
    bool odd = false;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z = 0;
    for (uint32_t p = draw.dl_begin + 8; p + 8 <= draw.dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if (op == 0xFA) { alpha = float(word(rdram, p + 4) & 0xFF) / 255.f; continue; }
        if (!draw.quads && op == 0xE4 && p + 24 <= draw.dl_end) {
            if (word(rdram, p - 8) >> 24 != 0xF2 || word(rdram, p + 8) >> 24 != 0xE1 || word(rdram, p + 16) >> 24 != 0xF1) { odd = true; continue; }
            const uint32_t w1 = word(rdram, p + 4);
            // A flipped part or one clipped at the left edge starts past S = 0.
            if (word(rdram, p + 12) >> 16) odd = true;
            x0 = std::min(x0, ((w1 >> 12) & 0xFFF) / 4.f); y0 = std::min(y0, (w1 & 0xFFF) / 4.f);
            x1 = std::max(x1, ((w0 >> 12) & 0xFFF) / 4.f); y1 = std::max(y1, (w0 & 0xFFF) / 4.f);
            parts.push_back(p);
            p += 16;
        } else if (draw.quads && (op == 0x07 || op == 0x06)) {
            // The part's G_VTX comes before its texture load; the quad follows two SETTILESIZEs.
            uint32_t load = 0;
            for (uint32_t q = p - 8; q > draw.dl_begin && q + 12 * 8 > p; q -= 8) {
                const uint32_t c = word(rdram, q) >> 24;
                if (c == 0x07 || c == 0x06) break;
                if (word(rdram, q) == 0x01004008) { load = q; break; }
            }
            if (!load || word(rdram, p - 8) >> 24 != 0xF2) { odd = true; continue; }
            const uint32_t vertices = word(rdram, load + 4) & 0x1FFFFFFF;
            if (vertices + 64 > 0x800000) { odd = true; continue; }
            for (uint32_t v = 0; v < 4; ++v) {
                const float x = int16_t(half(rdram, vertices + v * 16)), y = int16_t(half(rdram, vertices + v * 16 + 2));
                z = int16_t(half(rdram, vertices + v * 16 + 4));
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
            parts.push_back(p);
        }
    }
    const std::string identity = std::to_string(id.scene) + "/" + std::to_string(id.atlas) + "/" + std::to_string(id.palette) + "/" + std::to_string(id.frame);
    if (parts.empty() || (odd && !text)) {
        ++unexpected;
        json commands = json::array();
        for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end && commands.size() < 48; p += 8) {
            char text[24];
            std::snprintf(text, sizeof text, "%08X %08X", word(rdram, p), word(rdram, p + 4));
            commands.push_back(text);
        }
        note("odd:" + identity, {{"kind", "left original"}, {"scene", id.scene}, {"atlas", id.atlas}, {"palette", id.palette},
             {"frame", id.frame}, {"slot", id.slot}, {"parts", parts.size()}, {"quads", draw.quads}, {"commands", commands}});
        return;
    }
    // An HD frame not decoded yet leaves the original this time; text never shows it.
    if (!ready(record.asset)) {
        ++pending;
        if (!text) return;
    }
    record.quad = draw.quads;
    record.z = z;
    record.color[3] = alpha;
    if (draw.quads) { record.rect[0] = x0; record.rect[1] = y1; record.rect[2] = x1; record.rect[3] = y0; }
    else { record.rect[0] = x0; record.rect[1] = y0; record.rect[2] = x1; record.rect[3] = y1; }
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    note((text ? "text:" : "image:") + identity + (text ? ":" + job.key : ""),
         {{"kind", text ? "text" : "image"}, {"scene", id.scene}, {"atlas", id.atlas}, {"palette", id.palette}, {"frame", id.frame},
          {"slot", id.slot}, {"sub", id.sub}, {"quads", draw.quads}, {"parts", parts.size()}, {"rect", {record.rect[0], record.rect[1], record.rect[2], record.rect[3]}},
          {"z", z}, {"alpha", alpha}, {"key", text ? job.key : std::string()}});
    const uint32_t marker = parts.back();
    for (const uint32_t p : parts) {
        if (p == marker) continue;
        if (draw.quads) { put(rdram, p, 0); put(rdram, p + 4, 0); }
        else for (uint32_t k = 0; k < 24; k += 8) { put(rdram, p + k, 0); put(rdram, p + k + 4, 0); }
    }
    if (!draw.quads) {
        const auto coord = [](float v) { return uint32_t(std::clamp(v * 4.f, 0.f, 4095.f)); };
        put(rdram, marker, 0xE4000000 | coord(x1) << 12 | coord(y1));
        put(rdram, marker + 4, coord(x0) << 12 | coord(y0));
        put(rdram, marker + 8, 0xE1000000); put(rdram, marker + 12, 0);
        put(rdram, marker + 16, 0xF1000000); put(rdram, marker + 20, 0x04000400);
    }
    put(rdram, marker - 8, kTagW0); put(rdram, marker - 4, kTagW1 | record.id);
    ++rewritten;
}

void shutdown() {
    if (!installed) return;
    {
        std::lock_guard lock(asset_mutex);
        stopping = true;
    }
    queue_ready.notify_all();
    if (worker.joinable()) worker.join();
    if (!output.empty()) {
        size_t resident = 0;
        for (const auto& asset : assets) resident += asset->texture != nullptr;
        std::ofstream(output / "scene-sprite-summary.json") << json({{"schema", "srw64.scene-sprite-run.v1"},
            {"frame_images", images.size()}, {"assets", assets.size()}, {"rewritten_draws", rewritten.load()},
            {"native_draws", rendered.load()}, {"waiting_for_texture", pending.load()}, {"skipped", skipped.load()},
            {"left_original", unexpected.load()}, {"decoded", decoded.load()}, {"failed", failures.load()},
            {"released", released.load()}, {"resident_at_exit", resident}}).dump(2) << '\n';
    }
    for (auto& asset : assets) if (asset->texture) { asset->texture->release(); asset->texture = nullptr; }
    for (auto& [key, state] : pipelines) state->release();
    pipelines.clear();
    for (auto& state : depth_states) { if (state) state->release(); state = nullptr; }
    if (clamp_sampler) clamp_sampler->release(); clamp_sampler = nullptr;
    if (wrap_sampler) wrap_sampler->release(); wrap_sampler = nullptr;
    device = nullptr;
}
}
