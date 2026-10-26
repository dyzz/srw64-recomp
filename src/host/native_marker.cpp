#ifndef SRW64_GL_ONLY
#define HLSL_CPU
#endif
#include "native_marker.hpp"
#include "app/runtime.hpp"
#include "presentation/image_mode.hpp"
#ifndef SRW64_GL_ONLY
#include "hle/rt64_state.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_workload.h"
#include "rhi/rt64_render_hooks.h"
#include "native_gpu.hpp"
#else
#include "../gl/gl_api.hpp"
#include "../gl/program_cache.hpp"
#include "../gl/display_list.hpp"
#include "hd_texture.hpp"
#endif
#include "json/json.hpp"
#include "stb/stb_image.h"
#include "app/rom_import_codec.hpp"
#include "app/sha256.hpp"
#include "librecomp/game.hpp"
#ifdef SRW64_NATIVE_DIALOGUE
#include "localization/catalog.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

#ifdef __SWITCH__
extern "C" void srw64_switch_background_thread();   // host/switch/switch_platform.cpp
#endif

namespace srw64::marker {
namespace {
using json = nlohmann::json;
std::vector<uint8_t> reference, vertices, indices;
std::filesystem::path output;
#ifndef SRW64_GL_ONLY
// src/host/shaders/Hd{Marker,Ring,Model,Trail,Plate}{VS,PS}.hlsl (NativeMarker.hlsli).
std::unique_ptr<gpu::Program> markerProgram, ringProgram, modelProgram, trailProgram, plateProgram, bakedProgram, waterProgram;
std::unique_ptr<gpu::Buffer> vertexBuffer, indexBuffer;
std::unique_ptr<plume::RenderDescriptorSet> markerMesh;
#endif
std::atomic<uint64_t> classified{};
std::atomic<uint64_t> original_models{};
uint64_t rendered{}, suppressed{};
// 5600's dashed ring: a 28 x 28 quad at y = 4 textured with 12 yellow dashes, drawn
// by four TRI2 commands (both faces). Redrawn as smooth arcs with the same layout.
constexpr std::array<uint32_t, 4> kRingCommands{0x1a08,0x1a10,0x1a18,0x1a20};
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
// Battle backgrounds (docs/design/battle-animation-rendering.md §11): 'baked' meshes add a
// float2 uv and draw their lighting from an atlas; 'water' is a plane shaded with waves.
constexpr size_t kTexturedStride = 36;
enum class Shading : uint8_t { colour, baked, water, hidden };  // hidden: suppressed, nothing drawn
struct Model {
    uint32_t resource = 0;
    std::string name;
    Shading shading = Shading::colour;
    float alpha = 1;   // water: below 1 blends over what is under it, as the original's layer
    size_t stride = kModelStride;
    std::vector<uint8_t> atlasPixels;
    int atlasWidth = 0, atlasHeight = 0;
#ifndef SRW64_GL_ONLY
    std::unique_ptr<gpu::Texture> atlas;
#else
    hd_texture::Compressed atlasPrepared;   // the Switch's BC7 copy of the atlas (hd_texture.hpp)
    uint32_t glAtlas = 0, glVertices = 0, glIndices = 0;   // made on first draw (native_marker GL)
    std::array<uint32_t, 3> glPlates{};
#endif
    std::vector<uint8_t> reference, vertices, indices;  // reference: taken from the ROM, see originals_ready()
    size_t referenceBytes = 0;
    std::vector<uint32_t> commands;  // sorted; the first carries the native draw
    uint32_t draw_command = 0;
#ifndef SRW64_GL_ONLY
    std::unique_ptr<gpu::Buffer> vertexBuffer, indexBuffer;
    std::unique_ptr<plume::RenderDescriptorSet> mesh;
#endif
    std::atomic<uint64_t> classified{}, original{};
    uint64_t rendered{}, suppressed{};
    // Name plate (type-5 billboard, 200 x 30 units above the origin), redrawn per locale.
    std::vector<uint32_t> plateCommands;
    uint32_t plateDraw = 0;
    std::array<std::vector<uint8_t>, 3> platePixels;  // RGBA8 per kPlateLocales entry
    int plateWidth = 0, plateHeight = 0;
#ifndef SRW64_GL_ONLY
    std::array<std::unique_ptr<gpu::Texture>, 3> plateTextures;
    std::array<std::unique_ptr<plume::RenderDescriptorSet>, 3> plateSets;
#endif
    std::atomic<uint64_t> plateClassified{};
    uint64_t plateRendered{}, plateSuppressed{};
};
std::vector<std::unique_ptr<Model>> models;

// Plates follow the reading language (F7); the locale is fixed per workload when the
// display list is classified, so both render targets of a frame agree. The model pack has
// no Traditional Chinese plates: zh-Hant shows the Simplified ones.
constexpr uint32_t kPlateIdBase = 0x504C0000;  // 'PL'; bits 4+ model, 1-3 locale, 0 suppressed
constexpr std::array<const char *, 3> kPlateLocales{"ja", "zh-Hans", "en"};

uint32_t plate_locale() {
#ifdef SRW64_NATIVE_DIALOGUE
    std::string locale = srw64::localization::snapshot()->locale;
    if (locale == "zh-Hant") locale = "zh-Hans";
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
} trail;

std::vector<uint8_t> read(const std::filesystem::path& path) {
    // Read whole (read_text): through a stream iterator, a byte at a time, the packs' 26 MB
    // took the Switch some 7 s.
    const std::string bytes = app::read_text(path, 256u << 20);
    return {bytes.begin(), bytes.end()};
}

// Packs carry no ROM bytes: each names an original resource and the SHA-256 of its
// decoded bytes, and the host takes it from the player's ROM (docs/native/native-ship-model.md).
constexpr size_t kResourceTable = 0x00A20BD0;

std::span<const uint8_t> player_rom() {
    if (recomp::is_rom_loaded()) return recomp::get_rom();
    // Frame probes replay display lists without loading a game.
    static std::vector<uint8_t> file;
    if (file.empty()) {
        const char *path = std::getenv("SRW64_ROM_PATH");
        if (!path || !*path) throw std::runtime_error("Native model packs need the game ROM (SRW64_ROM_PATH)");
        file = read(path);
        if (file.size() < 4 || file[0] != 0x80 || file[1] != 0x37) throw std::runtime_error("SRW64_ROM_PATH is not a .z64 ROM");
    }
    return file;
}

std::string sha256(std::span<const uint8_t> bytes) {
    srw64::app::Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

// RDRAM holds big-endian words byte-swapped on a little-endian host.
std::vector<uint8_t> rdram_image(std::span<const uint8_t> bytes) {
    std::vector<uint8_t> image(bytes.begin(), bytes.end());
    for (size_t i = 0; i < image.size(); i += 4) std::reverse(image.begin() + i, image.begin() + std::min(i + 4, image.size()));
    return image;
}

std::vector<uint8_t> original_resource(uint32_t id, size_t bytes, const std::string& expected) {
    const auto decoded = srw64::app::rom_import::resource(player_rom(), kResourceTable, id).bytes;
    if (decoded.size() != bytes || sha256(decoded) != expected)
        throw std::runtime_error("ROM resource " + std::to_string(id) + " differs from the one the pack was made for");
    return rdram_image(decoded);
}

bool markerPack = false;
struct PendingOriginal { std::vector<uint8_t> *target; uint32_t id; size_t bytes; std::string sha256; };
std::vector<PendingOriginal> pendingOriginals;
struct { size_t rom = 0, bytes = 0; std::string sha256; } pendingTrail;

// The renderer is configured before the game loads its ROM, so the originals are taken
// from it when the first display list is classified. Without them nothing is replaced.
bool originals_ready() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
        try {
            for (const auto& p : pendingOriginals) *p.target = original_resource(p.id, p.bytes, p.sha256);
            if (trail.enabled) {
                const auto code = srw64::app::rom_import::slice(player_rom(), pendingTrail.rom, pendingTrail.bytes);
                if (sha256(code) != pendingTrail.sha256) throw std::runtime_error("ROM trail code differs from the pack's");
                trail.code = rdram_image(code);
            }
            ready = true;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64 native models disabled: %s\n", error.what());
        }
    });
    return ready;
}

