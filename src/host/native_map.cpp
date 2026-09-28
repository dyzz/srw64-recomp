#define HLSL_CPU
#include "native_map.hpp"
#include "native_map_geometry.hpp"
#include "native_marker.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#include "native_gpu.hpp"
#include "rom_art.hpp"
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
#include <tuple>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

uint64_t srw64_current_vi();

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
    uint16_t layout = 0;                     // map layout, or a window frame's scene (mode 9)
    int width = 0, height = 0, scale = 0;
    int origin[2]{};                         // meta "origin": the crop's corner in scene pixels
    Palette reference{};
    bool alpha = false;                      // meta "alpha": premultiplied base, drawn translucent
    std::filesystem::path folder;
    std::string base_file = "base.png";      // meta "base": the release pack stores map bases as JPEG
    // Maps decode on the decoder thread the first time their layout is drawn (the game
    // waits briefly for it) and give their GPU textures back once they have not been
    // drawn for a while (lazy). Window frames (docs/native/native-ui-text.md §4) are made
    // from the ROM (rom_art.cpp) the same way and draw from the next frame on.
    bool from_rom = false, lazy = false, waited = false;
    enum class State { waiting, queued, decoded, ready } state = State::waiting;
    std::vector<uint8_t> base, index;        // RGBA8 and one index per pixel, at width*scale
    std::vector<std::vector<uint8_t>> base_levels;   // base with its mip chain, from the decoder thread
    uint64_t last_draw = 0;                  // draw_clock when last used (std::atomic_ref)
    std::unique_ptr<gpu::Texture> base_texture, index_texture;
    std::unique_ptr<plume::RenderDescriptorSet> textures;
    std::vector<std::array<int, 2>> colonies;
};

struct Draw {
    uint32_t id = 0;
    int asset = -1;
    float rect[4]{}, uv[4]{};                // N64 screen pixels; normalized map coordinates
    Palette live{};
    uint8_t colony_frame = 0;
};

std::vector<Asset> assets;
std::array<Asset, 8> colony_frames;
bool colonies_loaded = false;
std::atomic<uint64_t> colony_draws{};
std::atomic<uint32_t> colony_frames_seen{};
std::ofstream colony_events;
int last_colony_frame = -1, last_colony_layout = -1, last_colony_mode = -1;
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
RT64::NativeMeshClassify *previous_classify{};
RT64::NativeMeshRender *previous_render{};
std::unique_ptr<gpu::Program> program;      // src/host/shaders/HdMap{VS,PS}.hlsl
std::mutex asset_mutex;                      // Asset::state and the decode queue
std::condition_variable decode_wake, decoded_wake;
std::atomic<uint64_t> draw_clock{};          // counts HD map draws, for evicting maps not drawn lately
uint64_t evict_after = 900;                  // draws (about half a minute) without a map before its textures go
constexpr auto kFirstDecodeWait = std::chrono::milliseconds(400);
std::atomic<uint64_t> evicted{}, decode_failures{};
std::deque<size_t> decode_queue;
std::thread decoder;
bool stopping = false;
size_t frame_assets = 0;
bool rom_frames = false;                     // an HD art pack is present: frames come from the ROM
std::atomic<uint64_t> rewritten{}, rendered{}, skipped{}, no_marker{}, panel_rewritten{}, panel_unmarked{}, overview_rewritten{};
int current_map = -1;                    // asset of the map drawn last; the terrain panel shows its cells
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

Asset load_meta(const std::filesystem::path& folder) {
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
    asset.alpha = meta.value("alpha", false);
    asset.base_file = meta.value("base", std::string("base.png"));
    if (meta.contains("origin")) { asset.origin[0] = meta["origin"][0]; asset.origin[1] = meta["origin"][1]; }
    asset.folder = folder;
    if (meta.contains("colony_instances")) {
        if (meta.value("atlas", 0) != 6229 || meta.value("palette", 0) != 6258 ||
            meta.value("mode", 0) != 1 || asset.alpha)
            throw std::runtime_error("Colony overlay requires the mode-1 colony atlas and palette");
        for (const auto& xy : meta.at("colony_instances")) {
            const int x = xy.at(0), y = xy.at(1);
            if (xy.size() != 2 || x < 0 || y < 0 || x + 64 > asset.width || y + 48 > asset.height)
                throw std::runtime_error("Colony instance is outside the map");
            asset.colonies.push_back({x, y});
        }
    }
    return asset;
}

