#define HLSL_CPU
#include "native_marker.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_workload.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_metal.h"
#include "json/json.hpp"
#include "stb/stb_image.h"
#ifdef SRW64_NATIVE_DIALOGUE
#include "localization/catalog.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace srw64::marker {
namespace {
using json = nlohmann::json;
std::vector<uint8_t> reference, vertices, indices;
std::filesystem::path output;
MTL::Device *device{};
MTL::Buffer *vertexBuffer{}, *indexBuffer{};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState *> markerPipelines;
std::array<MTL::DepthStencilState *, 4> depthStates{};
std::atomic<uint64_t> classified{};
std::atomic<uint64_t> original_models{};
uint64_t rendered{}, suppressed{};
// 5600's dashed ring: a 28 x 28 quad at y = 4 textured with 12 yellow dashes, drawn
// by four TRI2 commands (both faces). Redrawn as smooth arcs with the same layout.
constexpr std::array<uint32_t, 4> kRingCommands{0x1a08,0x1a10,0x1a18,0x1a20};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState *> ringPipelines;
uint64_t ringRendered{}, ringSuppressed{};

// Golden beacon (docs/native/native-ship-model.md): the marker bobs and grows in when it
// appears, the ring's dashes turn slowly over a dark backdrop, and a ripple pulses out.
// Host time drives it; the game still decides where the marker is and when it shows.
const auto beaconEpoch = std::chrono::steady_clock::now();
double beaconSeen = -1, beaconAppear = 0;
struct BeaconState { float time, age, scale, bob; };

BeaconState beacon_state() {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - beaconEpoch).count();
    if (now - beaconSeen > 0.25) beaconAppear = now;  // not drawn for a moment: it (re)appeared
    beaconSeen = now;
    const float age = float(now - beaconAppear), a = std::min(age / 0.55f, 1.0f) - 1.0f;
    const float back = 1.70158f;  // ease-out-back: a small overshoot as it lands
    const float scale = std::max(1.0f + (back + 1.0f) * a * a * a + back * a * a, 0.0f);
    return {float(now), age, scale, 1.2f * float(std::sin(now * 2 * M_PI / 2.8))};
}
std::ofstream draws;

// Vertex-coloured replacements for other original models (docs/native/native-ship-model.md).
// Each one is recognised like 5600: segment 4 points at a byte-identical copy of
// the original resource and the triangle command sits at a known offset in it.
constexpr uint32_t kModelIdBase = 0x534D0000;  // 'SM'; low bit set = suppressed original triangles
constexpr size_t kModelStride = 28;           // float3 position, float3 normal, uchar4 colour
struct Model {
    uint32_t resource = 0;
    std::string name;
    std::vector<uint8_t> reference, vertices, indices;
    std::vector<uint32_t> commands;  // sorted; the first carries the native draw
    uint32_t draw_command = 0;
    MTL::Buffer *vertexBuffer{}, *indexBuffer{};
    std::atomic<uint64_t> classified{}, original{};
    uint64_t rendered{}, suppressed{};
    // Name plate (type-5 billboard, 200 x 30 units above the origin), redrawn per locale.
    std::vector<uint32_t> plateCommands;
    uint32_t plateDraw = 0;
    std::array<std::vector<uint8_t>, 3> platePixels;  // RGBA8 per kPlateLocales entry
    int plateWidth = 0, plateHeight = 0;
    std::array<MTL::Texture *, 3> plateTextures{};
    std::atomic<uint64_t> plateClassified{};
    uint64_t plateRendered{}, plateSuppressed{};
};
std::vector<std::unique_ptr<Model>> models;
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState *> modelPipelines;

// Plates follow the reading language (F7); the locale is fixed per workload when the
// display list is classified, so both render targets of a frame agree.
constexpr uint32_t kPlateIdBase = 0x504C0000;  // 'PL'; bits 4+ model, 1-3 locale, 0 suppressed
constexpr std::array<const char *, 3> kPlateLocales{"ja", "zh-Hans", "en"};
std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState *> platePipelines;
MTL::SamplerState *plateSampler{};

uint32_t plate_locale() {
#ifdef SRW64_NATIVE_DIALOGUE
    const std::string locale = srw64::localization::snapshot()->locale;
    for (size_t i = 0; i < kPlateLocales.size(); ++i)
        if (locale == kPlateLocales[i]) return uint32_t(i);
#endif
    return 0;
}

// World-map travel trail (3D33). load_000A7EC0 801C4960 emits one step per quad:
// FA prim (c, c, 255) / E7 / G_VTX 8 at 801C97C0 + 0x40*i / G_QUAD 0,1,2,3. The quads
// are axis-aligned squares on the map, so a diagonal journey reads as a staircase.
// The first quad carries one smooth ribbon through their centres, snapshotted while
// the display list is processed; the others are suppressed.
constexpr uint32_t kTrailIdBase = 0x54520000;  // 'TR'; low bit set = suppressed quad
constexpr size_t kTrailRing = 64;
constexpr size_t kTrailBufferBytes = 4 << 20;
struct TrailPoint { float x, y, z, c; };
struct TrailDraw { uint32_t serial = 0; float halfWidth = 0; std::vector<TrailPoint> points; };
struct Trail {
    bool enabled = false;
    uint32_t codeAddress = 0, vertexBuffer = 0, vertexBytes = 0;
    std::vector<uint8_t> code;  // RDRAM image of the builder's first instructions
    std::array<TrailDraw, kTrailRing> ring;
    std::mutex mutex;
    uint32_t serial = 0;
    std::atomic<uint64_t> classified{}, original{}, snapshots{}, points{};
    uint64_t rendered{}, suppressed{}, stale{};
    MTL::Buffer *buffer{};
    size_t cursor = 0;
    std::map<std::tuple<MTL::PixelFormat, MTL::PixelFormat, NS::UInteger>, MTL::RenderPipelineState *> pipelines;
} trail;

std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing native marker asset: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool hd_mode() {
    // Standalone model probes have no image-mode controller and keep their
    // explicit model selection. Profiles couple it to the applied HD mode.
    return !presentation::image_mode.enabled() || presentation::image_mode.current()==1;
}