bool hd_mode() {
    // Standalone model probes have no image-mode controller and keep their
    // explicit model selection. Profiles couple it to the applied HD mode.
    return !presentation::image_mode.enabled() || presentation::image_mode.current()==1;
}

// A display-list command as RDRAM holds it (RT64::DisplayList's layout).
struct Words { uint32_t w0, w1; };

uint32_t classify_marker(const uint8_t *rdram, uint32_t base, uintptr_t offset) {
    if (base > 0x800000 - reference.size()) return 0;
    constexpr std::array<uint32_t, 8> commands{0x1a90,0x1a98,0x1aa0,0x1aa8,0x1b18,0x1b20,0x1b28,0x1b30};
    const bool ring = std::find(kRingCommands.begin(), kRingCommands.end(), offset) != kRingCommands.end();
    if (!ring && std::find(commands.begin(), commands.end(), offset) == commands.end()) return 0;
    if (std::memcmp(rdram + base, reference.data(), reference.size())) return 0;
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

uint32_t classify_models(const uint8_t *rdram, uint32_t base, uintptr_t offset) {
    for (size_t i = 0; i < models.size(); ++i) {
        Model& m = *models[i];
        const bool body = std::binary_search(m.commands.begin(), m.commands.end(), offset);
        const bool plate = !body && std::binary_search(m.plateCommands.begin(), m.plateCommands.end(), offset);
        if (!body && !plate) continue;
        if (base > 0x800000 - m.reference.size()) continue;
        if (std::memcmp(rdram + base, m.reference.data(), m.reference.size())) continue;
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

bool trail_quad(const Words *dl, uint32_t& vertices) {
    if (dl->w0 != 0x07000204 || dl->w1 != 0x00000406 || dl[-1].w0 != 0x01008010
        || dl[-2].w0 != 0xE7000000 || dl[-3].w0 != 0xFA000000) return false;
    vertices = dl[-1].w1;
    return vertices >= trail.vertexBuffer && vertices - trail.vertexBuffer + 0x80 <= trail.vertexBytes
        && (vertices - trail.vertexBuffer) % 0x40 == 0;
}

uint32_t classify_trail(const uint8_t *rdram, const Words *dl) {
    uint32_t vertices;
    if (!trail_quad(dl, vertices)) return 0;
    if (std::memcmp(rdram + (trail.codeAddress & 0x7FFFFF), trail.code.data(), trail.code.size())) return 0;
    const bool first = vertices == trail.vertexBuffer;
    if (!hd_mode()) {
        if (first) ++trail.original;
        return 0;
    }
    ++trail.classified;
    if (!first) return kTrailIdBase | 1;
    TrailDraw draw;
    for (const Words *quad = dl; trail_quad(quad, vertices); quad += 4) {
        float x = 0, y = 0, z = 0, low = 1e9f, high = -1e9f;
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t v = vertices + 16 * k;
            const float vx = guest_half(rdram, v);
            x += vx; y += guest_half(rdram, v + 2); z += guest_half(rdram, v + 4);
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

// Segment 4 is established by the game each time it calls a model. Never identify an asset
// by its incidental heap address or by its color alone. `dl` is the triangle command in RDRAM.
uint32_t classify_at(const uint8_t *rdram, const Words *dl, uint32_t base) {
    if (!originals_ready()) return 0;
    if (trail.enabled)
        if (const uint32_t id = classify_trail(rdram, dl)) return id;
    const auto pointer = reinterpret_cast<uintptr_t>(dl);
    const auto start = reinterpret_cast<uintptr_t>(rdram) + base;
    if (pointer < start) return 0;
    const uintptr_t offset = pointer - start;
    if (markerPack)
        if (const uint32_t id = classify_marker(rdram, base, offset)) return id;
    return models.empty() ? 0 : classify_models(rdram, base, offset);
}

#ifndef SRW64_GL_ONLY
uint32_t classify(RT64::State *state, const RT64::DisplayList *dl) {
    return classify_at(state->RDRAM, reinterpret_cast<const Words *>(dl), state->rsp->fromSegmentedMasked(0x04000000));
}

// All float4 fields keep the CPU/Metal uniform layout explicit. hlsl++ stores
// rows; Metal interprets these bytes as columns, matching RT64's row-vector math.
struct Uniforms {
    float mvp[16], normalView[16];
    float viewportScale[4], viewportTranslate[4], resolution[4], screen[4];
};

// Transforms come from the immutable workload that carried the marked draw.
Uniforms uniforms(const RT64::NativeMeshDraw& call, hlslpp::float4x4 *mvpOut = nullptr, uint32_t *worldIndexOut = nullptr,
                  const hlslpp::float4x4 *local = nullptr, hlslpp::float4x4 *modelViewOut = nullptr) {
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
    if (modelViewOut) *modelViewOut = hlslpp::mul(world, view);
    return u;
}

// Draw into the scene's own colour/depth attachments, in the original draw's place:
// the transform (NativeMarker.hlsli) then `extra` as this draw's data.
bool draw(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call,
          const gpu::Program& program, const gpu::State& state, plume::RenderDescriptorSet *set, const Uniforms& u,
          uint32_t vertices, const void *extra = nullptr, size_t extra_bytes = 0) {
    std::vector<uint8_t> data(sizeof(Uniforms) + extra_bytes);
    std::memcpy(data.data(), &u, sizeof(Uniforms));
    if (extra_bytes) std::memcpy(data.data() + sizeof(Uniforms), extra, extra_bytes);
    if (!program.begin(list, framebuffer, call, state, set, gpu::push_data(data.data(), data.size()))) return false;
    list->drawInstanced(vertices, 1, 0, 0);
    return true;
}

// Meshes as the original encoder had them: the game's depth test and write,
// counter-clockwise front faces, back faces culled.
gpu::State mesh_state(const RT64::NativeMeshDraw& call) {
    gpu::State state;
    state.depth_test = call.depthCompare;
    state.depth_write = call.depthWrite;
    state.cull = plume::RenderCullMode::BACK;
    state.counter_clockwise = true;
    state.topology = plume::RenderPrimitiveTopology::TRIANGLE_LIST;
    return state;
}

// Blended overlays (ring, trail) test depth like the game but never write it.
gpu::State overlay_state(const RT64::NativeMeshDraw& call) {
    gpu::State state;
    state.blend = gpu::Blend::straight;
    state.depth_test = call.depthCompare;
    return state;
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
    if (!call.workload) throw std::runtime_error("Native model missing immutable draw context");
    hlslpp::float4x4 mvp, modelView; uint32_t worldIndex{};
    const Uniforms u = uniforms(call, &mvp, &worldIndex, nullptr, &modelView);
    const uint32_t count = uint32_t(m.indices.size() / 4);
    if (m.shading == Shading::hidden) {
        ++m.suppressed; return true;
    } else if (m.shading == Shading::baked) {
        // The atlas uploads on first draw, on RT64's workload command list, like the plates.
        if (!m.mesh) {
            if (!bakedProgram || !m.atlas || !m.atlas->upload(list)) return false;
            m.mesh = bakedProgram->bind({m.vertexBuffer.get(), m.indexBuffer.get(), m.atlas.get()});
        }
        if (!draw(list, framebuffer, call, *bakedProgram, mesh_state(call), m.mesh.get(), u, count)) return false;
    } else if (m.shading == Shading::water) {
        if (!waterProgram || !m.mesh) throw std::runtime_error("Native water missing its program");
        struct { float modelView[16]; float time[4]; } extra{};
        hlslpp::store(modelView, extra.modelView);
        static const auto start = std::chrono::steady_clock::now();
        extra.time[0] = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        extra.time[1] = m.alpha;
        gpu::State state = mesh_state(call);
        if (m.alpha < 1) state.blend = gpu::Blend::straight;
        if (!draw(list, framebuffer, call, *waterProgram, state, m.mesh.get(), u, count, &extra, sizeof(extra))) return false;
    } else {
        if (!modelProgram || !m.mesh) throw std::runtime_error("Native model missing immutable draw context");
        if (!draw(list, framebuffer, call, *modelProgram, mesh_state(call), m.mesh.get(), u, count)) return false;
    }
    ++m.rendered;
    if (m.rendered == 1 || m.rendered % 60 == 0) log_draw(call, m.rendered, m.resource, m.indices.size()/12, u, mvp, worldIndex);
    return true;
}

bool render_trail(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    if (call.id & 1) { ++trail.suppressed; return true; }
    if (!trailProgram || !call.workload) throw std::runtime_error("Native trail missing draw context");
    const uint32_t serial = (call.id & 0xFFFF) >> 1;
    TrailDraw draw;
    {
        std::lock_guard lock(trail.mutex);
        const TrailDraw& slot = trail.ring[serial % kTrailRing];
        if (slot.serial != serial) { ++trail.stale; return true; }
        draw = slot;
    }
    if (draw.points.empty()) return true;
    // One draw's data holds the transform, the parameters and the points (HdTrailVS).
    const size_t limit = gpu::kMaxDrawData - sizeof(Uniforms) / 16 - 1;
    if (draw.points.size() > limit) draw.points.erase(draw.points.begin(), draw.points.end() - limit);  // keep the newest
    const Uniforms u = uniforms(call);
    std::vector<float> extra(4 + draw.points.size() * 4);
    extra[0] = draw.halfWidth; extra[1] = float(draw.points.size()); extra[2] = 1.45f; extra[3] = 2e-4f;  // halo, depth pull
    std::memcpy(&extra[4], draw.points.data(), draw.points.size() * sizeof(TrailPoint));
    if (!::srw64::marker::draw(list, framebuffer, call, *trailProgram, overlay_state(call), nullptr, u,
                               uint32_t(draw.points.size() * 2), extra.data(), extra.size() * sizeof(float))) return false;
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
    if (!plateProgram || !call.workload) throw std::runtime_error("Native plate missing immutable draw context");
    const size_t locale = std::min<size_t>((call.id >> 1) & 7, kPlateLocales.size() - 1);
    auto& texture = m.plateTextures[locale];
    if (!texture) return false;
    if (!m.plateSets[locale]) {
        if (!texture->upload(list)) return false;
        m.plateSets[locale] = plateProgram->bind({texture.get()});
    }
    const Uniforms u = uniforms(call);
    gpu::State state;  // opaque board, the game's depth test and write
    state.depth_test = call.depthCompare;
    state.depth_write = call.depthWrite;
    if (!draw(list, framebuffer, call, *plateProgram, state, m.plateSets[locale].get(), u, 4)) return false;
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
        if (!ringProgram || !call.workload) throw std::runtime_error("Native ring missing immutable draw context");
        const Uniforms u = uniforms(call);
        const BeaconState beacon = beacon_state();
        if (!draw(list, framebuffer, call, *ringProgram, overlay_state(call), nullptr, u, 4, &beacon, sizeof(beacon))) return false;
        ++ringRendered;
        return true;
    }
    if (call.id != 5600) return false;
    if (!markerProgram || !markerMesh || !call.workload) throw std::runtime_error("Native marker missing immutable draw context");
    hlslpp::float4x4 mvp; uint32_t worldIndex{};
    const BeaconState beacon = beacon_state();
    const float g = beacon.scale;  // grow in, then float gently above the ring
    const hlslpp::float4x4 local(g, 0, 0, 0,  0, g, 0, 0,  0, 0, g, 0,  0, beacon.bob, 0, 1);
    const Uniforms u = uniforms(call, &mvp, &worldIndex, &local);
    if (!draw(list, framebuffer, call, *markerProgram, mesh_state(call), markerMesh.get(), u, uint32_t(indices.size() / 4))) return false;
    ++rendered;
    if (rendered == 1 || rendered % 60 == 0) log_draw(call, rendered, 5600, indices.size()/12, u, mvp, worldIndex);
    return true;
}

#endif  // RT64's draws

#ifdef SRW64_GL_ONLY
// The same draws on the GL renderer (src/gl, the Switch): the Hd{Marker,Ring,Model,Trail,
// Plate,Baked,Water} shaders in GLSL. The interpreter hands a model's place over as a native
// command (n64gl::Command, from classify_gl) with the game's transform to the picture's clip
// space and its model-view; draw_gl paints it there with the game's depth test.
#ifdef SRW64_GLES
constexpr const char *kGlPreamble = "#version 100\n#extension GL_OES_standard_derivatives : enable\nprecision highp float;\n";
#else
constexpr const char *kGlPreamble = "#version 120\n";
#endif
// Row-vector matrices (n64gl::Command's) handed to GLSL as they are read column-major: the
// transpose, so `matrix * vector` there is the row vector times the matrix here.
constexpr const char *kGlMeshVS =
    "attribute vec3 a_position;\nattribute vec3 a_normal;\nattribute vec4 a_color;\nattribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\nuniform mat3 u_normal;\nuniform mat4 u_modelview;\nuniform float u_pull;\n"
    "varying vec3 v_normal;\nvarying vec4 v_color;\nvarying vec2 v_uv;\nvarying vec3 v_view;\nvarying vec2 v_plane;\n"
    "void main() {\n  gl_Position = u_mvp * vec4(a_position, 1.0);\n  gl_Position.z -= u_pull * gl_Position.w;\n"
    "  v_normal = u_normal * a_normal;\n  v_color = a_color;\n  v_uv = a_uv;\n"
    "  v_view = (u_modelview * vec4(a_position, 1.0)).xyz;\n  v_plane = a_position.xz;\n}\n";
constexpr const char *kGlVaryings =
    "varying vec3 v_normal;\nvarying vec4 v_color;\nvarying vec2 v_uv;\nvarying vec3 v_view;\nvarying vec2 v_plane;\n";
constexpr const char *kGlModelPS =   // HdModelPS
    "void main() {\n  vec3 base = v_color.rgb;\n"
    "  if (v_color.a < 0.5) { gl_FragColor = vec4(clamp(base * 1.3 + 0.08, 0.0, 1.0), 1.0); return; }\n"
    "  vec3 n = normalize(v_normal), view = vec3(0.0, 0.0, 1.0);\n"
    "  vec3 key = normalize(vec3(-0.5, 0.8, 0.7)), fill = normalize(vec3(0.7, -0.1, 0.5));\n"
    "  float diffuse = max(dot(n, key), 0.0);\n  float spec = pow(max(dot(n, normalize(key + view)), 0.0), 36.0);\n"
    "  float rim = pow(1.0 - abs(dot(n, view)), 3.0);\n"
    "  vec3 c = base * (0.36 + 0.64 * diffuse + 0.15 * max(dot(n, fill), 0.0)) + spec * 0.2 + base * rim * 0.1;\n"
    "  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n}\n";
constexpr const char *kGlMarkerPS =   // HdMarkerPS
    "void main() {\n  vec3 n = normalize(v_normal), view = vec3(0.0, 0.0, 1.0);\n"
    "  vec3 key = normalize(vec3(-0.55, 0.8, 1.0)), fill = normalize(vec3(0.75, 0.1, 0.5));\n"
    "  vec3 r = reflect(-view, n);\n  float sky = smoothstep(-0.35, 0.85, r.y);\n"
    "  vec3 env = mix(vec3(0.55, 0.38, 0.08), vec3(1.0, 0.96, 0.80), sky);\n"
    "  env += vec3(1.0, 0.97, 0.85) * 0.5 * exp(-pow((r.y - 0.12) / 0.07, 2.0));\n"
    "  float diffuse = max(dot(n, key), 0.0);\n  float broad = pow(max(dot(n, normalize(key + view)), 0.0), 16.0);\n"
    "  float sharp = pow(max(dot(n, normalize(vec3(-0.35, 0.65, 1.5) + view)), 0.0), 70.0);\n"
    "  float rim = pow(1.0 - abs(dot(n, view)), 3.0);\n  vec3 gold = vec3(1.0, 0.82, 0.16);\n"
    "  vec3 c = gold * (0.14 + 0.36 * diffuse + 0.10 * max(dot(n, fill), 0.0)) + gold * env * 0.62;\n"
    "  c += vec3(1.0, 0.92, 0.65) * broad * 0.32 + vec3(1.0, 0.98, 0.9) * sharp * 0.7;\n"
    "  c += vec3(0.35, 0.24, 0.06) * rim;\n  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n}\n";
constexpr const char *kGlRingPS =   // HdRingPS; v_uv is the ring plane's x, z
    "uniform vec4 u_beacon;\n"
    "vec4 over(vec4 dst, vec3 rgb, float a) {\n  float result = a + dst.a * (1.0 - a);\n"
    "  return result > 0.0 ? vec4((rgb * a + dst.rgb * dst.a * (1.0 - a)) / result, result) : vec4(0.0);\n}\n"
    "float band(float d, float aa) { return 1.0 - smoothstep(-aa, aa, d); }\n"
    "void main() {\n  float time = u_beacon.x, age = u_beacon.y, scale = u_beacon.z;\n"
    "  vec2 p = v_uv / max(scale, 0.01);\n  float r = length(p);\n  float aa = max(fwidth(r), 1e-3);\n"
    "  vec4 c = vec4(0.0);\n  c = over(c, vec3(0.05, 0.04, 0.0), 0.22 * band(r - 8.5, 2.0));\n"
    "  float cycle = fract(time / 2.4);\n"
    "  c = over(c, vec3(1.0, 0.9, 0.25), 0.5 * pow(1.0 - cycle, 1.5) * band(abs(r - (5.0 + 10.0 * cycle)) - 0.4, aa));\n"
    "  if (age < 0.8) {\n    float burst = age / 0.8;\n"
    "    c = over(c, vec3(1.0, 0.92, 0.35), 0.8 * (1.0 - burst) * band(abs(r - (4.0 + 12.0 * burst)) - 0.6, aa));\n  }\n"
    "  float period = 2.0 * 3.14159265 / 12.0;\n"
    "  float phase = atan(-p.y, p.x) - (20.2 + 18.0 * time) * 3.14159265 / 180.0;\n"
    "  phase -= floor(phase / period + 0.5) * period;\n"
    "  float centre = 12.8, halfWidth = 0.6, halfLength = 5.25 * 3.14159265 / 180.0 * centre;\n"
    "  float along = max(abs(phase * r) - (halfLength - halfWidth), 0.0);\n"
    "  float dist = length(vec2(along, r - centre)) - halfWidth;\n  float daa = max(fwidth(dist), 1e-3);\n"
    "  c = over(c, vec3(0.28, 0.2, 0.0), 0.45 * band(dist - 0.3, daa));\n"
    "  c = over(c, vec3(1.0, 0.9, 0.12), band(dist, daa));\n"
    "  if (c.a <= 0.0) discard;\n  gl_FragColor = c;\n}\n";
constexpr const char *kGlTrailPS =   // HdTrailPS; v_uv: across, the prim colour
    "uniform float u_halo;\n"
    "void main() {\n  float a = abs(v_uv.x);\n  float aa = max(fwidth(v_uv.x), 1e-3);\n"
    "  float core = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, a);\n  float halo = (1.0 - smoothstep(1.0, u_halo, a)) * 0.3;\n"
    "  vec3 colour = vec3(v_uv.y, v_uv.y, 1.0);\n  colour = mix(colour, vec3(1.0), (1.0 - a) * (1.0 - a) * 0.22 * core);\n"
    "  gl_FragColor = vec4(colour, max(core, halo));\n}\n";
constexpr const char *kGlPlatePS =   // HdPlatePS
    "uniform sampler2D u_texture;\nvoid main() { gl_FragColor = vec4(texture2D(u_texture, v_uv).rgb, 1.0); }\n";
constexpr const char *kGlBakedPS =   // HdBakedPS
    "uniform sampler2D u_texture;\n"
    "void main() {\n  vec3 c = texture2D(u_texture, v_uv).rgb;\n"
    "  if (v_color.a < 0.9) {\n    vec3 n = normalize(v_normal), view = vec3(0.0, 0.0, 1.0);\n"
    "    vec3 key = normalize(vec3(-0.5, 0.8, 0.7));\n"
    "    float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(abs(dot(n, view)), 0.0, 1.0), 5.0);\n"
    "    vec3 sky = vec3(0.72, 0.82, 0.95);\n    float spec = pow(clamp(dot(n, normalize(key + view)), 0.0, 1.0), 90.0);\n"
    "    c = mix(c, sky, clamp(fresnel * 0.8, 0.0, 1.0)) + spec * 0.35;\n  }\n"
    "  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n}\n";
constexpr const char *kGlWaterPS =   // HdWaterPS; the waves as a uniform (no constant arrays in GLSL ES 1.00)
    "uniform vec3 u_wave[16];\nuniform vec2 u_water;\nuniform mat3 u_normal;\n"
    "vec3 srgb(vec3 v) { return pow(clamp(v, 0.0, 1.0), vec3(1.0 / 2.2)); }\nvec3 lin(vec3 v) { return pow(v, vec3(2.2)); }\n"
    "void main() {\n  float time = u_water.x;\n  vec2 slope = vec2(0.0);\n"
    "  for (int i = 0; i < 16; ++i) {\n    vec2 kv = 2.0 * 3.14159265 * u_wave[i].xy / 1000.0;\n    float k = length(kv);\n"
    "    float omega = sqrt(9.8 * k * 40.0);\n    slope += kv / k * u_wave[i].z * cos(dot(kv, v_plane) - omega * time * 0.12);\n  }\n"
    "  float distance = length(v_view);\n  slope *= mix(1.0, 0.45, clamp(distance / 1600.0, 0.0, 1.0));\n"
    "  vec3 up = normalize(u_normal * vec3(0.0, 1.0, 0.0));\n  vec3 n = normalize(u_normal * vec3(-slope.x, 1.0, -slope.y));\n"
    "  vec3 v = normalize(-v_view);\n  vec3 r = reflect(-v, n);\n"
    "  vec3 zenith = lin(vec3(0.28, 0.46, 0.78)), horizon = lin(vec3(0.62, 0.74, 0.90));\n"
    "  float elevation = clamp(dot(r, up), 0.0, 1.0);\n  vec3 sky = mix(horizon, zenith, pow(elevation, 0.6));\n"
    "  vec3 deep = lin(vec3(0.02, 0.11, 0.30)), shallow = lin(vec3(0.04, 0.20, 0.42));\n"
    "  float facing = clamp(dot(n, v), 0.0, 1.0);\n  float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);\n"
    "  vec3 water = mix(shallow, deep, clamp(facing * 1.4, 0.0, 1.0));\n  vec3 c = mix(water, sky, fresnel * 0.75);\n"
    "  vec3 sun = normalize(u_normal * vec3(-0.25, 0.30, -0.92));\n"
    "  float glint = pow(clamp(dot(r, sun), 0.0, 1.0), 2500.0) * 5.0 + pow(clamp(dot(r, sun), 0.0, 1.0), 120.0) * 0.12;\n"
    "  c += glint * vec3(1.0, 0.95, 0.85);\n  c += clamp(length(slope) * 2.5 - 0.6, 0.0, 1.0) * 0.06;\n"
    "  c = mix(c, horizon, clamp((distance - 700.0) / 2200.0, 0.0, 1.0) * 0.7);\n"
    "  float alpha = u_water.y;\n"
    "  gl_FragColor = vec4(srgb(c), alpha >= 1.0 ? 1.0 : clamp(mix(alpha, 1.0, fresnel), 0.0, 1.0));\n}\n";
constexpr float kGlWaves[16][3] = {
    {2, 9, .06f}, {-7, 11, .05f}, {16, 10, .05f}, {-26, 8, .045f}, {13, -36, .04f}, {39, 39, .04f}, {-19, 77, .035f},
    {111, -22, .03f}, {-97, -123, .03f}, {23, 221, .028f}, {187, 131, .025f}, {-251, 97, .022f}, {61, -317, .02f},
    {349, 211, .018f}, {-433, -281, .016f}, {127, 509, .014f}};

enum GlKind { gl_model, gl_marker, gl_ring, gl_trail, gl_plate, gl_baked, gl_water, gl_kinds };
struct GlProgram { GLuint id = 0; GLint mvp = -1, normal = -1, modelview = -1, pull = -1, beacon = -1, halo = -1, water = -1, wave = -1, texture = -1; };
std::array<GlProgram, gl_kinds> glPrograms;
GLuint glMarkerVertices = 0, glMarkerIndices = 0;
std::atomic<uint64_t> glRendered{};

const GlProgram *gl_program(GlKind kind) {
    GlProgram& p = glPrograms[kind];
    if (p.id) return &p;
    static const char *const sources[gl_kinds] = {kGlModelPS, kGlMarkerPS, kGlRingPS, kGlTrailPS, kGlPlatePS, kGlBakedPS, kGlWaterPS};
    std::string error;
    p.id = n64gl::link_program(std::string(kGlPreamble) + kGlMeshVS, std::string(kGlPreamble) + kGlVaryings + sources[kind],
                               {{0, "a_position"}, {1, "a_normal"}, {2, "a_color"}, {3, "a_uv"}}, error);
    if (!p.id) {
        std::fprintf(stderr, "SRW64_NATIVE_MODEL_GL program %d: %s\n", int(kind), error.c_str());
        return nullptr;
    }
    p.mvp = glGetUniformLocation(p.id, "u_mvp"); p.normal = glGetUniformLocation(p.id, "u_normal");
    p.modelview = glGetUniformLocation(p.id, "u_modelview"); p.pull = glGetUniformLocation(p.id, "u_pull");
    p.beacon = glGetUniformLocation(p.id, "u_beacon"); p.halo = glGetUniformLocation(p.id, "u_halo");
    p.water = glGetUniformLocation(p.id, "u_water"); p.wave = glGetUniformLocation(p.id, "u_wave");
    p.texture = glGetUniformLocation(p.id, "u_texture");
    return &p;
}

// Row-vector 4 x 4 products (a then b) and the normal transform: the inverse transpose of
// the model-view's 3 x 3, as RT64's normalView.
void gl_multiply(const float *a, const float *b, float *out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
}
void gl_normal(const float *mv, float *out) {
    const float a = mv[0], b = mv[1], c = mv[2], d = mv[4], e = mv[5], f = mv[6], g = mv[8], h = mv[9], i = mv[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) det = 1;
    // inverse = adjugate / det; its transpose holds the cofactors in place
    const float cof[9] = {A, B, C, -(b * i - c * h), a * i - c * g, -(a * h - b * g), b * f - c * e, -(a * f - c * d), a * e - b * d};
    for (int k = 0; k < 9; ++k) out[k] = cof[k] / det;
}

GLuint gl_buffer(GLenum target, const std::vector<uint8_t>& bytes) {
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(target, id);
    glBufferData(target, GLsizeiptr(bytes.size()), bytes.data(), GL_STATIC_DRAW);
    return id;
}
GLuint gl_picture(const std::vector<uint8_t>& rgba, int width, int height) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// A prepared picture (hd_texture.hpp) with its mips, as it is.
GLuint gl_compressed(const hd_texture::Compressed& c) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    const uint8_t *at = c.data->data();
    uint32_t w = c.width, h = c.height;
    for (uint32_t level = 0; level < c.levels; ++level, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
        const size_t size = hd_texture::level_bytes(c.format, w, h);
        glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), c.format, GLsizei(w), GLsizei(h), 0, GLsizei(size), at);
        at += size;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, c.levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// The program with the draw's transforms; `local` goes before the game's (the marker's bob).
const GlProgram *gl_begin(GlKind kind, const n64gl::Command& command, const float *local = nullptr) {
    const GlProgram *p = gl_program(kind);
    if (!p) return nullptr;
    glUseProgram(p->id);
    float mvp[16], mv[16], normal[9];
    if (local) { gl_multiply(local, command.mvp, mvp); gl_multiply(local, command.modelview, mv); }
    else { std::memcpy(mvp, command.mvp, sizeof mvp); std::memcpy(mv, command.modelview, sizeof mv); }
    gl_normal(mv, normal);
    glUniformMatrix4fv(p->mvp, 1, GL_FALSE, mvp);
    if (p->normal >= 0) glUniformMatrix3fv(p->normal, 1, GL_FALSE, normal);
    if (p->modelview >= 0) glUniformMatrix4fv(p->modelview, 1, GL_FALSE, mv);
    if (p->pull >= 0) glUniform1f(p->pull, 0);
    if (p->texture >= 0) glUniform1i(p->texture, 0);
    return p;
}

// Meshes (models, the marker): the game's depth test and write, opaque unless `blend`.
void gl_mesh_state(const n64gl::Command& command, bool blend, bool write = true) {
    if (command.depth & 1) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); }
    else if (command.depth & 2) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); }
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(write && (command.depth & 2) ? GL_TRUE : GL_FALSE);
    if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
}