// A window frame drawn from the ROM's line layout (rom_art.cpp): indices, coverage, the
// reference palette and the painted base.
void generate(Asset& asset) {
    const auto spec = rom_art::frame(asset.layout);
    if (!spec) return;
    const auto image = rom_art::frame_image(*spec);
    if (!image.width) return;
    asset.width = image.width; asset.height = image.height; asset.scale = image.scale;
    asset.origin[0] = image.origin[0]; asset.origin[1] = image.origin[1];
    std::copy(image.palette.begin(), image.palette.end(), asset.reference.begin());
    asset.index = image.index;
    asset.base.resize(asset.index.size() * 4);
    for (size_t i = 0; i < asset.index.size(); ++i) {
        // The strokes' antialiased coverage on top of the palette entry's own alpha.
        const uint32_t c = image.palette[asset.index[i]], a = (c >> 24) * image.coverage[i] / 255;
        for (int k = 0; k < 3; ++k) asset.base[i * 4 + k] = uint8_t((((c >> (8 * k)) & 0xFF) * a + 127) / 255);
        asset.base[i * 4 + 3] = uint8_t(a);
    }
    asset.alpha = true;
}

// The images, off the game thread for window frames.
void decode(Asset& asset) {
    if (asset.from_rom) { generate(asset); return; }
    const auto& folder = asset.folder;
    const int w = asset.width * asset.scale, h = asset.height * asset.scale;
    auto read = [&](const char* name, int channels, std::vector<uint8_t>& out) {
        int x = 0, y = 0, n = 0;
        uint8_t* pixels = stbi_load((folder / name).c_str(), &x, &y, &n, channels);
        if (!pixels) throw std::runtime_error("Cannot read HD map image " + (folder / name).string());
        if (x != w || y != h) { stbi_image_free(pixels); throw std::runtime_error("HD map image size differs from meta.json"); }
        out.assign(pixels, pixels + size_t(x) * y * channels);
        stbi_image_free(pixels);
    };
    read(asset.base_file.c_str(), 4, asset.base);
    read("index.png", 1, asset.index);
    // Translucent assets (window frames) filter premultiplied: straight alpha would pull
    // the colour of transparent texels into the edges.
    if (asset.alpha)
        for (size_t i = 0; i < asset.base.size(); i += 4)
            for (int c = 0; c < 3; ++c) asset.base[i + c] = uint8_t((asset.base[i + c] * asset.base[i + 3] + 127) / 255);
    // Opaque maps get a mip chain for the overview (the map shrinks about 13x there); the
    // index map stays one level, its palette shift is looked up per pixel.
    asset.base_levels.clear();
    if (asset.alpha) asset.base_levels.push_back(std::move(asset.base));
    else asset.base_levels = gpu::rgba_mips(std::move(asset.base), uint32_t(w), uint32_t(h));
    asset.base.clear();
}

void make_textures(Asset& asset) {
    const auto level = [](std::vector<uint8_t>& pixels) {
        std::vector<std::vector<uint8_t>> levels;
        levels.push_back(std::move(pixels));
        return levels;
    };
    const uint32_t w = asset.width * asset.scale, h = asset.height * asset.scale;
    if (asset.base_levels.empty()) asset.base_levels.push_back(std::move(asset.base));   // ROM frames
    asset.base_texture = std::make_unique<gpu::Texture>(w, h, plume::RenderFormat::R8G8B8A8_UNORM, std::move(asset.base_levels));
    asset.base_levels.clear();
    asset.index_texture = std::make_unique<gpu::Texture>(w, h, plume::RenderFormat::R8_UINT, level(asset.index));
    asset.state = Asset::State::ready;
}

