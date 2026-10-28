#define HLSL_CPU
#include "native_portrait.hpp"
#include "app/runtime.hpp"
#include "battle_hud.hpp"
#include "presentation/image_mode.hpp"
#include "presentation/rgba_file.hpp"
#ifndef SRW64_GL_ONLY
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#endif
#include "native_gpu.hpp"
#include "native_quads.hpp"
#include "hd_texture.hpp"
#include "mod_library.hpp"
#include "portrait_identities_table.hpp"
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
#include <optional>
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
    int size = 0;                            // square; the manifest's, or a MOD picture's own
    std::filesystem::path file;
    std::filesystem::file_time_type stamp{}; // a MOD picture's, to know a replaced file
    std::vector<std::vector<uint8_t>> levels;  // premultiplied RGBA8 mip chain, dropped after upload
    std::unique_ptr<gpu::Texture> texture;
    std::unique_ptr<plume::RenderDescriptorSet> set;
    Clock::time_point used{};
    // The GL renderer's (gl_quads): the picture as it uploads it, compressed or RGBA8.
    std::shared_ptr<const std::vector<uint8_t>> gl_pixels;
    uint32_t gl_format = 0, gl_levels = 1;
};

struct Draw {
    uint32_t id = 0;
    int asset = -1;
    bool flip = false, silhouette = false;
    float rect[4]{};                         // N64 screen pixels
};

int size = 0;
float silhouette_rgb[3]{};
// Reserved once and never reallocated: draws on other threads index it while MOD pictures
// join at the end (kCapacity bounds them).
constexpr size_t kCapacity = 2048;
std::vector<Asset> assets = [] { std::vector<Asset> v; v.reserve(kCapacity); return v; }();
// The sprite record holds resource handles, so a portrait is recognised by FNV-1a 64
// digests of the index and palette data its SETTIMGs point at: (pixels, palette) ->
// (asset, silhouette). One pair of images shares its pixels and differs by palette.
std::map<std::pair<uint64_t, uint64_t>, std::pair<int, bool>> by_content;
// MOD portraits (art/portraits/<pilot>.png of the enabled packages, docs/design/mod-names-
// art-data.md §2.2), the same identities, looked up before the HD pack's and drawn whether
// or not the HD pictures are on: rebuilt when the packages change.
std::map<std::pair<uint64_t, uint64_t>, std::pair<int, bool>> mod_by_content;
std::map<unsigned, PilotImage> pilot_images;
std::map<std::string, std::filesystem::path> mod_pages;          // original page picture -> MOD file
std::mutex mod_mutex;
std::optional<unsigned> mod_revision;
mods::Version mod_game{-1, -1, -1};
bool hooks_installed = false;
std::mutex asset_mutex;                      // decoded levels and textures cross threads
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
#ifndef SRW64_GL_ONLY
RT64::NativeMeshClassify *previous_classify{};
RT64::NativeMeshRender *previous_render{};
std::unique_ptr<gpu::Program> program;      // src/host/shaders/HdPortrait{VS,PS}.hlsl
#endif
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
    auto image = presentation::load_rgba(asset.file);
    if (image.width != asset.size || image.height != asset.size) throw std::runtime_error("HD portrait size differs from the manifest: " + asset.file.string());
    std::vector<uint8_t> level = std::move(image.pixels);
    for (size_t i = 0; i < level.size(); i += 4)
        for (int c = 0; c < 3; ++c) level[i + c] = uint8_t((level[i + c] * level[i + 3] + 127) / 255);
    asset.levels.clear();
    asset.levels.push_back(std::move(level));
    for (int s = asset.size; s > 1; s /= 2) {
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

#ifdef SRW64_GL_ONLY
// The GL renderer's picture: the prepared, compressed copy where there is one (the Switch,
// hd_texture.hpp), else the decoded level 0. Idle ones beyond kResident go again.
void gl_prepare(Asset& asset) {
    const auto now = Clock::now();
    size_t resident = 0;
    for (const auto& other : assets) resident += other.gl_pixels != nullptr;
    if (resident >= kResident)
        for (auto& other : assets)
            if (other.gl_pixels && now - other.used > kIdle) { other.gl_pixels.reset(); ++released; }
    asset.used = now;
    hd_texture::Compressed c;
    if (hd_texture::load(asset.file, c) && c.width == uint32_t(asset.size) && c.height == uint32_t(asset.size)) {
        asset.gl_pixels = c.data; asset.gl_format = c.format; asset.gl_levels = c.levels;
        ++decoded;
        return;
    }
    decode(asset);
    asset.gl_pixels = std::make_shared<const std::vector<uint8_t>>(std::move(asset.levels.front()));
    asset.gl_format = 0; asset.gl_levels = 1;
    asset.levels.clear();
}

bool gl_quads(uint32_t id, std::vector<native_quads::Quad>& out) {
    if ((id & 0xFFFF0000u) != kIdBase) return false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[id % kRing];
    }
    if (draw.id != (id & 0xFFFF) || draw.asset < 0) return true;
    std::lock_guard lock(asset_mutex);
    Asset& asset = assets[size_t(draw.asset)];
    if (!asset.gl_pixels) return true;
    asset.used = Clock::now();
    native_quads::Quad q;
    q.pixels = asset.gl_pixels;
    q.width = q.height = uint32_t(asset.size);
    q.format = asset.gl_format; q.levels = asset.gl_levels;
    std::copy(draw.rect, draw.rect + 4, q.rect);
    q.uv[0] = draw.flip ? 1.f : 0.f; q.uv[1] = 0; q.uv[2] = draw.flip ? 0.f : 1.f; q.uv[3] = 1;
    if (draw.silhouette) { std::copy(silhouette_rgb, silhouette_rgb + 3, q.fill); q.fill[3] = 1; }
    q.label = "portrait";
    out.push_back(std::move(q));
    ++rendered;
    return true;
}