uint32_t classify_marker(RT64::State *state, uint32_t base, uintptr_t offset) {
    if (base > 0x800000 - reference.size()) return 0;
    constexpr std::array<uint32_t, 8> commands{0x1a90,0x1a98,0x1aa0,0x1aa8,0x1b18,0x1b20,0x1b28,0x1b30};
    const bool ring = std::find(kRingCommands.begin(), kRingCommands.end(), offset) != kRingCommands.end();
    if (!ring && std::find(commands.begin(), commands.end(), offset) == commands.end()) return 0;
    if (std::memcmp(state->RDRAM + base, reference.data(), reference.size())) return 0;
    // Decide while building the workload. Original mode must retain all eight
    // guest triangles, rather than merely skipping the native GPU draw later.
    // Image-mode changes drain submitted workloads before classification resumes.
    if (!replacement_enabled()) {
        if (offset == commands.front()) ++original_models;
        return 0;
    }
    if (ring) return offset == kRingCommands.front() ? 5602 : 5603;
    ++classified;
    return offset == commands.front() ? 5600 : 5601;
}

uint32_t classify_models(RT64::State *state, uint32_t base, uintptr_t offset) {
    for (size_t i = 0; i < models.size(); ++i) {
        Model& m = *models[i];
        const bool body = std::binary_search(m.commands.begin(), m.commands.end(), offset);
        const bool plate = !body && std::binary_search(m.plateCommands.begin(), m.plateCommands.end(), offset);
        if (!body && !plate) continue;
        if (base > 0x800000 - m.reference.size()) continue;
        if (std::memcmp(state->RDRAM + base, m.reference.data(), m.reference.size())) continue;
        if (plate) {
            // A plate is text: Original images keep the original board only in Japanese.
            const uint32_t locale = plate_locale();
            if (!hd_mode() && locale == 0) return 0;
            ++m.plateClassified;
            return kPlateIdBase | uint32_t(i) << 4 | locale << 1 | (offset == m.plateDraw ? 0 : 1);
        }
        if (!hd_mode()) {
            if (offset == m.draw_command) ++m.original;
            return 0;
        }
        ++m.classified;
        return kModelIdBase | uint32_t(i) << 1 | (offset == m.draw_command ? 0 : 1);
    }
    return 0;
}

int16_t guest_half(const uint8_t *rdram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, rdram + ((address & 0x7FFFFF) ^ 2), 2);
    return int16_t(value);
}

bool trail_quad(const RT64::DisplayList *dl, uint32_t& vertices) {
    if (dl->w0 != 0x07000204 || dl->w1 != 0x00000406 || dl[-1].w0 != 0x01008010
        || dl[-2].w0 != 0xE7000000 || dl[-3].w0 != 0xFA000000) return false;
    vertices = dl[-1].w1;
    return vertices >= trail.vertexBuffer && vertices - trail.vertexBuffer + 0x80 <= trail.vertexBytes
        && (vertices - trail.vertexBuffer) % 0x40 == 0;
}

uint32_t classify_trail(RT64::State *state, const RT64::DisplayList *dl) {
    uint32_t vertices;
    if (!trail_quad(dl, vertices)) return 0;
    if (std::memcmp(state->RDRAM + (trail.codeAddress & 0x7FFFFF), trail.code.data(), trail.code.size())) return 0;
    const bool first = vertices == trail.vertexBuffer;
    if (!hd_mode()) {
        if (first) ++trail.original;
        return 0;
    }
    ++trail.classified;
    if (!first) return kTrailIdBase | 1;
    TrailDraw draw;
    for (const RT64::DisplayList *quad = dl; trail_quad(quad, vertices); quad += 4) {
        float x = 0, y = 0, z = 0, low = 1e9f, high = -1e9f;
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t v = vertices + 16 * k;
            const float vx = guest_half(state->RDRAM, v);
            x += vx; y += guest_half(state->RDRAM, v + 2); z += guest_half(state->RDRAM, v + 4);
            low = std::min(low, vx); high = std::max(high, vx);
        }
        draw.halfWidth = (high - low) / 2;
        draw.points.push_back({x / 4, y / 4, z / 4, float(quad[-3].w1 >> 24) / 255.0f});
    }
    // Step centres are whole map units (4 or 8 apart), so a diagonal journey jitters by
    // half a unit; a centred moving average keeps the ends and straightens the ribbon.
    const std::vector<TrailPoint> raw = draw.points;
    const int count = int(raw.size());
    for (int i = 0; i < count; ++i) {
        const int r = std::min({4, i, count - 1 - i});
        float x = 0, y = 0, z = 0;
        for (int j = i - r; j <= i + r; ++j) { x += raw[j].x; y += raw[j].y; z += raw[j].z; }
        draw.points[i].x = x / (2 * r + 1); draw.points[i].y = y / (2 * r + 1); draw.points[i].z = z / (2 * r + 1);
    }
    ++trail.snapshots; trail.points += draw.points.size();
    std::lock_guard lock(trail.mutex);
    const uint32_t serial = (++trail.serial % 0x7FFF) + 1;
    draw.serial = serial;
    trail.ring[serial % kTrailRing] = std::move(draw);
    return kTrailIdBase | serial << 1;
}

uint32_t classify(RT64::State *state, const RT64::DisplayList *dl) {
    if (trail.enabled)
        if (const uint32_t id = classify_trail(state, dl)) return id;
    // Segment 4 is established by the game each time it calls a model. Never
    // identify an asset by its incidental heap address or by its color alone.
    const uint32_t base = state->rsp->fromSegmentedMasked(0x04000000);
    const auto pointer = reinterpret_cast<uintptr_t>(dl);
    const auto start = reinterpret_cast<uintptr_t>(state->RDRAM) + base;
    if (pointer < start) return 0;
    const uintptr_t offset = pointer - start;
    if (!reference.empty())
        if (const uint32_t id = classify_marker(state, base, offset)) return id;
    return models.empty() ? 0 : classify_models(state, base, offset);
}