// An indexed mesh: position, normal, then an RGBA8 word (stride 28) or a uv and the word (36).
void gl_draw_mesh(GLuint vertices, GLuint indices, size_t stride, size_t index_count) {
    glBindBuffer(GL_ARRAY_BUFFER, vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
    const auto at = [](size_t offset) { return reinterpret_cast<const void *>(offset); };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, GLsizei(stride), at(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, GLsizei(stride), at(12));
    if (stride >= 28) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, GLsizei(stride), at(stride == kTexturedStride ? 32 : 24));
    } else glVertexAttrib4f(2, 1, 1, 1, 1);
    if (stride == kTexturedStride) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, GLsizei(stride), at(24));
    }
    glDrawElements(GL_TRIANGLES, GLsizei(index_count), GL_UNSIGNED_INT, nullptr);
    for (GLuint a = 0; a < 4; ++a) glDisableVertexAttribArray(a);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Vertices built here: position, normal (unused), and uv (the ring's plane, the trail's
// across and colour, the plate's picture).
void gl_draw_strip(const std::vector<float>& position, const std::vector<float>& uv) {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, position.data());
    glVertexAttrib3f(1, 0, 1, 0);
    glVertexAttrib4f(2, 1, 1, 1, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 0, uv.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(position.size() / 3));
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(3);
}