#else
uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

// Workload thread. Upload on first use; release textures idle long enough that no
// recorded work can still read them (gpu::Texture also waits before freeing).
plume::RenderDescriptorSet* texture_for(Asset& asset, plume::RenderCommandList* list) {
    std::lock_guard lock(asset_mutex);
    const auto now = Clock::now();
    if (!asset.set) {
        if (asset.levels.empty()) return nullptr;
        size_t resident = 0;
        for (auto& other : assets) resident += other.set != nullptr;
        if (resident >= kResident)
            for (auto& other : assets)
                if (other.set && now - other.used > kIdle) { other.set.reset(); other.texture.reset(); ++released; }
        asset.texture = std::make_unique<gpu::Texture>(uint32_t(asset.size), uint32_t(asset.size), plume::RenderFormat::R8G8B8A8_UNORM,
                                                       std::move(asset.levels));
        asset.levels.clear();
        asset.levels.shrink_to_fit();
        if (!asset.texture->upload(list)) { asset.texture.reset(); return nullptr; }
        asset.set = program->bind({asset.texture.get()});
    }
    asset.used = now;
    return asset.set.get();
}

bool render(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) != kIdBase) return previous_render ? previous_render(list, framebuffer, call) : false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[call.id % kRing];
    }
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !program) { ++skipped; return true; }
    auto* set = texture_for(assets[size_t(draw.asset)], list);
    if (!set) { ++skipped; return true; }
    // HdPortraitVS/PS data: rect, uv, resolution, fill.
    struct { float rect[4], uv[4], resolution[4], fill[4]; } u{};
    std::memcpy(u.rect, draw.rect, sizeof(u.rect));
    // The quad is in full-frame N64 coordinates (see native_map.cpp on screenScale).
    u.uv[0] = draw.flip ? 1.f : 0.f; u.uv[1] = 0; u.uv[2] = draw.flip ? 0.f : 1.f; u.uv[3] = 1;
    gpu::frame_resolution(call, u.resolution);
    u.fill[0] = silhouette_rgb[0]; u.fill[1] = silhouette_rgb[1]; u.fill[2] = silhouette_rgb[2];
    u.fill[3] = draw.silhouette ? 1.f : 0.f;
    gpu::State state;
    state.blend = gpu::Blend::premultiplied;
    if (!program->begin(list, framebuffer, call, state, set, gpu::push_data(&u, sizeof(u)))) { ++skipped; return true; }
    list->drawInstanced(4, 1, 0, 0);
    ++rendered;
    return true;
}
#endif
}

void install() {
    if (hooks_installed) return;
    hooks_installed = true;
#ifdef SRW64_GL_ONLY
    native_quads::add(gl_quads);
#else
    // Chain after marker/hdmap, which install their hooks first.
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
#endif
    for (int c = 0; c < 3; ++c) silhouette_rgb[c] = kSilhouetteRgb[c] / 255.f;
}