// All float4 fields keep the CPU/Metal uniform layout explicit. hlsl++ stores
// rows; Metal interprets these bytes as columns, matching RT64's row-vector math.
struct Uniforms {
    float mvp[16], normalView[16];
    float viewportScale[4], viewportTranslate[4], resolution[4], screen[4];
};

// Shared by both mesh kinds; the vertex layouts differ only after the normal.
constexpr const char *kVertexTransform = R"(
        struct Uniforms {
            float4x4 mvp, normalView;
            float4 viewportScale, viewportTranslate, resolution, screen;
        };
        float4 to_clip(float3 position, constant Uniforms& u) {
            float4 p = u.mvp * float4(position, 1);
            float3 screen = p.xyz / float3(p.w, -p.w, p.w) * u.viewportScale.xyz + u.viewportTranslate.xyz;
            float2 clip = (screen.xy - u.resolution.xy * .5f) / (u.resolution.xy * float2(.5f, -.5f));
            clip = clip * u.screen.xy + u.screen.zw;
            return float4(clip * p.w, screen.z * p.w, p.w);
        }
)";

MTL::RenderPipelineState *build_pipeline(const std::string& body, MTL::Texture *color, MTL::Texture *depth, const char *what, bool blend = false) {
    const std::string source = std::string("#include <metal_stdlib>\nusing namespace metal;\n") + kVertexTransform + body;
    NS::Error *error{};
    auto *library = device->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : std::string(what) + " shader failed");
    auto *vs = library->newFunction(NS::String::string("vs", NS::UTF8StringEncoding));
    auto *fs = library->newFunction(NS::String::string("fs", NS::UTF8StringEncoding));
    auto *desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs); desc->setFragmentFunction(fs);
    auto *attachment = desc->colorAttachments()->object(0);
    attachment->setPixelFormat(color->pixelFormat());
    if (blend) {
        attachment->setBlendingEnabled(true);
        attachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        attachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }
    desc->setDepthAttachmentPixelFormat(depth->pixelFormat());
    desc->setRasterSampleCount(color->sampleCount());
    auto *next = device->newRenderPipelineState(desc, &error);
    desc->release(); vs->release(); fs->release(); library->release();
    if (!next) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : std::string(what) + " pipeline failed");
    return next;
}

MTL::RenderPipelineState *marker_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    // One per target format: with MSAA the native-size and scaled targets can differ.
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = markerPipelines.find(key); found != markerPipelines.end()) return found->second;
    return markerPipelines[key] = build_pipeline(R"(
        struct Vertex { packed_float3 position; packed_float3 normal; };
        struct V { float4 p [[position]]; float3 normal; };
        vertex V vs(uint i [[vertex_id]], const device Vertex *vertices [[buffer(0)]], constant Uniforms& u [[buffer(1)]]) {
            return {to_clip(float3(vertices[i].position), u), (u.normalView * float4(float3(vertices[i].normal), 0)).xyz};
        }
        fragment float4 fs(V in [[stage_in]]) {
            // Faceted gold marker (original eight faces with rounded edges): each flat
            // facet reflects a bright sky or a warm ground by its orientation, and a
            // horizon band sweeps across facets and edges as the game spins it.
            float3 n = normalize(in.normal);
            float3 view = float3(0,0,1);
            float3 key = normalize(float3(-.55f,.8f,1.0f));
            float3 fill = normalize(float3(.75f,.1f,.5f));
            float3 r = reflect(-view, n);
            float sky = smoothstep(-.35f, .85f, r.y);
            float3 env = mix(float3(.55f,.38f,.08f), float3(1.0f,.96f,.80f), sky);
            env += float3(1.0f,.97f,.85f) * .5f * exp(-pow((r.y - .12f) / .07f, 2.0f));
            float diffuse = max(dot(n,key),0.0f);
            float broad = pow(max(dot(n,normalize(key+view)),0.0f),16.0f);
            float sharp = pow(max(dot(n,normalize(float3(-.35f,.65f,1.5f)+view)),0.0f),70.0f);
            float rim = pow(1.0f-abs(dot(n,view)),3.0f);
            float3 gold = float3(1.0f,.82f,.16f);
            float3 c = gold * (.14f + .36f*diffuse + .10f*max(dot(n,fill),0.0f)) + gold * env * .62f;
            c += float3(1.0f,.92f,.65f)*broad*.32f + float3(1.0f,.98f,.9f)*sharp*.7f;
            c += float3(.35f,.24f,.06f)*rim;
            return float4(saturate(c),1);
        }
    )", color, depth, "Native marker");
}

MTL::RenderPipelineState *ring_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = ringPipelines.find(key); found != ringPipelines.end()) return found->second;
    return ringPipelines[key] = build_pipeline(R"(
        struct Beacon { float time, age, scale, bob; };
        struct V { float4 p [[position]]; float2 local; };
        // The ring's plane (y = 4), a little wider than the original 14 so the ripple can clear
        // the dashes; every effect stays within the original ring's footprint.
        vertex V vs(uint vid [[vertex_id]], constant Uniforms& u [[buffer(1)]]) {
            const float2 xz = float2(vid & 1 ? 17.0f : -17.0f, vid >> 1 ? 17.0f : -17.0f);
            return {to_clip(float3(xz.x, 4, xz.y), u), xz};
        }
        float4 over(float4 dst, float3 rgb, float a) {  // straight-alpha "over"
            const float out = a + dst.a * (1.0f - a);
            return out > 0.0f ? float4((rgb * a + dst.rgb * dst.a * (1.0f - a)) / out, out) : float4(0);
        }
        float band(float d, float aa) { return 1.0f - smoothstep(-aa, aa, d); }
        // Dashes keep the original texture's layout: centres at 20.2 + 30k degrees measured as
        // atan2(-z, x), radius 0.87-0.955 of the half size, about 10.5 degrees long; they turn
        // slowly. Beneath them a dark backdrop; a ripple leaves the centre every 2.4 s and fades
        // just past the dashes, and a brighter one marks the arrival.
        fragment float4 fs(V in [[stage_in]], constant Beacon& k [[buffer(0)]]) {
            const float2 p = in.local / max(k.scale, .01f);
            const float r = length(p);
            const float aa = max(fwidth(r), 1e-3f);
            float4 c = float4(0);
            c = over(c, float3(.05f, .04f, 0), .22f * band(r - 8.5f, 2.0f));
            const float cycle = fract(k.time / 2.4f);
            c = over(c, float3(1.0f, .9f, .25f), .5f * pow(1.0f - cycle, 1.5f) * band(abs(r - (5.0f + 10.0f * cycle)) - .4f, aa));
            if (k.age < .8f) {
                const float burst = k.age / .8f;
                c = over(c, float3(1.0f, .92f, .35f), .8f * (1.0f - burst) * band(abs(r - (4.0f + 12.0f * burst)) - .6f, aa));
            }
            const float period = 2.0f * M_PI_F / 12.0f;
            float phase = atan2(-p.y, p.x) - (20.2f + 18.0f * k.time) * M_PI_F / 180.0f;
            phase -= round(phase / period) * period;
            const float centre = 12.8f, halfWidth = .6f, halfLength = 5.25f * M_PI_F / 180.0f * centre;
            const float along = max(abs(phase * r) - (halfLength - halfWidth), 0.0f);
            const float dist = length(float2(along, r - centre)) - halfWidth;  // rounded arc
            const float daa = max(fwidth(dist), 1e-3f);
            c = over(c, float3(.28f, .2f, 0), .45f * band(dist - .3f, daa));  // faint dark edge on bright land
            c = over(c, float3(1.0f, .9f, .12f), band(dist, daa));
            if (c.a <= 0.0f) discard_fragment();
            return c;
        }
    )", color, depth, "Native ring", true);
}