bool gl_model_draw(const n64gl::Command& command) {
    const size_t index = (command.native_id & 0xFFFF) >> 1;
    if (index >= models.size()) return false;
    Model& m = *models[index];
    if (m.shading == Shading::hidden || m.vertices.empty()) { ++m.suppressed; return true; }
    if (!m.glVertices) {
        m.glVertices = gl_buffer(GL_ARRAY_BUFFER, m.vertices);
        m.glIndices = gl_buffer(GL_ELEMENT_ARRAY_BUFFER, m.indices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        if (m.shading == Shading::baked)
            m.glAtlas = m.atlasPrepared.data ? gl_compressed(m.atlasPrepared) : gl_picture(m.atlasPixels, m.atlasWidth, m.atlasHeight);
    }
    const GlKind kind = m.shading == Shading::baked ? gl_baked : m.shading == Shading::water ? gl_water : gl_model;
    const GlProgram *p = gl_begin(kind, command);
    if (!p) return true;
    if (kind == gl_water) {
        static const auto start = std::chrono::steady_clock::now();
        glUniform2f(p->water, std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count(), m.alpha);
        glUniform3fv(p->wave, 16, &kGlWaves[0][0]);
    }
    if (kind == gl_baked) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.glAtlas); }
    gl_mesh_state(command, kind == gl_water && m.alpha < 1);
    gl_draw_mesh(m.glVertices, m.glIndices, m.stride, m.indices.size() / 4);
    ++m.rendered;
    return true;
}

