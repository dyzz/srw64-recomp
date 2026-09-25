#define HLSL_CPU
#include "native_marker.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_workload.h"
#include "rhi/rt64_render_hooks.h"
#include "native_gpu.hpp"
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
#include <tuple>

namespace srw64::marker {
namespace {
using json = nlohmann::json;
std::vector<uint8_t> reference, vertices, indices;
std::filesystem::path output;
// src/host/shaders/Hd{Marker,Ring,Model,Trail,Plate}{VS,PS}.hlsl (NativeMarker.hlsli).
std::unique_ptr<gpu::Program> markerProgram, ringProgram, modelProgram, trailProgram, plateProgram;
std::unique_ptr<gpu::Buffer> vertexBuffer, indexBuffer;
std::unique_ptr<plume::RenderDescriptorSet> markerMesh;
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
struct Model {
    uint32_t resource = 0;
    std::string name;
    std::vector<uint8_t> reference, vertices, indices;  // reference: taken from the ROM, see originals_ready()
    size_t referenceBytes = 0;
    std::vector<uint32_t> commands;  // sorted; the first carries the native draw
    uint32_t draw_command = 0;
    std::unique_ptr<gpu::Buffer> vertexBuffer, indexBuffer;
    std::unique_ptr<plume::RenderDescriptorSet> mesh;
    std::atomic<uint64_t> classified{}, original{};
    uint64_t rendered{}, suppressed{};
    // Name plate (type-5 billboard, 200 x 30 units above the origin), redrawn per locale.
    std::vector<uint32_t> plateCommands;
    uint32_t plateDraw = 0;
    std::array<std::vector<uint8_t>, 3> platePixels;  // RGBA8 per kPlateLocales entry
    int plateWidth = 0, plateHeight = 0;
    std::array<std::unique_ptr<gpu::Texture>, 3> plateTextures;
    std::array<std::unique_ptr<plume::RenderDescriptorSet>, 3> plateSets;
    std::atomic<uint64_t> plateClassified{};
    uint64_t plateRendered{}, plateSuppressed{};
};
std::vector<std::unique_ptr<Model>> models;

// Plates follow the reading language (F7); the locale is fixed per workload when the
// display list is classified, so both render targets of a frame agree.
constexpr uint32_t kPlateIdBase = 0x504C0000;  // 'PL'; bits 4+ model, 1-3 locale, 0 suppressed
constexpr std::array<const char *, 3> kPlateLocales{"ja", "zh-Hans", "en"};

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
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing native marker asset: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
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
    if (!originals_ready()) return 0;
    if (trail.enabled)
        if (const uint32_t id = classify_trail(state, dl)) return id;
    // Segment 4 is established by the game each time it calls a model. Never
    // identify an asset by its incidental heap address or by its color alone.
    const uint32_t base = state->rsp->fromSegmentedMasked(0x04000000);
    const auto pointer = reinterpret_cast<uintptr_t>(dl);
    const auto start = reinterpret_cast<uintptr_t>(state->RDRAM) + base;
    if (pointer < start) return 0;
    const uintptr_t offset = pointer - start;
    if (markerPack)
        if (const uint32_t id = classify_marker(state, base, offset)) return id;
    return models.empty() ? 0 : classify_models(state, base, offset);
}

// All float4 fields keep the CPU/Metal uniform layout explicit. hlsl++ stores
// rows; Metal interprets these bytes as columns, matching RT64's row-vector math.
struct Uniforms {
    float mvp[16], normalView[16];
    float viewportScale[4], viewportTranslate[4], resolution[4], screen[4];
};

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
    if (!modelProgram || !m.mesh || !call.workload) throw std::runtime_error("Native model missing immutable draw context");
    hlslpp::float4x4 mvp; uint32_t worldIndex{};
    const Uniforms u = uniforms(call, &mvp, &worldIndex);
    if (!draw(list, framebuffer, call, *modelProgram, mesh_state(call), m.mesh.get(), u, uint32_t(m.indices.size() / 4))) return false;
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

void load_plate(Model& m, const std::filesystem::path& pack, const json& plate) {
    m.plateCommands = plate.at("triangle_commands").get<std::vector<uint32_t>>();
    if (m.plateCommands.empty()) throw std::runtime_error("Native plate without commands");
    m.plateDraw = m.plateCommands.front();
    std::sort(m.plateCommands.begin(), m.plateCommands.end());
    if (m.plateCommands.back() + 8 > m.referenceBytes) throw std::runtime_error("Invalid native plate command offsets");
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
        if (m->commands.empty() || m->vertices.empty() || m->vertices.size() % kModelStride || m->indices.empty() || m->indices.size() % 12)
            throw std::runtime_error("Invalid native model sizes for resource " + std::to_string(m->resource));
        m->draw_command = m->commands.front();
        std::sort(m->commands.begin(), m->commands.end());
        if (entry.contains("plate") && !entry.at("plate").is_null()) load_plate(*m, pack, entry.at("plate"));
        if (m->draw_command != m->commands.front() || m->commands.back() + 8 > m->referenceBytes)
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

void configure(const std::filesystem::path& directory) {
    output = directory;
    if (const char *pack = std::getenv("SRW64_NATIVE_MARKER"); pack && *pack) {
        const std::filesystem::path path(pack);
        std::ifstream stream(path / "manifest.json");
        if (!stream) throw std::runtime_error("Native marker pack without manifest.json: " + path.string());
        const json manifest = json::parse(stream);
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
    if (!markerPack && models.empty() && !trail.enabled) return;
    draws.open(output / "native-model-draws.jsonl");
    RT64::SetNativeMeshHooks(classify, render);
}

void gpu_init() {
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
        if (!modelProgram)
            modelProgram = std::make_unique<gpu::Program>("HdModel", std::vector<gpu::Slot>{{gpu::Slot::buffer}, {gpu::Slot::buffer}});
        m->vertexBuffer = std::make_unique<gpu::Buffer>(m->vertices);
        m->indexBuffer = std::make_unique<gpu::Buffer>(m->indices);
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
                {"vertices",m->vertices.size()/kModelStride},{"triangles",m->indices.size()/12}});
        json summary = {{"schema","srw64.native-models-run.v1"},{"models",list},{"rdram_modified",false}};
        if (trail.enabled)
            summary["trail"] = {{"classified_quads",trail.classified.load()},{"snapshots",trail.snapshots.load()},
                {"snapshot_points",trail.points.load()},{"native_draws",trail.rendered},{"suppressed_quads",trail.suppressed},
                {"stale_draws",trail.stale},{"original_draws",trail.original.load()}};
        std::ofstream(output / "native-models-summary.json") << summary.dump(2) << '\n';
    }
    draws.close();
    for (auto& m : models) {
        for (auto& set : m->plateSets) set.reset();
        for (auto& texture : m->plateTextures) texture.reset();
        m->mesh.reset(); m->vertexBuffer.reset(); m->indexBuffer.reset();
    }
    markerMesh.reset(); vertexBuffer.reset(); indexBuffer.reset();
    markerProgram.reset(); ringProgram.reset(); modelProgram.reset(); trailProgram.reset(); plateProgram.reset();
}
}