void decode_loop() {
    for (;;) {
        size_t index;
        {
            std::unique_lock lock(asset_mutex);
            decode_wake.wait(lock, [] { return stopping || !decode_queue.empty(); });
            if (stopping) return;
            index = decode_queue.front(); decode_queue.pop_front();
        }
        // Only this thread touches a queued asset's pixels.
        try {
            decode(assets[index]);
        } catch (const std::exception& error) {
            assets[index].index.clear();
            ++decode_failures;
            fprintf(stderr, "SRW64_HD_MAPS decode failed: %s\n", error.what());
        }
        std::lock_guard lock(asset_mutex);
        // An asset that does not decode stays original: waiting again would retry forever.
        assets[index].state = assets[index].index.empty() ? Asset::State::queued : Asset::State::decoded;
        decoded_wake.notify_all();
    }
}

int find_asset(uint16_t layout) {
    for (size_t i = 0; i < assets.size(); ++i) if (assets[i].layout == layout) return int(i);
    return -1;
}

bool hd_enabled() {
    return !presentation::image_mode.enabled() || presentation::image_mode.current() == 1;
}

std::atomic<bool (*)(uint16_t)> original_frames{};

uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

bool render_quad(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer,
                 const RT64::NativeMeshDraw& call, Asset& asset, const Draw& draw) {
    if (!asset.base_texture || !asset.index_texture) { ++skipped; return false; }   // evicted meanwhile
    if (!asset.textures) {
        if (!asset.base_texture->upload(list) || !asset.index_texture->upload(list)) { ++skipped; return false; }
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
        return false;
    }
    list->drawInstanced(4, 1, 0, 0);
    return true;
}

// Render thread: maps not drawn for evict_after draws give their textures back (kept alive
// until the GPU is done with them) and decode again when they return.
void evict_stale(size_t keep) {
    const uint64_t now = draw_clock.load();
    std::lock_guard lock(asset_mutex);
    for (size_t i = 0; i < assets.size(); ++i) {
        Asset& old = assets[i];
        if (i == keep || !old.lazy || old.state != Asset::State::ready) continue;
        if (now - std::atomic_ref<uint64_t>(old.last_draw).load() < evict_after) continue;
        struct Held { std::unique_ptr<gpu::Texture> base, index; std::unique_ptr<plume::RenderDescriptorSet> set; };
        gpu::retire(std::make_shared<Held>(Held{std::move(old.base_texture), std::move(old.index_texture), std::move(old.textures)}));
        old.state = Asset::State::waiting;
        old.waited = false;
        ++evicted;
    }
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
    if (!render_quad(list, framebuffer, call, asset, draw)) return true;
    ++rendered;
    if (asset.lazy && rendered % 64 == 0) evict_stale(size_t(draw.asset));
    for (const auto& xy : asset.colonies) {
        Draw overlay = draw;
        if (!project_overlay(draw.rect, draw.uv, asset.width, asset.height,
                             xy[0], xy[1], 64, 48, overlay.rect, overlay.uv)) continue;
        if (render_quad(list, framebuffer, call, colony_frames[draw.colony_frame], overlay)) {
            ++colony_draws;
            colony_frames_seen.fetch_or(1u << draw.colony_frame);
        }
    }
    return true;
}
}

// Where the maps are: SRW64_HD_MAPS (development), else the HD art pack's tactical map
// index (compile_art writes srw64-tactical-maps.json; its root is relative to the pack).
std::filesystem::path maps_root() {
    if (const char* root = std::getenv("SRW64_HD_MAPS"); root && *root) return root;
    const char* art = std::getenv("SRW64_ART_PACK");
    if (!art || !*art) return {};
    const auto index = std::filesystem::path(art) / "srw64-tactical-maps.json";
    std::ifstream stream(index);
    if (!stream) return {};
    const json runtime = json::parse(stream);
    if (runtime.value("schema", "") != "srw64.tactical-maps-runtime.v1") throw std::runtime_error("Unknown tactical map index in " + index.string());
    std::filesystem::path root = runtime.at("root").get<std::string>();
    if (root.is_relative()) root = std::filesystem::path(art) / root;
    // A public pack may leave the maps out (tools/release/build_release.py): original maps then.
    return std::filesystem::is_directory(root) ? root : std::filesystem::path{};
}