bool gl_plate_draw(const n64gl::Command& command) {
    const size_t index = (command.native_id & 0xFFFF) >> 4;
    if (index >= models.size()) return false;
    Model& m = *models[index];
    const size_t locale = std::min<size_t>((command.native_id >> 1) & 7, kPlateLocales.size() - 1);
    if (m.platePixels[locale].empty()) return true;
    if (!m.glPlates[locale]) m.glPlates[locale] = gl_picture(m.platePixels[locale], m.plateWidth, m.plateHeight);
    const GlProgram *p = gl_begin(gl_plate, command);
    if (!p) return true;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m.glPlates[locale]);
    gl_mesh_state(command, false);
    gl_draw_strip({-100, 0, 0, 100, 0, 0, -100, 30, 0, 100, 30, 0}, {0, 1, 1, 1, 0, 0, 1, 0});
    ++m.plateRendered;
    return true;
}

bool gl_trail_draw(const n64gl::Command& command) {
    const uint32_t serial = (command.native_id & 0xFFFF) >> 1;
    TrailDraw draw;
    {
        std::lock_guard lock(trail.mutex);
        const TrailDraw& slot = trail.ring[serial % kTrailRing];
        if (slot.serial != serial) { ++trail.stale; return true; }
        draw = slot;
    }
    if (draw.points.empty()) return true;
    // HdTrailVS on the CPU: two vertices per step across the direction of travel, the ends
    // carried half a square further; halo 1.45, pulled 2e-4 towards the eye.
    const float halo = 1.45f;
    const size_t n = draw.points.size();
    std::vector<float> position, uv;
    for (size_t i = 0; i < n; ++i) {
        const TrailPoint& here = draw.points[i];
        const TrailPoint& next = draw.points[std::min(i + 1, n - 1)];
        const TrailPoint& prev = draw.points[i > 0 ? i - 1 : 0];
        float dx = next.x - prev.x, dz = next.z - prev.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length > 1e-4f) { dx /= length; dz /= length; } else { dx = 1; dz = 0; }
        for (const float side : {-1.f, 1.f}) {
            float x = here.x - dz * draw.halfWidth * halo * side, z = here.z + dx * draw.halfWidth * halo * side;
            if (i == 0) { x -= dx * draw.halfWidth; z -= dz * draw.halfWidth; }
            if (i == n - 1) { x += dx * draw.halfWidth; z += dz * draw.halfWidth; }
            position.insert(position.end(), {x, here.y, z});
            uv.insert(uv.end(), {side * halo, here.c});
        }
    }
    const GlProgram *p = gl_begin(gl_trail, command);
    if (!p) return true;
    glUniform1f(p->pull, 2e-4f);
    glUniform1f(p->halo, halo);
    gl_mesh_state(command, true, false);
    gl_draw_strip(position, uv);
    ++trail.rendered;
    return true;
}