MTL::RenderPipelineState *model_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    // RT64 alternates the native-size and the scaled target; keep one per format.
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = modelPipelines.find(key); found != modelPipelines.end()) return found->second;
    return modelPipelines[key] = build_pipeline(R"(
        struct Vertex { packed_float3 position; packed_float3 normal; uchar4 color; };
        struct V { float4 p [[position]]; float3 normal; float4 color; };
        vertex V vs(uint i [[vertex_id]], const device Vertex *vertices [[buffer(0)]], constant Uniforms& u [[buffer(1)]]) {
            return {to_clip(float3(vertices[i].position), u), (u.normalView * float4(float3(vertices[i].normal), 0)).xyz,
                    float4(vertices[i].color) / 255.0f};
        }
        fragment float4 fs(V in [[stage_in]]) {
            // Colours are authored in display (sRGB) values, like the N64 frame buffer.
            float3 base = in.color.rgb;
            if (in.color.a < .5f) return float4(saturate(base * 1.3f + .08f), 1);  // engine glow
            float3 n = normalize(in.normal);
            float3 view = float3(0,0,1);
            float3 key = normalize(float3(-.5f,.8f,.7f));
            float3 fill = normalize(float3(.7f,-.1f,.5f));
            float diffuse = max(dot(n,key),0.0f);
            float spec = pow(max(dot(n,normalize(key+view)),0.0f),36.0f);
            float rim = pow(1.0f-abs(dot(n,view)),3.0f);
            float3 c = base * (.36f + .64f*diffuse + .15f*max(dot(n,fill),0.0f)) + spec*.2f + base*rim*.1f;
            return float4(saturate(c),1);
        }
    )", color, depth, "Native model");
}

MTL::RenderPipelineState *trail_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = trail.pipelines.find(key); found != trail.pipelines.end()) return found->second;
    return trail.pipelines[key] = build_pipeline(R"(
        struct Params { float halfWidth, count, halo, depthPull; };
        struct V { float4 p [[position]]; float across; float c; };
        // Two vertices per trail step: a strip through the step centres on the map plane,
        // as wide as the original squares plus a faint halo, extended half a square at both ends.
        vertex V vs(uint vid [[vertex_id]], const device float4 *points [[buffer(0)]],
                    constant Uniforms& u [[buffer(1)]], constant Params& k [[buffer(2)]]) {
            const uint n = uint(k.count);
            const uint i = min(vid / 2, n - 1);
            const float side = (vid & 1) ? 1.0f : -1.0f;
            const float3 c = points[i].xyz;
            float2 d = points[min(i + 1, n - 1)].xz - points[i > 0 ? i - 1 : 0].xz;
            d = length(d) > 1e-4f ? normalize(d) : float2(1, 0);
            const float2 across = float2(-d.y, d.x) * k.halfWidth * k.halo * side;
            float2 xz = c.xz + across;
            if (i == 0) xz -= d * k.halfWidth;
            if (i == n - 1) xz += d * k.halfWidth;
            float4 p = to_clip(float3(xz.x, c.y, xz.y), u);
            p.z -= k.depthPull * p.w;  // stay in front of the map plane it lies on
            return {p, side * k.halo, points[i].w};
        }
        fragment float4 fs(V in [[stage_in]], constant Params& k [[buffer(0)]]) {
            const float a = abs(in.across);  // 0 centre, 1 original edge, k.halo halo edge
            const float aa = max(fwidth(in.across), 1e-3f);
            const float core = 1.0f - smoothstep(1.0f - aa, 1.0f + aa, a);
            const float halo = (1.0f - smoothstep(1.0f, k.halo, a)) * .3f;
            float3 color = float3(in.c, in.c, 1.0f);  // the original prim colour (c, c, 255)
            color = mix(color, float3(1), (1.0f - a) * (1.0f - a) * .22f * core);
            return float4(color, max(core, halo));
        }
    )", color, depth, "Native trail", true);
}