void configure(const std::filesystem::path& directory) {
    output = directory;
    // Checks shorten the eviction delay to see a map leave and come back.
    if (const char* after = std::getenv("SRW64_HD_MAPS_EVICT_AFTER"); after && *after) evict_after = std::max(1, std::atoi(after));
    if (const auto found = maps_root(); !found.empty()) {
        const std::string root_string = found.string();
        const char* root = root_string.c_str();
        for (const auto& entry : std::filesystem::directory_iterator(root))
            if (entry.is_directory() && std::filesystem::exists(entry.path() / "meta.json")) {
                assets.push_back(load_meta(entry.path()));
                assets.back().lazy = true;
            }
        colonies_loaded = std::any_of(assets.begin(), assets.end(), [](const Asset& a) { return !a.colonies.empty(); });
        if (colonies_loaded) {
            for (size_t i = 0; i < colony_frames.size(); ++i) {
                colony_frames[i] = load_meta(std::filesystem::path(root) / "colony" / ("frame-0" + std::to_string(i)));
                auto& frame = colony_frames[i];
                if (frame.width != 64 || frame.height != 48 || !frame.alpha || !frame.colonies.empty())
                    throw std::runtime_error("Invalid colony frame metadata");
                for (const auto& map : assets)
                    if (!map.colonies.empty() && frame.reference != map.reference)
                        throw std::runtime_error("Colony and map reference palettes differ");
                decode(frame);
                frame.state = Asset::State::decoded;
            }
            colony_events.open(output / "hd-colony-events.jsonl");
        }
    }
    // Window frames come from the player's ROM (rom_art.cpp) when an HD art pack is present,
    // as assets added the first time each frame scene is drawn.
    if (const char* art = std::getenv("SRW64_ART_PACK"); art && *art) rom_frames = true;
    assets.reserve(assets.size() + 512);     // frames join later: no reallocation under the renderer
    if (assets.empty() && !rom_frames) return;
    decoder = std::thread(decode_loop);
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
    fprintf(stderr, "SRW64_HD_MAPS loaded %zu map(s)%s\n", assets.size(), rom_frames ? ", window frames from the ROM" : "");
}

void gpu_init() {
    if (assets.empty() && !rom_frames) return;
    // The textures upload on first draw, on RT64's workload command list.
    {
        std::lock_guard lock(asset_mutex);
        for (auto& asset : assets)
            if (asset.state == Asset::State::decoded) make_textures(asset);
        if (colonies_loaded) for (auto& frame : colony_frames) make_textures(frame);
    }
    // One linear clamp sampler after the base and index textures.
    program = std::make_unique<gpu::Program>("HdMap", 2, std::vector<gpu::Sampler>{{true, true}});
}

void set_original_frames(bool (*keep)(uint16_t)) { original_frames = keep; }