void configure(const std::filesystem::path& art_directory, const std::filesystem::path& directory) {
    const auto spec_path = art_directory / "srw64-portraits-hd.json";
    if (!std::filesystem::exists(spec_path)) return;
    output = directory;
    const json spec = json::parse(app::read_text(spec_path, 64u << 20));
    if (spec.at("schema") != "srw64.portrait-images.v1" || spec.at("source_size") != 96)
        throw std::runtime_error("Unsupported HD portrait specification");
    size = spec.at("size").get<int>();
    if (size < 96 || size > 2048 || size % 96 != 0) throw std::runtime_error("Unsupported HD portrait size");
    const uint64_t silhouette = hex64(spec.at("silhouette").at("palette_fnv1a64"));
    install();
    for (int c = 0; c < 3; ++c) silhouette_rgb[c] = spec.at("silhouette").at("rgb")[c].get<float>() / 255.f;
    std::lock_guard lock(asset_mutex);
    for (const auto& row : spec.at("images")) {
        Asset asset;
        asset.image = row.at("image").get<uint16_t>();
        asset.size = size;
        asset.file = art_directory / row.at("file").get<std::string>();
        if (!hd_texture::exists(asset.file)) throw std::runtime_error("Missing HD portrait " + asset.file.string());
        if (assets.size() >= kCapacity) throw std::runtime_error("Too many portraits");
        const uint64_t pixels = hex64(row.at("pixels_fnv1a64"));
        if (!by_content.emplace(std::make_pair(pixels, hex64(row.at("palette_fnv1a64"))), std::make_pair(int(assets.size()), false)).second)
            throw std::runtime_error("Duplicate HD portrait identity");
        by_content.emplace(std::make_pair(pixels, silhouette), std::make_pair(int(assets.size()), true));
        assets.push_back(std::move(asset));
    }
    fprintf(stderr, "SRW64_HD_PORTRAITS loaded %zu portrait(s) at %dpx\n", assets.size(), size);
}

void set_pilot_images(std::map<unsigned, PilotImage> images) {
    std::lock_guard lock(mod_mutex);
    pilot_images = std::move(images);
    mod_revision.reset();
}

namespace {
// Game thread, from rewrite: the enabled packages' art/portraits/<pilot>.png again when they
// change. A picture is square, 96 to 2048 pixels and a multiple of 96; the upper package wins.
void refresh_mods() {
    const unsigned now = mods::revision();
    const auto game = mods::game_version();
    std::lock_guard lock(mod_mutex);
    if (mod_revision == now && game == mod_game) return;
    mod_revision = now; mod_game = game;
    std::map<unsigned, std::filesystem::path> files;
    std::vector<std::string> problems;
    std::error_code error;
    for (const auto& p : mods::installed(game)) {
        const auto dir = p.folder / "art" / "portraits";
        if (!p.active || !std::filesystem::is_directory(dir, error)) continue;
        for (const auto& item : std::filesystem::directory_iterator(dir, error)) {
            const auto stem = item.path().stem().string();
            auto ext = item.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".png") continue;
            const bool number = !stem.empty() && stem.size() <= 3 && std::all_of(stem.begin(), stem.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                                !(stem.size() > 1 && stem[0] == '0');
            const unsigned pilot = number ? unsigned(std::stoul(stem)) : 0;
            if (!number || !pilot_images.contains(pilot)) { problems.push_back(p.id() + " art/portraits/" + item.path().filename().string() + "：文件名要是有头像的人物号"); continue; }
            int w = 0, h = 0, n = 0;
            if (!stbi_info(item.path().string().c_str(), &w, &h, &n) || w != h || w < 96 || w > 2048 || w % 96) {
                problems.push_back(p.id() + " art/portraits/" + item.path().filename().string() + "：要是 96–2048 像素、96 的倍数的正方形 PNG");
                continue;
            }
            files[pilot] = item.path();
        }
    }
    std::map<std::pair<uint64_t, uint64_t>, std::pair<int, bool>> table;
    std::map<std::string, std::filesystem::path> pages;
    for (const auto& [pilot, file] : files) pages[pilot_images.at(pilot).path] = file;
    {
        std::lock_guard assets_lock(asset_mutex);
        for (const auto& [pilot, file] : files) {
            const uint16_t image = pilot_images.at(pilot).image, palette = pilot_images.at(pilot).palette;
            const auto identity = std::find_if(std::begin(kIdentities), std::end(kIdentities),
                                               [&](const Identity& i) { return i.image == image && i.palette == palette; });
            if (identity == std::end(kIdentities)) { problems.push_back("art/portraits/" + std::to_string(pilot) + ".png：这个人物的头像认不出来"); continue; }
            int w = 0, h = 0, n = 0;
            stbi_info(file.string().c_str(), &w, &h, &n);
            const auto stamp = std::filesystem::last_write_time(file, error);
            // A picture already loaded (same file, size and time) is used again.
            int index = -1;
            for (size_t i = 0; i < assets.size(); ++i)
                if (assets[i].file == file && assets[i].size == w && assets[i].stamp == stamp) index = int(i);
            if (index < 0) {
                if (assets.size() >= kCapacity) { problems.push_back("too many MOD portraits"); break; }
                Asset asset;
                asset.stamp = stamp;
                asset.size = w;
                asset.file = file;
                index = int(assets.size());
                assets.push_back(std::move(asset));
            }
            table[{identity->pixels, identity->palette_digest}] = {index, false};
            table[{identity->pixels, kSilhouettePalette}] = {index, true};
        }
    }
    mod_by_content = std::move(table);
    mod_pages = std::move(pages);
    for (const auto& problem : problems) fprintf(stderr, "SRW64_MOD_PORTRAITS_PROBLEM %s\n", problem.c_str());
    fprintf(stderr, "SRW64_MOD_PORTRAITS portraits=%zu problems=%zu\n", files.size(), problems.size());
}
}