// Transforms come from the immutable workload that carried the marked draw.
Uniforms uniforms(const RT64::NativeMeshDraw& call, hlslpp::float4x4 *mvpOut = nullptr, uint32_t *worldIndexOut = nullptr,
                  const hlslpp::float4x4 *local = nullptr) {
    const auto& d = call.workload->drawData;
    const auto worldIndex = d.worldIndices.at(call.vertexIndex);
    const auto& guestWorld = d.lerpWorldTransforms.empty() ? d.worldTransforms.at(worldIndex) : d.lerpWorldTransforms.at(worldIndex);
    const hlslpp::float4x4 guest(guestWorld);
    const hlslpp::float4x4 world = local ? hlslpp::mul(*local, guest) : guest;  // row vectors: local first
    const auto& vp = d.modViewProjTransforms.empty() ? d.viewProjTransforms.at(call.viewProjIndex) : d.modViewProjTransforms.at(call.viewProjIndex);
    const auto& view = d.modViewTransforms.empty() ? d.viewTransforms.at(call.viewProjIndex) : d.modViewTransforms.at(call.viewProjIndex);
    const auto mvp = hlslpp::mul(world, vp);
    const auto normalView = hlslpp::transpose(hlslpp::inverse(hlslpp::mul(world, view)));
    const auto& viewport = d.rspViewports.at(call.viewProjIndex);
    Uniforms u{};
    hlslpp::store(mvp, u.mvp); hlslpp::store(normalView, u.normalView);
    hlslpp::store(viewport.scale, u.viewportScale); hlslpp::store(viewport.translate, u.viewportTranslate);
    u.resolution[0] = call.fbWidth; u.resolution[1] = call.fbHeight;
    u.screen[0] = call.screenScale[0]; u.screen[1] = call.screenScale[1];
    u.screen[2] = call.screenOffset[0]; u.screen[3] = call.screenOffset[1];
    if (mvpOut) *mvpOut = mvp;
    if (worldIndexOut) *worldIndexOut = worldIndex;
    return u;
}

// Draw into the scene's own colour/depth attachments, in the original draw's place.
template <typename Encode>
void encode_pass(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call,
                 const char *label, MTL::RenderPipelineState *(*select)(MTL::Texture *, MTL::Texture *), Encode encode) {
    const auto *fb = static_cast<const plume::MetalFramebuffer *>(framebuffer);
    if (fb->colorAttachments.size() != 1 || !fb->depthAttachment.getTexture())
        throw std::runtime_error("Native marker requires original scene color and depth attachments");
    auto *color = fb->colorAttachments[0].getTexture();
    auto *depth = fb->depthAttachment.getTexture();
    auto *state = select(color, depth);
    auto *command = static_cast<plume::MetalCommandList *>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    auto *pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto *attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(color); attachment->setLoadAction(MTL::LoadActionLoad); attachment->setStoreAction(MTL::StoreActionStore);
    pass->depthAttachment()->setTexture(depth);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionLoad); pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    auto *encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setLabel(NS::String::string(label, NS::UTF8StringEncoding));
    encoder->setRenderPipelineState(state);
    encoder->setDepthStencilState(depthStates[(call.depthCompare ? 1 : 0) | (call.depthWrite ? 2 : 0)]);
    encoder->setViewport(MTL::Viewport{call.viewport.x, call.viewport.y, call.viewport.width, call.viewport.height, call.viewport.minDepth, call.viewport.maxDepth});
    const auto& s = call.scissor;
    encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(s.left), NS::UInteger(s.top), NS::UInteger(s.right-s.left), NS::UInteger(s.bottom-s.top)});
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise); encoder->setCullMode(MTL::CullModeBack);
    encode(encoder);
    encoder->endEncoding();
}

void log_draw(const RT64::NativeMeshDraw& call, uint64_t count, uint32_t resource, size_t triangles,
              const Uniforms& u, const hlslpp::float4x4& mvp, uint32_t worldIndex) {
    const auto& viewport = call.workload->drawData.rspViewports.at(call.viewProjIndex);
    const auto origin = hlslpp::mul(hlslpp::float4(0,0,0,1), mvp);
    const auto screen = (origin.xyz / hlslpp::float3(origin.w,-origin.w,origin.w)) * viewport.scale + viewport.translate;
    std::array<float,3> center{}; hlslpp::store(screen,center.data());
    const auto& s = call.scissor;
    json record = {{"workload",call.workload->workloadId},{"draw",count},{"resource",resource},
        {"image_mode",presentation::image_mode.current()},
        {"triangles",triangles},{"world_index",worldIndex},{"projection_index",call.viewProjIndex},
        {"center",center},{"world",std::vector<float>(u.mvp,u.mvp+16)},
        {"depth_compare",call.depthCompare},{"depth_write",call.depthWrite},{"shared_scene_depth",true},
        {"scissor",{s.left,s.top,s.right,s.bottom}}};
    draws << record.dump() << '\n'; draws.flush();
}

bool render_model(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    const size_t index = (call.id & 0xFFFF) >> 1;
    if (index >= models.size()) return false;
    Model& m = *models[index];
    if (call.id & 1) { ++m.suppressed; return true; }
    if (!device || !call.workload) throw std::runtime_error("Native model missing immutable draw context");
    hlslpp::float4x4 mvp; uint32_t worldIndex{};
    const Uniforms u = uniforms(call, &mvp, &worldIndex);
    const std::string label = "SRW64 native model " + std::to_string(m.resource);
    encode_pass(list, framebuffer, call, label.c_str(), model_pipeline, [&](MTL::RenderCommandEncoder *encoder) {
        encoder->setVertexBuffer(m.vertexBuffer, 0, 0); encoder->setVertexBytes(&u, sizeof(u), 1);
        encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, m.indices.size()/4, MTL::IndexTypeUInt32, m.indexBuffer, 0);
    });
    ++m.rendered;
    if (m.rendered == 1 || m.rendered % 60 == 0) log_draw(call, m.rendered, m.resource, m.indices.size()/12, u, mvp, worldIndex);
    return true;
}

MTL::RenderPipelineState *plate_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    const auto key = std::make_tuple(color->pixelFormat(), depth->pixelFormat(), color->sampleCount());
    if (auto found = platePipelines.find(key); found != platePipelines.end()) return found->second;
    return platePipelines[key] = build_pipeline(R"(
        struct V { float4 p [[position]]; float2 uv; };
        // The board the original plate covers: x -100..100, y 0..30 at z 0.
        vertex V vs(uint vid [[vertex_id]], constant Uniforms& u [[buffer(1)]]) {
            const float2 corner = float2(vid & 1, vid >> 1);
            return {to_clip(float3(mix(-100.0f, 100.0f, corner.x), 30.0f * corner.y, 0), u), float2(corner.x, 1.0f - corner.y)};
        }
        fragment float4 fs(V in [[stage_in]], texture2d<float> plate [[texture(0)]], sampler s [[sampler(0)]]) {
            return float4(plate.sample(s, in.uv).rgb, 1);
        }
    )", color, depth, "Native plate");
}

