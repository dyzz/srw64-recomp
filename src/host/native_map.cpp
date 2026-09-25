#define HLSL_CPU
#include "native_map.hpp"
#include "native_marker.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#include "native_gpu.hpp"
#include "json/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace srw64::hdmap {
namespace {
using json = nlohmann::json;
using Palette = std::array<uint32_t, 256>;   // RGBA8, R in the low byte

// A G_NOOP carrying this tag immediately precedes the marker rectangle.
constexpr uint32_t kTagW0 = 0x00534457;      // G_NOOP, "SDW"
constexpr uint32_t kTagW1 = 0x48440000;      // "HD" + 16-bit draw id
constexpr uint32_t kIdBase = 0x48440000;     // native draw ids handed to RT64
constexpr size_t kRing = 1024;

struct Asset {
    uint16_t layout = 0;
    int width = 0, height = 0, scale = 0;
    Palette reference{};
    bool alpha = false;                      // meta "alpha": premultiplied base, drawn translucent
    std::vector<uint8_t> base, index;        // RGBA8 and one index per pixel, at width*scale
    std::unique_ptr<gpu::Texture> base_texture, index_texture;
    std::unique_ptr<plume::RenderDescriptorSet> textures;
};

struct Draw {
    uint32_t id = 0;
    int asset = -1;
    float rect[4]{}, uv[4]{};                // N64 screen pixels; normalized map coordinates
    Palette live{};
};

std::vector<Asset> assets;
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
RT64::NativeMeshClassify *previous_classify{};
RT64::NativeMeshRender *previous_render{};
std::unique_ptr<gpu::Program> program;      // src/host/shaders/HdMap{VS,PS}.hlsl
std::atomic<uint64_t> rewritten{}, rendered{}, skipped{}, no_marker{};
std::filesystem::path output;

uint32_t word(const uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address & 0x1FFFFFFF), 4);
    return value;
}

void put(uint8_t* rdram, uint32_t address, uint32_t value) {
    std::memcpy(rdram + (address & 0x1FFFFFFF), &value, 4);
}

uint16_t half(const uint8_t* rdram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, rdram + ((address & 0x1FFFFFFF) ^ 2), 2);
    return value;
}

uint32_t rgba5551(uint16_t v) {
    auto c = [](uint32_t x) { return (x * 255 + 15) / 31; };
    return c((v >> 11) & 31) | c((v >> 6) & 31) << 8 | c((v >> 1) & 31) << 16 | (v & 1 ? 0xFFu : 0u) << 24;
}

Asset load(const std::filesystem::path& folder) {
    std::ifstream stream(folder / "meta.json");
    if (!stream) throw std::runtime_error("HD map without meta.json: " + folder.string());
    const json meta = json::parse(stream);
    if (meta.at("schema") != "srw64.hd-map-runtime.v0") throw std::runtime_error("Unknown HD map schema in " + folder.string());
    Asset asset;
    asset.layout = meta.at("layout").get<uint16_t>();
    asset.width = meta.at("width"); asset.height = meta.at("height"); asset.scale = meta.at("scale");
    const auto& palette = meta.at("reference_palette");
    if (palette.size() != 256) throw std::runtime_error("HD map reference palette must have 256 entries");
    for (size_t i = 0; i < 256; ++i) {
        const auto& c = palette[i];
        asset.reference[i] = c[0].get<uint32_t>() | c[1].get<uint32_t>() << 8 | c[2].get<uint32_t>() << 16 | c[3].get<uint32_t>() << 24;
    }
    const int w = asset.width * asset.scale, h = asset.height * asset.scale;
    auto read = [&](const char* name, int channels, std::vector<uint8_t>& out) {
        int x = 0, y = 0, n = 0;
        uint8_t* pixels = stbi_load((folder / name).c_str(), &x, &y, &n, channels);
        if (!pixels) throw std::runtime_error("Cannot read HD map image " + (folder / name).string());
        if (x != w || y != h) { stbi_image_free(pixels); throw std::runtime_error("HD map image size differs from meta.json"); }
        out.assign(pixels, pixels + size_t(x) * y * channels);
        stbi_image_free(pixels);
    };
    read("base.png", 4, asset.base);
    read("index.png", 1, asset.index);
    // Translucent assets (window frames) filter premultiplied: straight alpha would pull
    // the colour of transparent texels into the edges.
    asset.alpha = meta.value("alpha", false);
    if (asset.alpha)
        for (size_t i = 0; i < asset.base.size(); i += 4)
            for (int c = 0; c < 3; ++c) asset.base[i + c] = uint8_t((asset.base[i + c] * asset.base[i + 3] + 127) / 255);
    return asset;
}

int find_asset(uint16_t layout) {
    for (size_t i = 0; i < assets.size(); ++i) if (assets[i].layout == layout) return int(i);
    return -1;
}

bool hd_enabled() {
    return !presentation::image_mode.enabled() || presentation::image_mode.current() == 1;
}

uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