void gpu_init() {
#ifndef SRW64_GL_ONLY
    // A linear, mipmapped, clamped sampler after the portrait.
    program = std::make_unique<gpu::Program>("HdPortrait", 1, std::vector<gpu::Sampler>{{.linear = true, .mipmaps = true}});
#endif
}

std::string mod_picture(const std::string& original_path) {
    refresh_mods();
    std::lock_guard lock(mod_mutex);
    const auto found = mod_pages.find(original_path);
    return found == mod_pages.end() ? std::string() : found->second.string();
}

std::string mod_picture_of(unsigned pilot) {
    refresh_mods();
    std::lock_guard lock(mod_mutex);
    const auto image = pilot_images.find(pilot);
    if (image == pilot_images.end()) return {};
    const auto found = mod_pages.find(image->second.path);
    return found == mod_pages.end() ? std::string() : found->second.string();
}

void rewrite(uint8_t* rdram, const PortraitDraw& draw) {
    if (!hooks_installed) return;
    refresh_mods();
    bool any_mod;
    { std::lock_guard lock(mod_mutex); any_mod = !mod_by_content.empty(); }
    if (!any_mod && (by_content.empty() || !hd_enabled())) return;
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
    const std::pair<uint64_t, uint64_t> key{digest(rdram, image, width * width), digest(rdram, palette, 128)};
    std::pair<int, bool> hit{-1, false};
    {
        std::lock_guard lock(mod_mutex);
        if (const auto m = mod_by_content.find(key); m != mod_by_content.end()) hit = m->second;
    }
    if (hit.first < 0 && hd_enabled())
        if (const auto f = by_content.find(key); f != by_content.end()) hit = f->second;
    if (hit.first < 0) { ++unknown; return; }
    const auto found = std::make_pair(key, hit);
    Asset& asset = assets[size_t(found.second.first)];
    {
        std::lock_guard lock(asset_mutex);
#ifdef SRW64_GL_ONLY
        if (!asset.gl_pixels) gl_prepare(asset);
#else
        if (!asset.set && asset.levels.empty()) decode(asset);
#endif
    }
    Draw record;
    record.asset = found.second.first;
    record.flip = flip;
    record.silhouette = found.second.second;
    record.rect[0] = x0 / 4.0f; record.rect[1] = y0 / 4.0f;
    record.rect[2] = x1 / 4.0f; record.rect[3] = y1 / 4.0f;
    // In the battle the portrait moves to its corner with the tiles it replaces (battle_hud.hpp).
    float dx, dy;
    battle_hud::portrait_offset(record.rect[0], dx, dy);
    record.rect[0] += dx; record.rect[2] += dx; record.rect[1] += dy; record.rect[3] += dy;
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
        for (const auto& asset : assets) resident += asset.set != nullptr;
        std::ofstream(output / "hd-portrait-summary.json") << json({{"schema", "srw64.hd-portrait-run.v1"},
            {"portraits", assets.size()}, {"size", size}, {"rewritten_draws", rewritten.load()}, {"native_draws", rendered.load()},
            {"skipped", skipped.load()}, {"unexpected_layout", unexpected.load()}, {"unknown_content", unknown.load()},
            {"decoded", decoded.load()}, {"released", released.load()}, {"resident_at_exit", resident},
            {"unexpected_shapes", [] { std::lock_guard lock(shape_mutex); return json(unexpected_shapes); }()}}).dump(2) << '\n';
    }
    for (auto& asset : assets) { asset.set.reset(); asset.texture.reset(); asset.gl_pixels.reset(); }
#ifndef SRW64_GL_ONLY
    program.reset();
#endif
}
}