bool render_trail(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    if (call.id & 1) { ++trail.suppressed; return true; }
    if (!device || !call.workload || !trail.buffer) throw std::runtime_error("Native trail missing draw context");
    const uint32_t serial = (call.id & 0xFFFF) >> 1;
    TrailDraw draw;
    {
        std::lock_guard lock(trail.mutex);
        const TrailDraw& slot = trail.ring[serial % kTrailRing];
        if (slot.serial != serial) { ++trail.stale; return true; }
        draw = slot;
    }
    if (draw.points.empty()) return true;
    const size_t limit = kTrailBufferBytes / 16 / sizeof(TrailPoint);
    if (draw.points.size() > limit) draw.points.erase(draw.points.begin(), draw.points.end() - limit);  // keep the newest
    const size_t bytes = draw.points.size() * sizeof(TrailPoint);
    if (trail.cursor + bytes > kTrailBufferBytes) trail.cursor = 0;
    const size_t offset = trail.cursor;
    std::memcpy(static_cast<uint8_t *>(trail.buffer->contents()) + offset, draw.points.data(), bytes);
    trail.cursor += (bytes + 255) & ~size_t(255);
    const Uniforms u = uniforms(call);
    const struct { float halfWidth, count, halo, depthPull; } params{draw.halfWidth, float(draw.points.size()), 1.45f, 2e-4f};
    encode_pass(list, framebuffer, call, "SRW64 native world-map trail", trail_pipeline, [&](MTL::RenderCommandEncoder *encoder) {
        encoder->setCullMode(MTL::CullModeNone);
        encoder->setDepthStencilState(depthStates[call.depthCompare ? 1 : 0]);  // blended: never write depth
        encoder->setVertexBuffer(trail.buffer, offset, 0);
        encoder->setVertexBytes(&u, sizeof(u), 1);
        encoder->setVertexBytes(&params, sizeof(params), 2);
        encoder->setFragmentBytes(&params, sizeof(params), 0);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(draw.points.size() * 2));
    });
    ++trail.rendered;
    if (trail.rendered == 1 || trail.rendered % 60 == 0) {
        const auto& s = call.scissor;
        draws << json({{"workload",call.workload->workloadId},{"draw",trail.rendered},{"trail_points",draw.points.size()},
            {"half_width",draw.halfWidth},{"first",{draw.points.front().x,draw.points.front().y,draw.points.front().z}},
            {"last",{draw.points.back().x,draw.points.back().y,draw.points.back().z}},
            {"depth_compare",call.depthCompare},{"depth_write",call.depthWrite},{"scissor",{s.left,s.top,s.right,s.bottom}}}).dump() << '\n';
        draws.flush();
    }
    return true;
}

bool render_plate(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    const size_t index = (call.id & 0xFFFF) >> 4;
    if (index >= models.size()) return false;
    Model& m = *models[index];
    if (call.id & 1) { ++m.plateSuppressed; return true; }
    if (!device || !call.workload) throw std::runtime_error("Native plate missing immutable draw context");
    auto *texture = m.plateTextures[std::min<size_t>((call.id >> 1) & 7, kPlateLocales.size() - 1)];
    if (!texture) return false;
    const Uniforms u = uniforms(call);
    encode_pass(list, framebuffer, call, "SRW64 native name plate", plate_pipeline, [&](MTL::RenderCommandEncoder *encoder) {
        encoder->setCullMode(MTL::CullModeNone);
        encoder->setVertexBytes(&u, sizeof(u), 1);
        encoder->setFragmentTexture(texture, 0);
        encoder->setFragmentSamplerState(plateSampler, 0);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    });
    ++m.plateRendered;
    return true;
}

bool render(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) == kPlateIdBase) return render_plate(list, framebuffer, call);
    if ((call.id & 0xFFFF0000u) == kModelIdBase) return render_model(list, framebuffer, call);
    if ((call.id & 0xFFFF0000u) == kTrailIdBase) return render_trail(list, framebuffer, call);
    if (call.id == 5601) { ++suppressed; return true; }
    if (call.id == 5603) { ++ringSuppressed; return true; }
    if (call.id == 5602) {
        if (!device || !call.workload) throw std::runtime_error("Native ring missing immutable draw context");
        const Uniforms u = uniforms(call);
        const BeaconState beacon = beacon_state();
        encode_pass(list, framebuffer, call, "SRW64 native 5600 ring", ring_pipeline, [&](MTL::RenderCommandEncoder *encoder) {
            encoder->setCullMode(MTL::CullModeNone);
            encoder->setDepthStencilState(depthStates[call.depthCompare ? 1 : 0]);  // blended: never write depth
            encoder->setVertexBytes(&u, sizeof(u), 1);
            encoder->setFragmentBytes(&beacon, sizeof(beacon), 0);
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
        });
        ++ringRendered;
        return true;
    }
    if (call.id != 5600) return false;
    if (!device || !call.workload) throw std::runtime_error("Native marker missing immutable draw context");
    hlslpp::float4x4 mvp; uint32_t worldIndex{};
    const BeaconState beacon = beacon_state();
    const float g = beacon.scale;  // grow in, then float gently above the ring
    const hlslpp::float4x4 local(g, 0, 0, 0,  0, g, 0, 0,  0, 0, g, 0,  0, beacon.bob, 0, 1);
    const Uniforms u = uniforms(call, &mvp, &worldIndex, &local);
    encode_pass(list, framebuffer, call, "SRW64 native 5600 marker", marker_pipeline, [&](MTL::RenderCommandEncoder *encoder) {
        encoder->setVertexBuffer(vertexBuffer, 0, 0); encoder->setVertexBytes(&u, sizeof(u), 1);
        encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indices.size()/4, MTL::IndexTypeUInt32, indexBuffer, 0);
    });
    ++rendered;
    if (rendered == 1 || rendered % 60 == 0) log_draw(call, rendered, 5600, indices.size()/12, u, mvp, worldIndex);
    return true;
}