bool gl_marker_draw(const n64gl::Command& command) {
    if (command.native_id == 5602) {
        const BeaconState beacon = beacon_state();
        const GlProgram *p = gl_begin(gl_ring, command);
        if (!p) return true;
        glUniform4f(p->beacon, beacon.time, beacon.age, beacon.scale, beacon.bob);
        gl_mesh_state(command, true, false);
        gl_draw_strip({-17, 4, -17, 17, 4, -17, -17, 4, 17, 17, 4, 17}, {-17, -17, 17, -17, -17, 17, 17, 17});
        ++ringRendered;
        return true;
    }
    if (!glMarkerVertices) {
        glMarkerVertices = gl_buffer(GL_ARRAY_BUFFER, vertices);
        glMarkerIndices = gl_buffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    const BeaconState beacon = beacon_state();
    const float g = beacon.scale;  // grow in, then float gently above the ring
    const float local[16] = {g, 0, 0, 0, 0, g, 0, 0, 0, 0, g, 0, 0, beacon.bob, 0, 1};
    if (!gl_begin(gl_marker, command, local)) return true;
    gl_mesh_state(command, false);
    gl_draw_mesh(glMarkerVertices, glMarkerIndices, 24, indices.size() / 4);
    ++rendered;
    return true;
}
#endif  // SRW64_GL_ONLY

void load_plate(Model& m, const std::filesystem::path& pack, const json& plate) {
    m.plateCommands = plate.at("triangle_commands").get<std::vector<uint32_t>>();
    if (m.plateCommands.empty()) throw std::runtime_error("Native plate without commands");
    m.plateDraw = m.plateCommands.front();
    std::sort(m.plateCommands.begin(), m.plateCommands.end());
    if (m.plateCommands.back() + 8 > m.referenceBytes) throw std::runtime_error("Invalid native plate command offsets");
    for (size_t l = 0; l < kPlateLocales.size(); ++l) {
        const auto file = pack / plate.at("textures").at(kPlateLocales[l]).get<std::string>();
        int width = 0, height = 0, channels = 0;
        uint8_t *pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) throw std::runtime_error("Cannot read native plate " + file.string());
        m.platePixels[l].assign(pixels, pixels + size_t(width) * height * 4);
        stbi_image_free(pixels);
        if (l && (width != m.plateWidth || height != m.plateHeight)) throw std::runtime_error("Native plate sizes differ");
        m.plateWidth = width; m.plateHeight = height;
    }
}