void rewrite(uint8_t* rdram, const MapDraw& draw) {
    if (assets.empty() && !rom_frames) return;
    int asset_index = find_asset(draw.layout);
    if (asset_index < 0 && rom_frames && rom_art::ready() && rom_art::frame(draw.layout) && assets.size() < assets.capacity()) {
        // A window frame drawn for the first time: its asset joins, made on the decoder thread.
        std::lock_guard lock(asset_mutex);
        Asset frame;
        frame.layout = draw.layout;
        frame.from_rom = true;
        assets.push_back(std::move(frame));
        ++frame_assets;
        asset_index = int(assets.size() - 1);
    }
    if (asset_index < 0) return;
    if (!assets[size_t(asset_index)].from_rom) current_map = asset_index;
    const uint8_t colony_frame = rdram[(0x80178C6D & 0x1FFFFFFF) ^ 3] & 7;
    const auto keep = original_frames.load();
    const bool hd = hd_enabled() && !(assets[size_t(asset_index)].from_rom && keep && keep(draw.layout));
    if (!assets[size_t(asset_index)].colonies.empty() &&
        (last_colony_layout != draw.layout || last_colony_frame != colony_frame || last_colony_mode != int(hd))) {
        colony_events << json({{"layout", draw.layout}, {"frame", colony_frame},
            {"vi", srw64_current_vi()}, {"hd", hd},
            {"copy_state", rdram[(0x80178C6C & 0x1FFFFFFF) ^ 3]},
            {"countdown", rdram[(0x80178C6E & 0x1FFFFFFF) ^ 3]},
            {"map_draw", rewritten.load()}, {"camera", {draw.camera_x, draw.camera_y}}}).dump() << '\n';
        colony_events.flush();
        last_colony_layout = draw.layout; last_colony_frame = colony_frame; last_colony_mode = int(hd);
    }
    if (!hd) return;
    {
        // An asset not decoded yet: queue it and leave the original this time.
        std::unique_lock lock(asset_mutex);
        Asset& lazy = assets[size_t(asset_index)];
        if (lazy.state == Asset::State::waiting) {
            lazy.state = Asset::State::queued;
            decode_queue.push_back(size_t(asset_index));
            decode_wake.notify_one();
        }
        // A map seen for the first time: its decode takes a moment (while the stage fades
        // in); wait for it once so the original cells do not flash first.
        if (lazy.lazy && lazy.state == Asset::State::queued && !lazy.waited) {
            lazy.waited = true;
            decoded_wake.wait_for(lock, kFirstDecodeWait, [&] { return lazy.state != Asset::State::queued; });
        }
        if (lazy.state == Asset::State::decoded) make_textures(lazy);
        if (lazy.state != Asset::State::ready) return;
        std::atomic_ref<uint64_t>(lazy.last_draw).store(++draw_clock);
    }
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
    // Small window frames load every cell's tile anew, so no two rectangles touch; the
    // SETTILESIZE just before one is also safe, as every rectangle of this draw goes.
    bool after_size = false;
    if (!marker)
        for (size_t r = 0; r < rects.size() && !marker && !after_size; ++r)
            if ((word(rdram, rects[r] - 8) >> 24) == 0xF2 && rects[r] - 8 >= draw.dl_begin) { marker = r; after_size = true; }
    if ((!marker && !after_size) || !tlut || x1 <= x0 || y1 <= y0) { ++no_marker; return; }
    Draw record;
    record.asset = asset_index;
    if (!asset.colonies.empty()) {
        // Snapshot the original game's current frame with this display list. The
        // render thread must not read live RDRAM or advance an independent timer.
        record.colony_frame = colony_frame;
    }
    for (int i = 0; i < 256; ++i) record.live[i] = rgba5551(half(rdram, tlut + i * 2));
    // Texture rectangle corners are 10.2 fixed point; the lower-right is exclusive.
    record.rect[0] = x0 / 4.0f; record.rect[1] = y0 / 4.0f;
    record.rect[2] = x1 / 4.0f; record.rect[3] = y1 / 4.0f;
    if (draw.overview) {
        // The overview draws every cell shrunk, skipping the border ring: the cells it drew
        // form a grid whose column and row counts tell how many border cells it left out.
        std::set<uint32_t> columns, rows;
        for (const uint32_t p : rects) { columns.insert((word(rdram, p + 4) >> 12) & 0xFFF); rows.insert(word(rdram, p + 4) & 0xFFF); }
        const int inset_x = std::max(0, (asset.width / 16 - int(columns.size())) / 2) * 16;
        const int inset_y = std::max(0, (asset.height / 16 - int(rows.size())) / 2) * 16;
        record.uv[0] = float(inset_x) / asset.width; record.uv[1] = float(inset_y) / asset.height;
        record.uv[2] = float(asset.width - inset_x) / asset.width; record.uv[3] = float(asset.height - inset_y) / asset.height;
    } else {
        // Cropped assets (window frames) start at their origin in scene pixels.
        record.uv[0] = (record.rect[0] + draw.camera_x - asset.origin[0]) / asset.width;
        record.uv[1] = (record.rect[1] + draw.camera_y - asset.origin[1]) / asset.height;
        record.uv[2] = (record.rect[2] + draw.camera_x - asset.origin[0]) / asset.width;
        record.uv[3] = (record.rect[3] + draw.camera_y - asset.origin[1]) / asset.height;
    }
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
    if (draw.overview) ++overview_rewritten;
}