void load_plate(Model& m, const std::filesystem::path& pack, const json& plate) {
    m.plateCommands = plate.at("triangle_commands").get<std::vector<uint32_t>>();
    if (m.plateCommands.empty()) throw std::runtime_error("Native plate without commands");
    m.plateDraw = m.plateCommands.front();
    std::sort(m.plateCommands.begin(), m.plateCommands.end());
    if (m.plateCommands.back() + 8 > m.reference.size()) throw std::runtime_error("Invalid native plate command offsets");
    for (size_t l = 0; l < kPlateLocales.size(); ++l) {
        const auto file = pack / plate.at("textures").at(kPlateLocales[l]).get<std::string>();
        int width = 0, height = 0, channels = 0;
        uint8_t *pixels = stbi_load(file.c_str(), &width, &height, &channels, 4);
        if (!pixels) throw std::runtime_error("Cannot read native plate " + file.string());
        m.platePixels[l].assign(pixels, pixels + size_t(width) * height * 4);
        stbi_image_free(pixels);
        if (l && (width != m.plateWidth || height != m.plateHeight)) throw std::runtime_error("Native plate sizes differ");
        m.plateWidth = width; m.plateHeight = height;
    }
}

void load_models(const std::filesystem::path& pack) {
    std::ifstream stream(pack / "manifest.json");
    if (!stream) throw std::runtime_error("Native model pack without manifest.json: " + pack.string());
    const json manifest = json::parse(stream);
    if (manifest.at("schema") != "srw64.native-models.v1") throw std::runtime_error("Unknown native model pack schema");
    for (const auto& entry : manifest.at("models")) {
        auto m = std::make_unique<Model>();
        m->resource = entry.at("resource_id");
        m->name = entry.at("name");
        m->reference = read(pack / entry.at("reference").get<std::string>());
        m->vertices = read(pack / entry.at("vertices").get<std::string>());
        m->indices = read(pack / entry.at("indices").get<std::string>());
        m->commands = entry.at("triangle_commands").get<std::vector<uint32_t>>();
        if (m->commands.empty() || m->reference.size() != entry.at("reference_bytes").get<size_t>()
            || m->vertices.empty() || m->vertices.size() % kModelStride || m->indices.empty() || m->indices.size() % 12)
            throw std::runtime_error("Invalid native model sizes for resource " + std::to_string(m->resource));
        m->draw_command = m->commands.front();
        std::sort(m->commands.begin(), m->commands.end());
        if (entry.contains("plate") && !entry.at("plate").is_null()) load_plate(*m, pack, entry.at("plate"));
        if (m->draw_command != m->commands.front() || m->commands.back() + 8 > m->reference.size())
            throw std::runtime_error("Invalid native model command offsets");
        const size_t count = m->vertices.size() / kModelStride;
        for (size_t i = 0; i < m->indices.size(); i += 4) {
            uint32_t index; std::memcpy(&index, m->indices.data() + i, 4);
            if (index >= count) throw std::runtime_error("Native model index out of range");
        }
        for (size_t v = 0; v < count; ++v)
            for (size_t k = 0; k < 6; ++k) {
                float value; std::memcpy(&value, m->vertices.data() + v * kModelStride + k * 4, 4);
                if (!std::isfinite(value)) throw std::runtime_error("Native model non-finite vertex");
            }
        models.push_back(std::move(m));
    }
    // Plate-only entries: boards on a resource whose geometry stays original (the space
    // region 5599). No body commands, so only their plate is classified.
    for (const auto& entry : manifest.value("plates", json::array())) {
        auto m = std::make_unique<Model>();
        m->resource = entry.at("resource_id");
        m->name = entry.at("name");
        m->reference = read(pack / entry.at("reference").get<std::string>());
        if (m->reference.size() != entry.at("reference_bytes").get<size_t>())
            throw std::runtime_error("Invalid native plate reference for resource " + std::to_string(m->resource));
        load_plate(*m, pack, entry);
        models.push_back(std::move(m));
    }
    if (manifest.contains("trail")) {
        const auto& t = manifest.at("trail");
        trail.codeAddress = t.at("code_vram");
        trail.vertexBuffer = t.at("vertex_buffer");
        trail.vertexBytes = t.at("vertex_bytes");
        const std::string hex = t.at("code");
        for (size_t i = 0; i + 1 < hex.size(); i += 2) trail.code.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
        if (trail.code.empty() || trail.code.size() % 4 || trail.vertexBytes < 0x80)
            throw std::runtime_error("Invalid native trail description");
        trail.enabled = true;
    }
}
}

bool replacement_enabled() {
    return !reference.empty() && hd_mode();
}

void configure(const std::filesystem::path& directory) {
    output = directory;
    if (const char *pack = std::getenv("SRW64_NATIVE_MARKER"); pack && *pack) {
        const std::filesystem::path path(pack);
        reference = read(path / "reference.bin"); vertices = read(path / "vertices.bin"); indices = read(path / "indices.bin");
        if (reference.size() != 7048 || vertices.empty() || vertices.size()%24 || indices.empty() || indices.size()%12)
            throw std::runtime_error("Invalid native marker asset sizes");
        for (size_t i=0; i<indices.size(); i+=4) {
            uint32_t index; std::memcpy(&index,indices.data()+i,4);
            if (index >= vertices.size()/24) throw std::runtime_error("Native marker index out of range");
        }
        for (size_t i=0; i<vertices.size(); i+=4) {
            float value; std::memcpy(&value,vertices.data()+i,4);
            if (!std::isfinite(value)) throw std::runtime_error("Native marker non-finite vertex");
        }
    }
    if (const char *pack = std::getenv("SRW64_NATIVE_MODELS"); pack && *pack) load_models(pack);
    if (reference.empty() && models.empty() && !trail.enabled) return;
    draws.open(output / "native-model-draws.jsonl");
    RT64::SetNativeMeshHooks(classify, render);
}