void load_models(const std::filesystem::path& pack) {
    const json manifest = json::parse(app::read_text(pack / "manifest.json", 16u << 20));
    if (manifest.at("schema") != "srw64.native-models.v2") throw std::runtime_error("Unknown native model pack schema");
    for (const auto& entry : manifest.at("models")) {
        auto m = std::make_unique<Model>();
        m->resource = entry.at("resource_id");
        m->name = entry.at("name");
        m->referenceBytes = entry.at("original_bytes");
        pendingOriginals.push_back({&m->reference, m->resource, m->referenceBytes, entry.at("original_sha256").get<std::string>()});
        m->vertices = read(pack / entry.at("vertices").get<std::string>());
        m->indices = read(pack / entry.at("indices").get<std::string>());
        m->commands = entry.at("triangle_commands").get<std::vector<uint32_t>>();
        const std::string shading = entry.value("shading", std::string("colour"));
        m->shading = shading == "baked" ? Shading::baked : shading == "water" ? Shading::water
                   : shading == "hidden" ? Shading::hidden : Shading::colour;
        if (shading != "colour" && shading != "baked" && shading != "water" && shading != "hidden") throw std::runtime_error("Unknown native model shading " + shading);
        m->stride = m->shading == Shading::colour ? kModelStride : kTexturedStride;
        m->alpha = entry.value("alpha", 1.0f);
        if (!(m->alpha > 0 && m->alpha <= 1)) throw std::runtime_error("Invalid native model alpha");
        if (entry.value("vertex_stride", size_t(kModelStride)) != m->stride) throw std::runtime_error("Native model stride differs from its shading");
        if (m->shading == Shading::baked) {
            const auto file = pack / entry.at("texture").get<std::string>();
#ifdef SRW64_GL_ONLY
            if (hd_texture::load(file, m->atlasPrepared)) {
                m->atlasWidth = int(m->atlasPrepared.width); m->atlasHeight = int(m->atlasPrepared.height);
            } else
#endif
            {
                int width = 0, height = 0, channels = 0;
                uint8_t *pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
                if (!pixels) throw std::runtime_error("Cannot read native model atlas " + file.string());
                m->atlasPixels.assign(pixels, pixels + size_t(width) * height * 4);
                stbi_image_free(pixels);
                m->atlasWidth = width; m->atlasHeight = height;
            }
        }
        if (m->commands.empty() || m->vertices.empty() || m->vertices.size() % m->stride || m->indices.empty() || m->indices.size() % 12)
            throw std::runtime_error("Invalid native model sizes for resource " + std::to_string(m->resource));
        m->draw_command = m->commands.front();
        std::sort(m->commands.begin(), m->commands.end());
        if (entry.contains("plate") && !entry.at("plate").is_null()) load_plate(*m, pack, entry.at("plate"));
        if (m->draw_command != m->commands.front() || m->commands.back() + 8 > m->referenceBytes)
            throw std::runtime_error("Invalid native model command offsets");
        const size_t count = m->vertices.size() / m->stride;
        for (size_t i = 0; i < m->indices.size(); i += 4) {
            uint32_t index; std::memcpy(&index, m->indices.data() + i, 4);
            if (index >= count) throw std::runtime_error("Native model index out of range");
        }
        for (size_t v = 0; v < count; ++v)
            for (size_t k = 0; k < 6; ++k) {
                float value; std::memcpy(&value, m->vertices.data() + v * m->stride + k * 4, 4);
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
        m->referenceBytes = entry.at("original_bytes");
        pendingOriginals.push_back({&m->reference, m->resource, m->referenceBytes, entry.at("original_sha256").get<std::string>()});
        load_plate(*m, pack, entry);
        models.push_back(std::move(m));
    }
    if (manifest.contains("trail")) {
        const auto& t = manifest.at("trail");
        trail.codeAddress = t.at("code_vram");
        trail.vertexBuffer = t.at("vertex_buffer");
        trail.vertexBytes = t.at("vertex_bytes");
        pendingTrail = {t.at("code_rom").get<size_t>(), t.at("code_bytes").get<size_t>(), t.at("code_sha256").get<std::string>()};
        if (!pendingTrail.bytes || pendingTrail.bytes % 4 || trail.vertexBytes < 0x80)
            throw std::runtime_error("Invalid native trail description");
        trail.enabled = true;
    }
}
}

bool replacement_enabled() {
    return markerPack && hd_mode();
}