void rewrite_panel(uint8_t* rdram, const PanelDraw& draw) {
    if (current_map < 0 || !hd_enabled()) return;
    const Asset& asset = assets[size_t(current_map)];
    {
        std::lock_guard lock(asset_mutex);
        if (asset.state != Asset::State::ready) return;
    }
    // The cursor (object 0x35) holds the cell's map-pixel origin as floats.
    float cx, cy;
    const uint32_t wx = word(rdram, 0x80102308), wy = word(rdram, 0x8010230C);
    std::memcpy(&cx, &wx, 4); std::memcpy(&cy, &wy, 4);
    const int px = int(cx), py = int(cy);
    if (px < 0 || py < 0 || px + 16 > asset.width || py + 16 > asset.height) return;
    // The panel's list: TLUT load, the atlas cell load, then one texture rectangle.
    uint32_t image = 0, tlut = 0, rect = 0;
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (uint32_t p = draw.dl_begin; p + 8 <= draw.dl_end; p += 8) {
        const uint32_t w0 = word(rdram, p), op = w0 >> 24;
        if (op == 0xFD) image = word(rdram, p + 4);
        else if (op == 0xF0) tlut = image;
        else if ((op == 0xE4 || op == 0xE5) && p + 24 <= draw.dl_end && !rect) {
            const uint32_t w1 = word(rdram, p + 4);
            x0 = (w1 >> 12) & 0xFFF; y0 = w1 & 0xFFF; x1 = (w0 >> 12) & 0xFFF; y1 = w0 & 0xFFF;
            rect = p;
            p += 16;
        }
    }
    // The SETTILESIZE just before the rectangle carries the tag; the rectangle itself stays
    // as the marker, so the panel's own frame and background are untouched.
    if (!rect || !tlut || x1 <= x0 || y1 <= y0 || rect - 8 < draw.dl_begin || (word(rdram, rect - 8) >> 24) != 0xF2) {
        ++panel_unmarked;
        return;
    }
    std::atomic_ref<uint64_t>(assets[size_t(current_map)].last_draw).store(++draw_clock);
    Draw record;
    record.asset = current_map;
    record.colony_frame = rdram[(0x80178C6D & 0x1FFFFFFF) ^ 3] & 7;
    for (int i = 0; i < 256; ++i) record.live[i] = rgba5551(half(rdram, tlut + i * 2));
    record.rect[0] = x0 / 4.0f; record.rect[1] = y0 / 4.0f;
    record.rect[2] = x1 / 4.0f; record.rect[3] = y1 / 4.0f;
    // Half an HD texel inside the cell, so bilinear sampling never pulls in a neighbour.
    const float inset = 0.5f / asset.scale;
    record.uv[0] = (px + inset) / asset.width;  record.uv[1] = (py + inset) / asset.height;
    record.uv[2] = (px + 16 - inset) / asset.width; record.uv[3] = (py + 16 - inset) / asset.height;
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    put(rdram, rect + 8, 0xE1000000); put(rdram, rect + 12, 0);
    put(rdram, rect + 16, 0xF1000000); put(rdram, rect + 20, 0x04000400);
    put(rdram, rect - 8, kTagW0); put(rdram, rect - 4, kTagW1 | record.id);
    ++panel_rewritten;
}

void shutdown() {
    if (assets.empty() && !rom_frames) return;
    {
        std::lock_guard lock(asset_mutex);
        stopping = true;
    }
    decode_wake.notify_all();
    if (decoder.joinable()) decoder.join();
    size_t frames_ready = 0;
    for (const auto& asset : assets) frames_ready += asset.state == Asset::State::ready && asset.alpha;
    std::ofstream(output / "hd-map-summary.json") << json({{"schema", "srw64.hd-map-run.v0"},
        {"maps", assets.size() - frame_assets}, {"frames", frame_assets}, {"frames_drawn", frames_ready},
        {"rewritten_draws", rewritten.load()}, {"native_draws", rendered.load()},
        {"colony_draws", colony_draws.load()}, {"colony_frames_seen_mask", colony_frames_seen.load()},
        {"evicted", evicted.load()}, {"decode_failures", decode_failures.load()},
        {"panel_draws", panel_rewritten.load()}, {"panel_unmarked", panel_unmarked.load()}, {"overview_draws", overview_rewritten.load()},
        {"skipped", skipped.load()}, {"unmarked_draws", no_marker.load()}}).dump(2) << '\n';
    for (auto& asset : assets) {
        asset.textures.reset();
        asset.base_texture.reset();
        asset.index_texture.reset();
    }
    for (auto& frame : colony_frames) {
        frame.textures.reset(); frame.base_texture.reset(); frame.index_texture.reset();
    }
    colony_events.close();
    program.reset();
}
}