void metal_init(plume::RenderDevice *value) {
    if (reference.empty() && models.empty() && !trail.enabled) return;
    device = static_cast<plume::MetalDevice *>(value)->mtl;
    if (!reference.empty()) {
        vertexBuffer = device->newBuffer(vertices.data(),vertices.size(),MTL::ResourceStorageModeShared);
        indexBuffer = device->newBuffer(indices.data(),indices.size(),MTL::ResourceStorageModeShared);
        if (!vertexBuffer || !indexBuffer) throw std::runtime_error("Native marker GPU allocation failed");
    }
    for (auto& m : models) {
        if (m->vertices.empty()) continue;  // plate-only entry
        m->vertexBuffer = device->newBuffer(m->vertices.data(), m->vertices.size(), MTL::ResourceStorageModeShared);
        m->indexBuffer = device->newBuffer(m->indices.data(), m->indices.size(), MTL::ResourceStorageModeShared);
        if (!m->vertexBuffer || !m->indexBuffer) throw std::runtime_error("Native model GPU allocation failed");
    }
    if (trail.enabled && !(trail.buffer = device->newBuffer(kTrailBufferBytes, MTL::ResourceStorageModeShared)))
        throw std::runtime_error("Native trail GPU allocation failed");
    // Plate textures with mipmaps: the board shrinks to a few hundred pixels on screen.
    MTL::CommandQueue *queue = nullptr;
    MTL::CommandBuffer *uploads = nullptr;
    MTL::BlitCommandEncoder *blit = nullptr;
    for (auto& m : models) {
        if (m->plateCommands.empty()) continue;
        if (!queue) { queue = device->newCommandQueue(); uploads = queue->commandBuffer(); blit = uploads->blitCommandEncoder(); }
        for (size_t l = 0; l < kPlateLocales.size(); ++l) {
            auto *desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, m->plateWidth, m->plateHeight, true);
            desc->setUsage(MTL::TextureUsageShaderRead);
            auto *texture = device->newTexture(desc);
            if (!texture) throw std::runtime_error("Native plate GPU allocation failed");
            texture->replaceRegion(MTL::Region(0, 0, m->plateWidth, m->plateHeight), 0, m->platePixels[l].data(), m->plateWidth * 4);
            blit->generateMipmaps(texture);
            m->plateTextures[l] = texture;
        }
    }
    if (queue) {
        blit->endEncoding(); uploads->commit(); uploads->waitUntilCompleted(); queue->release();
        auto *sampler = MTL::SamplerDescriptor::alloc()->init();
        sampler->setMinFilter(MTL::SamplerMinMagFilterLinear); sampler->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler->setMipFilter(MTL::SamplerMipFilterLinear);
        sampler->setSAddressMode(MTL::SamplerAddressModeClampToEdge); sampler->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
        plateSampler = device->newSamplerState(sampler); sampler->release();
    }
    for (size_t i=0;i<depthStates.size();++i) {
        auto *desc = MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthCompareFunction((i&1) ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
        desc->setDepthWriteEnabled(i&2);
        depthStates[i] = device->newDepthStencilState(desc); desc->release();
    }
}

void shutdown() {
    if (reference.empty() && models.empty() && !trail.enabled) return;
    if (!reference.empty())
        std::ofstream(output / "native-model-summary.json") << json({{"schema","srw64.native-marker-run.v1"},
            {"classified_triangles",classified.load()},{"native_draws",rendered},{"suppressed_triangles",suppressed},
            {"original_model_classifications",original_models.load()},
            {"ring_draws",ringRendered},{"ring_suppressed",ringSuppressed},
            {"vertices",vertices.size()/24},{"triangles",indices.size()/12},{"rdram_modified",false}}).dump(2) << '\n';
    if (!models.empty()) {
        json list = json::array();
        for (auto& m : models)
            list.push_back({{"resource",m->resource},{"name",m->name},{"classified_commands",m->classified.load()},
                {"native_draws",m->rendered},{"suppressed_commands",m->suppressed},{"original_draws",m->original.load()},
                {"plate_classified",m->plateClassified.load()},{"plate_draws",m->plateRendered},{"plate_suppressed",m->plateSuppressed},
                {"vertices",m->vertices.size()/kModelStride},{"triangles",m->indices.size()/12}});
        json summary = {{"schema","srw64.native-models-run.v1"},{"models",list},{"rdram_modified",false}};
        if (trail.enabled)
            summary["trail"] = {{"classified_quads",trail.classified.load()},{"snapshots",trail.snapshots.load()},
                {"snapshot_points",trail.points.load()},{"native_draws",trail.rendered},{"suppressed_quads",trail.suppressed},
                {"stale_draws",trail.stale},{"original_draws",trail.original.load()}};
        std::ofstream(output / "native-models-summary.json") << summary.dump(2) << '\n';
    }
    draws.close();
    for (auto& [key, state] : markerPipelines) state->release();
    markerPipelines.clear();
    for (auto& [key, state] : ringPipelines) state->release();
    ringPipelines.clear();
    for (auto& [key, state] : modelPipelines) state->release();
    modelPipelines.clear();
    for (auto& [key, state] : trail.pipelines) state->release();
    trail.pipelines.clear();
    for (auto& [key, state] : platePipelines) state->release();
    platePipelines.clear();
    if (plateSampler) plateSampler->release(); plateSampler=nullptr;
    for (auto& m : models)
        for (auto*& texture : m->plateTextures) { if (texture) texture->release(); texture=nullptr; }
    if (trail.buffer) trail.buffer->release(); trail.buffer=nullptr;
    if (vertexBuffer) vertexBuffer->release(); vertexBuffer=nullptr;
    if (indexBuffer) indexBuffer->release(); indexBuffer=nullptr;
    for (auto& m : models) {
        if (m->vertexBuffer) m->vertexBuffer->release(); m->vertexBuffer=nullptr;
        if (m->indexBuffer) m->indexBuffer->release(); m->indexBuffer=nullptr;
    }
    for(auto*& state:depthStates) { if(state)state->release();state=nullptr; }
    device=nullptr;
}
}