namespace {
#ifdef SRW64_GL_ONLY
std::atomic<bool> packs_loaded{false};   // the loader thread's packs, for classify_gl
std::thread pack_loader;
#endif
void configure_now(const std::filesystem::path& directory, const std::filesystem::path& marker_pack, const std::filesystem::path& models_pack) {
    output = directory;
    const char *marker_env = std::getenv("SRW64_NATIVE_MARKER");
    std::error_code missing;
    const std::filesystem::path marker = marker_env && *marker_env ? std::filesystem::path(marker_env)
        : std::filesystem::exists(marker_pack / "manifest.json", missing) ? marker_pack : std::filesystem::path();
    if (!marker.empty()) {
        const std::filesystem::path& path = marker;
        const json manifest = json::parse(app::read_text(path / "manifest.json", 16u << 20));
        if (manifest.at("schema") != "srw64.native-marker.v2" || manifest.at("resource_id") != 5600)
            throw std::runtime_error("Unknown native marker pack schema");
        markerPack = true;
        pendingOriginals.push_back({&reference, 5600, 7048, manifest.at("original_resource_sha256").get<std::string>()});
        vertices = read(path / "vertices.bin"); indices = read(path / "indices.bin");
        if (vertices.empty() || vertices.size()%24 || indices.empty() || indices.size()%12)
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
    else if (std::filesystem::exists(models_pack / "manifest.json", missing)) load_models(models_pack);
    if (!markerPack && models.empty() && !trail.enabled) return;
    if (!output.empty()) draws.open(output / "native-model-draws.jsonl");
#ifndef SRW64_GL_ONLY
    RT64::SetNativeMeshHooks(classify, render);
#endif
}
}  // namespace

void configure(const std::filesystem::path& directory, const std::filesystem::path& marker_pack, const std::filesystem::path& models_pack) {
#ifdef SRW64_GL_ONLY
    // Off the start: the packs' 26 MB took the Switch 1.5 s from its SD card. Until they are
    // in, the game's own models draw; the ROM's originals are checked here too, not at the
    // first triangle.
    pack_loader = std::thread([directory, marker_pack, models_pack] {
#ifdef __SWITCH__
        srw64_switch_background_thread();
#endif
        try {
            configure_now(directory, marker_pack, models_pack);
            if (markerPack || !models.empty() || trail.enabled) originals_ready();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64_NATIVE_MODELS_FAILED %s\n", error.what());
            markerPack = false; models.clear(); trail.enabled = false;
        }
        packs_loaded = true;
    });
    pack_loader.detach();
#else
    configure_now(directory, marker_pack, models_pack);
#endif
}

void gpu_init() {
#ifndef SRW64_GL_ONLY
    if (!markerPack && models.empty() && !trail.enabled) return;
    if (markerPack) {
        markerProgram = std::make_unique<gpu::Program>("HdMarker", std::vector<gpu::Slot>{{gpu::Slot::buffer}, {gpu::Slot::buffer}});
        ringProgram = std::make_unique<gpu::Program>("HdRing", std::vector<gpu::Slot>{});
        vertexBuffer = std::make_unique<gpu::Buffer>(vertices);
        indexBuffer = std::make_unique<gpu::Buffer>(indices);
        markerMesh = markerProgram->bind({vertexBuffer.get(), indexBuffer.get()});
    }
    for (auto& m : models) {
        if (m->vertices.empty()) continue;  // plate-only entry
        m->vertexBuffer = std::make_unique<gpu::Buffer>(m->vertices);
        m->indexBuffer = std::make_unique<gpu::Buffer>(m->indices);
        if (m->shading == Shading::baked) {
            if (!bakedProgram)
                bakedProgram = std::make_unique<gpu::Program>("HdBaked", std::vector<gpu::Slot>{{gpu::Slot::buffer}, {gpu::Slot::buffer},
                    {gpu::Slot::texture}, {gpu::Slot::sampler, {.linear = true, .mipmaps = true}}});
            m->atlas = std::make_unique<gpu::Texture>(m->atlasWidth, m->atlasHeight, plume::RenderFormat::R8G8B8A8_UNORM,
                gpu::rgba_mips(std::move(m->atlasPixels), m->atlasWidth, m->atlasHeight));
            continue;   // bound after the atlas uploads (render_model)
        }
        if (m->shading == Shading::hidden) continue;
        if (m->shading == Shading::water) {
            if (!waterProgram)
                waterProgram = std::make_unique<gpu::Program>("HdWater", std::vector<gpu::Slot>{{gpu::Slot::buffer}, {gpu::Slot::buffer}});
            m->mesh = waterProgram->bind({m->vertexBuffer.get(), m->indexBuffer.get()});
            continue;
        }
        if (!modelProgram)
            modelProgram = std::make_unique<gpu::Program>("HdModel", std::vector<gpu::Slot>{{gpu::Slot::buffer}, {gpu::Slot::buffer}});
        m->mesh = modelProgram->bind({m->vertexBuffer.get(), m->indexBuffer.get()});
    }
    if (trail.enabled) trailProgram = std::make_unique<gpu::Program>("HdTrail", std::vector<gpu::Slot>{});
    // Plate textures with mipmaps: the board shrinks to a few hundred pixels on screen.
    // They upload on first draw, on RT64's workload command list.
    for (auto& m : models) {
        if (m->plateCommands.empty()) continue;
        if (!plateProgram)
            plateProgram = std::make_unique<gpu::Program>("HdPlate", std::vector<gpu::Slot>{{gpu::Slot::texture},
                {gpu::Slot::sampler, {.linear = true, .mipmaps = true}}});
        for (size_t l = 0; l < kPlateLocales.size(); ++l)
            m->plateTextures[l] = std::make_unique<gpu::Texture>(m->plateWidth, m->plateHeight, plume::RenderFormat::R8G8B8A8_UNORM,
                gpu::rgba_mips(m->platePixels[l], m->plateWidth, m->plateHeight));
    }
#endif
}

void shutdown() {
    if (!markerPack && models.empty() && !trail.enabled) return;
    if (markerPack)
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
                {"vertices",m->vertices.size()/m->stride},{"triangles",m->indices.size()/12}});
        json summary = {{"schema","srw64.native-models-run.v1"},{"models",list},{"rdram_modified",false}};
        if (trail.enabled)
            summary["trail"] = {{"classified_quads",trail.classified.load()},{"snapshots",trail.snapshots.load()},
                {"snapshot_points",trail.points.load()},{"native_draws",trail.rendered},{"suppressed_quads",trail.suppressed},
                {"stale_draws",trail.stale},{"original_draws",trail.original.load()}};
        std::ofstream(output / "native-models-summary.json") << summary.dump(2) << '\n';
    }
    draws.close();
#ifndef SRW64_GL_ONLY
    for (auto& m : models) {
        for (auto& set : m->plateSets) set.reset();
        for (auto& texture : m->plateTextures) texture.reset();
        m->mesh.reset(); m->atlas.reset(); m->vertexBuffer.reset(); m->indexBuffer.reset();
    }
    markerMesh.reset(); vertexBuffer.reset(); indexBuffer.reset();
    markerProgram.reset(); ringProgram.reset(); modelProgram.reset(); trailProgram.reset(); plateProgram.reset();
    bakedProgram.reset(); waterProgram.reset();
#endif
}

#ifdef SRW64_GL_ONLY
n64gl::MeshTag classify_gl(const uint8_t *rdram, uint32_t address, uint32_t segment4) {
    if (!packs_loaded.load(std::memory_order_acquire) || (!markerPack && models.empty() && !trail.enabled)) return {};
    const uint32_t id = classify_at(rdram, reinterpret_cast<const Words *>(rdram + (address & 0x7FFFFF)), segment4 & 0x7FFFFF);
    // Odd ids are a replaced model's other triangles (5601, 5603, the low bit of the rest).
    return {id, id != 0 && !(id & 1)};
}

bool draw_gl(const n64gl::Command& command, int width, int height) {
    const uint32_t id = command.native_id, base = id & 0xFFFF0000u;
    if (base != kModelIdBase && base != kPlateIdBase && base != kTrailIdBase && (id < 5600 || id > 5603)) return false;
    // The scissor in the target's pixels, as native_gl.cpp sets it.
    const float picture = n64gl::kPictureHeight * float(width) / float(height);
    const int picture_px = int(std::ceil(picture - 1e-3f));
    const int16_t *s = command.state.scissor;
    const int x0 = int(std::clamp<int>(s[0], 0, picture_px) * width / picture);
    const int x1 = int(std::clamp<int>(s[2], 0, picture_px) * width / picture + 0.5f);
    const int y0 = (n64gl::kPictureHeight - std::clamp<int>(s[3], 0, n64gl::kPictureHeight)) * height / n64gl::kPictureHeight;
    const int y1 = (n64gl::kPictureHeight - std::clamp<int>(s[1], 0, n64gl::kPictureHeight)) * height / n64gl::kPictureHeight;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
    glDisable(GL_CULL_FACE);
    if (base == kModelIdBase) gl_model_draw(command);
    else if (base == kPlateIdBase) gl_plate_draw(command);
    else if (base == kTrailIdBase) gl_trail_draw(command);
    else gl_marker_draw(command);
    if (glRendered++ == 0) std::fprintf(stderr, "SRW64_NATIVE_MODEL_GL first draw %08X\n", id);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
    return true;
}
#endif
}