bool render(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) != kIdBase) return previous_render ? previous_render(list, framebuffer, call) : false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[call.id % kRing];
    }
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !program) { ++skipped; return true; }
    Asset& asset = assets[size_t(draw.asset)];
    if (!asset.textures) {
        if (!asset.base_texture->upload(list) || !asset.index_texture->upload(list)) { ++skipped; return true; }
        gpu::Texture* const textures[] = {asset.base_texture.get(), asset.index_texture.get()};
        asset.textures = program->bind_textures(textures);
    }
    // HdMapVS/PS data: rect, uv, resolution, screen, then the reference and live palettes.
    struct {
        float rect[4], uv[4], resolution[4], screen[4];
        uint32_t reference[256], live[256];
    } data{};
    std::memcpy(data.rect, draw.rect, sizeof(data.rect)); std::memcpy(data.uv, draw.uv, sizeof(data.uv));
    data.resolution[0] = float(call.fbWidth); data.resolution[1] = float(call.fbHeight);
    // RT64's screenScale/screenOffset for a rectangle describe that rectangle's own
    // viewport (width / frame width, centre offset). The quad is already in full-frame
    // N64 coordinates, so applying them stretched the map by the marker's overhang and
    // made it breathe as the culled bottom edge moved while scrolling.
    data.screen[0] = 1; data.screen[1] = 1; data.screen[2] = 0; data.screen[3] = 0;
    std::memcpy(data.reference, asset.reference.data(), sizeof(data.reference));
    std::memcpy(data.live, draw.live.data(), sizeof(data.live));
    gpu::State state;
    if (asset.alpha) state.blend = gpu::Blend::premultiplied;
    if (!program->begin(list, framebuffer, call, state, asset.textures.get(), gpu::push_data(&data, sizeof(data)), asset.alpha ? 1 : 0)) {
        ++skipped;
        return true;
    }
    list->drawInstanced(4, 1, 0, 0);
    ++rendered;
    return true;
}
}

void configure(const std::filesystem::path& directory) {
    const char* root = std::getenv("SRW64_HD_MAPS");
    if (!root || !*root) return;
    output = directory;
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "meta.json")) assets.push_back(load(entry.path()));
    if (assets.empty()) return;
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
    fprintf(stderr, "SRW64_HD_MAPS loaded %zu map(s)\n", assets.size());
}

void gpu_init() {
    if (assets.empty()) return;
    // The textures upload on first draw, on RT64's workload command list.
    const auto level = [](std::vector<uint8_t>& pixels) {
        std::vector<std::vector<uint8_t>> levels;
        levels.push_back(std::move(pixels));
        return levels;
    };
    for (auto& asset : assets) {
        const uint32_t w = asset.width * asset.scale, h = asset.height * asset.scale;
        asset.base_texture = std::make_unique<gpu::Texture>(w, h, plume::RenderFormat::R8G8B8A8_UNORM, level(asset.base));
        asset.index_texture = std::make_unique<gpu::Texture>(w, h, plume::RenderFormat::R8_UINT, level(asset.index));
    }
    // One linear clamp sampler after the base and index textures.
    program = std::make_unique<gpu::Program>("HdMap", 2, std::vector<gpu::Sampler>{{}});
}

void rewrite(uint8_t* rdram, const MapDraw& draw) {
    if (assets.empty()) return;
    const int asset_index = find_asset(draw.layout);
    if (asset_index < 0 || !hd_enabled()) return;
    const Asset& asset = assets[size_t(asset_index)];
    // Walk the commands the drawer wrote: remember its TLUT source and every
    // 24-byte texture rectangle (E4 + E1 + F1).
    std::vector<uint32_t> rects;
    uint32_t image = 0, tlut = 0;
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if (op == 0xFD) image = word(rdram, p + 4);
        else if (op == 0xF0) tlut = image;
        else if ((op == 0xE4 || op == 0xE5) && p + 24 <= draw.dl_end) {
            const uint32_t w1 = word(rdram, p + 4);
            x0 = std::min<int32_t>(x0, (w1 >> 12) & 0xFFF); y0 = std::min<int32_t>(y0, w1 & 0xFFF);
            x1 = std::max<int32_t>(x1, (w0 >> 12) & 0xFFF); y1 = std::max<int32_t>(y1, w0 & 0xFFF);
            rects.push_back(p);
            p += 16;
        }
    }
    // The marker needs a rectangle whose previous 8 bytes belong to another rectangle,
    // so the tag never overwrites a tile load or state command.
    size_t marker = 0;
    for (size_t r = 1; r < rects.size() && !marker; ++r) if (rects[r - 1] + 24 == rects[r]) marker = r;
    if (!marker || !tlut || x1 <= x0 || y1 <= y0) { ++no_marker; return; }
    Draw record;
    record.asset = asset_index;
    for (int i = 0; i < 256; ++i) record.live[i] = rgba5551(half(rdram, tlut + i * 2));
    // Texture rectangle corners are 10.2 fixed point; the lower-right is exclusive.
    record.rect[0] = x0 / 4.0f; record.rect[1] = y0 / 4.0f;
    record.rect[2] = x1 / 4.0f; record.rect[3] = y1 / 4.0f;
    record.uv[0] = (record.rect[0] + draw.camera_x) / asset.width;
    record.uv[1] = (record.rect[1] + draw.camera_y) / asset.height;
    record.uv[2] = (record.rect[2] + draw.camera_x) / asset.width;
    record.uv[3] = (record.rect[3] + draw.camera_y) / asset.height;
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    // The marker rectangle covers the whole drawn area; the 8 bytes before it (the tail of
    // the previous rectangle) carry the tag. Every other rectangle becomes no-ops; tile
    // loads and state commands stay untouched.
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
    if (assets.empty()) return;
    std::ofstream(output / "hd-map-summary.json") << json({{"schema", "srw64.hd-map-run.v0"},
        {"maps", assets.size()}, {"rewritten_draws", rewritten.load()}, {"native_draws", rendered.load()},
        {"skipped", skipped.load()}, {"unmarked_draws", no_marker.load()}}).dump(2) << '\n';
    for (auto& asset : assets) {
        asset.textures.reset();
        asset.base_texture.reset();
        asset.index_texture.reset();
    }
    program.reset();
}
}
